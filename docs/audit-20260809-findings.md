# MfPlayer 全仓库审计 — 2026-08-09

四单只读审计（codex / blueprint 流程），分区互不重叠，每单自行选取维度。
共 **26 条** finding。**已核实 13 条（其中 A-1、C-1、D-3、E-2 已修），剩 13 条待核实**（2026-08-10）。

## 怎么用这个文档

- **「老王定级」是报告里的原始定级，不可信** —— 实测它一贯偏高。真正的定级以核实后为准。
- 状态三档：`已核实` / `未核实` / `驳回`。**未核实的一律不许直接开修。**
- 核实方法：逐条读代码，确认触发路径真实存在、上游没有守卫拦住。
- 核实成本会超过挖掘成本，量大就分批。

| 分区 | 范围 | 条数 |
|---|---|---|
| 单 1 | `core/playback/` + `platform/rendering/mpv/` | 5 |
| 单 2 | `core/cache/` + `core/network/` | 5 |
| 单 3 | `core/library/` `detail/` `server/` `settings/` `media/` `providers/` | 9 |
| 单 4 | `ui/qml/` | 7 |

---

> **路径更正**：原报告的路径普遍少写一层目录，核实时按下表换算 ——
>
> | 报告里写的 | 真实路径 |
> |---|---|
> | `embyclient.cpp` | `core/providers/emby/embyclient.cpp` |
> | `mediamodel.cpp` | `core/media/models/mediamodel.cpp` |
> | `LibraryGridView.qml` | `ui/qml/views/LibraryGridView.qml` |
>
> 行号本身是准的，只是目录层级不全。后面核实剩下几条时先 `find` 一下再读。

## 已核实（10 条）

### A-1 · ImageProvider 在 reader 线程无保护读取 `m_imageCache`
- **状态**：`已修` — 2026-08-10，`b5a599d`
- **修法**：把主线程解析好的路径随 provider URL 带走
  （`image://imgcache/<md5>/<base64url(路径)>`），provider 不再持有也不访问 CacheStore。
  **不是加锁** —— 详见 commit message 与 CLAUDE.md 的红线条目。
  方案对比由 `/codex:adversarial-review` 产出（Codex session `019fe8ac`），
  其五条关键事实经逐行核实全部成立；第四方案由它提出，人工审收后采纳并补了
  两条：LRU 键与携带路径分离、provider 路径硬收口在缓存目录内。
- **老王定级**：高 · **核实后**：高
- **来源**：单 2
- **位置**：`core/cache/imagecacheprovider.cpp:121`、`core/cache/cachestore.cpp:343`

`ImageCacheProvider : public QQuickAsyncImageProvider`（`imagecacheprovider.h:42`），
其 `requestImageResponse()` 由 **Qt 的 pixmap reader 线程**调用，不是主线程 —— 这正是
async provider 存在的意义。它在第 121 行调用 `m_cache->resolveImagePath(hash)`，
后者对 `m_imageCache`（QHash）做 `constFind()`。

包在外面的 `QMutexLocker lock(&m_mutex)` 锁的是 **provider 自己的** mutex；
CacheStore 主线程写 `m_imageCache`（启动批量插入、下载完成、过期、清理）时
**不持有这把锁**，两者形成不了同步关系。

失败场景：启动时 DBWorker 扫描完成 → 主线程批量插入触发 QHash rehash →
同时首屏图片请求进入 reader 线程做 `constFind()` → 数据竞争，UB。
表现为偶发崩溃、堆损坏、路径错误命中或图片随机缺失。

> **⚠️ 这条推翻了 CLAUDE.md 的一条既定决策。**
> 那里写着「`m_imageCache` / `m_pendingDownloads`: 主线程独占，无 mutex。别加回
> `m_imageCacheMutex`」，而 `cachestore.cpp:344` 的注释更是白纸黑字
> "All accesses are on the main thread, no mutex needed."
> **这个前提是错的。** 决策本身要重新做，不是简单加锁了事 ——
> 加锁会打掉当初做这个决策想要的东西，得先想清楚换哪种方案
> （主线程 QueuedConnection 解析路径？provider 侧自持快照？读写锁？）。
> 这是设计决策，不外包。

### A-2 · 手动换片期间旧片 EOF，会用新条目的 id 结算并清空它
- **状态**：`已核实`
- **老王定级**：中 · **核实后**：中
- **来源**：单 1
- **位置**：`core/playback/playbackcontroller.cpp:129`、`:48`、`:106`

`playItem(B)` 立刻把成员状态切到 B，但在等 B 的 `fetchPlaybackInfo` 期间
**没有停掉正在播的 A**。`m_playGeneration` 只保护 PlaybackInfo 回调，
`fileLoaded` / `endOfFile` 不受它保护，也不带文件身份。

A 在这个窗口内自然 EOF → `reportStopForCurrent()` 用
**B 的 itemId + A 的 sessionId + B 的起播位置**发 stop，然后 `clear()` 掉 itemId。
之后 B 的回调照常 `reportPlaybackStart()` 和 `loadfile`，但成员 itemId 已空 ——
**B 整场的周期进度和最终 stop 全部静默跳过**。

窗口 = fetchPlaybackInfo 的 RTT（几十到几百 ms），需要 A 恰好在此期间播完。
概率低但真实，后果是"看完了但进度没记住"。

### A-3 · OpenGL 离屏 FBO 从未绑定就附加纹理
- **状态**：`已核实` — 实锤
- **老王定级**：中 · **核实后**：中（仅 OpenGL 后端）
- **来源**：单 1
- **位置**：`platform/rendering/mpv/mpvrenderitem.cpp:493-501`

```cpp
f->glGenFramebuffers(1, &st.offFbo);   // 新建，但从没 bind
f->glGenTextures(1, &st.offTex);
f->glBindTexture(GL_TEXTURE_2D, st.offTex);
...
f->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, ...);  // 挂到当前绑定的 FBO
```

`glGenFramebuffers` 不会绑定新对象，中间也没有
`glBindFramebuffer(GL_FRAMEBUFFER, st.offFbo)`。纹理因此挂到了调用前绑定的
**Qt render target** 上；新建的 offFbo 一直没有 color attachment，
513 行绑上去时是 incomplete。

D3D11 / Vulkan 路径不经过这里，Windows 主路径不受影响。

---

## D-3 · 失败路径照调成功回调，而且错误信号没人接
- **状态**：`已修` — 2026-08-10，`07341b9`（方案 A：只加 `return`）
- **未做**：`networkError` 仍是死信号，失败的表现就是 UI 不动。要不要给用户可见提示是单独一件事。
- **老王定级**：中 · **核实后**：**中偏高**
- **位置**：`core/providers/emby/embyclient.cpp:166-175`、`:186-195`、`:523`、`:530`、`:539`、`:546`

```cpp
if (!r.ok()) {
    if (r.httpStatus == 401) emit guard->tokenExpired();
    else                     emit guard->networkError(r.errorString());
}
cb(QJsonDocument::fromJson(r.ok() ? r.body : QByteArray()));   // ← 无条件
```

`postJson` / `deleteJson` 失败时只是"顺便喊一声"，callback 照调。
四个调用点全中：`markPlayed` / `markUnplayed` / `addFavorite` / `removeFavorite`
一律无条件 `emit playedStatusChanged(id, true)` / `favoriteChanged(id, true)`。

**比原报告更严重的一点**：`networkError` 全仓库 **3 处 emit、0 个 connect** ——
它是个死信号。所以断网点收藏时用户连错误提示都看不到，红心照亮，服务器上什么都没发生，
本地缓存还被写成已收藏。

修这条要连带决定 `networkError` 到底接不接 —— 不接的话，失败就只能靠 UI 不变来体现。

---

## 根因一：异步响应不带请求身份（D-1 / D-2 / D-4）

三条是同一个形状 —— 响应回来时用「**当前**的成员变量」推断「这个响应属于谁」。
飞行期间成员变量被改过，判断就错，而且错得静默。

### D-1 · `librariesFetched` 的 generation 守卫是摆设
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`core/server/servermanager.cpp:44`、`:66`、`:89`、`:169`

```cpp
// switchToServer()
++m_serverGeneration;                          // :66
m_librariesGeneration = m_serverGeneration;    // :89  ← 同一次里跟着设成一样
// onLibrariesFetched()
if (m_librariesGeneration != m_serverGeneration) return;   // :169  ← 永远为真
```

两个变量在每次切换里被**同步更新**，所以这个判断拦不下任何东西。
连切 A→B→C，A 的库列表回来照样通过，被当成 C 的发出去。

唯一有效的场景是 `addServer()`（:44）：那里 `++m_serverGeneration` 在前、
`m_librariesGeneration` 要等 `loginSuccess`（:55）才设，中间存在真实窗口。
**即：这个守卫只在登录期间有效，切换已登录服务器时完全失效。**

### D-2 · `seasonsFetched` / `episodesFetched` 不带 id，且污染会自我放大
- **状态**：`已核实` · **老王定级**：中 · **核实后**：**中偏高**
- **位置**：`core/providers/emby/embyclient.cpp:238`、`:251`、`core/detail/detailmanager.cpp:259`、`:264`、`:269`

信号签名只有 `QJsonArray`，不带 `seriesId` / `seasonId`。
`DetailManager` 两个槽全程用当前成员变量：

```cpp
void DetailManager::onSeasonsFetched(const QJsonArray &seasons) {
    m_cache->putSeasons(m_currentSeriesId, sorted);                 // :259 污染 SQLite
    ...
    m_currentSeasonId = sorted.first().toObject()["Id"].toString(); // :263
    m_emby->fetchEpisodes(m_currentSeriesId, m_currentSeasonId);    // :264 ← B 的剧集 id 配 A 的季 id
}
```

比原报告更狠的两点：
1. **持久化** —— `putSeasons` / `putEpisodes` 写的是 SQLite，污染不随重启消失。
2. **自我放大** —— :264 会立刻用错位的组合再发一个请求，回来再污染一次集缓存。

另外 :261 的 `emit seasonsChanged()` 是**无参全局信号** —— 这正是 E-1 的另一半。

### D-4 · `onItemsFetched` 只比 parentId，不看 generation
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`core/library/librarybrowser.cpp:504`、`:272`、`:283`

```cpp
if (parentId != m_currentLibraryId) return;   // :504 ← 唯一的守卫
m_contentModel->setItems(items);              // :507
```

`m_browseGeneration` 在 `setLibraryTab` / `setSortBy` / `setFilter`（:194 / :301 / :317）
都自增了，但这些操作**不改 parentId**，守卫形同虚设。只有 `loadMore()`（:272 捕获、
:283 校验）真正用了它。

而且 :526-527 会接着按**新**条件 `loadMore()` 续拉，于是列表变成旧排序首屏 + 新排序续页。

---

## 根因二：QML 侧不校验数据/事件属于当前页（E-1 / E-2 / E-3）

### E-1 · `onSeasonsChanged` 的守卫验错了对象
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`ui/qml/pages/DetailPage.qml:123`

```qml
function onSeasonsChanged() {
    if (!detailRoot.itemData || detailRoot.itemData.Id !== detailRoot.itemId) return
    ...
    detailRoot.currentSeasonIdx = 0
}
```

**守卫是存在的**，但它检查的是「我自己的 itemData 跟我自己的 itemId 对不对得上」——
留在 StackView 里的后台页 A 当然对得上，它是 A 的数据配 A 的 id。
**它验的是内部一致性，不是事件归属。**

失败路径：详情页 A 选了第 3 季 → 从 A push 到别的详情页（点相似影片/演员）→
打开剧集 B 触发 `Detail.fetchSeasons(B)` → `emit seasonsChanged()` 广播 →
A 的守卫通过 → `A.currentSeasonIdx = 0`，"季选择状态保持"被打掉。

### E-2 · `itemData` 在 Emby 换集路径上永不更新
- **状态**：`已核实` · **老王定级**：中 · **核实后**：**中偏高**
- **位置**：`ui/qml/pages/PlayerPage.qml:56`、`:61`、`:164`、`:168-178`

`itemData` 在整个 PlayerPage 里只有三处引用：声明（:56）、`_versionSources` 绑定读取（:61）、
**local 分支**置 null（:164）。`switchEpisode()` 的 **Emby 分支清了 7 个属性
（itemId / startTimeTicks / episodeTitle / episodeSubtitle / mediaSourceId /
audioIndex / subtitleIndex），唯独没清 `itemData`**。

所以不是"切下一集后版本菜单是上一集的" —— 是**整个 PlayerPage 生命周期里，
版本菜单一直是最初从 DetailPage 传进来那一集的**。选中即用错误的 MediaSourceId 起播。

`Detail.browseItem(ep.itemId)`（:176）刷新的是 Detail 单例，不是这个 property，救不了。

### E-3 · 400ms 入场 Timer 落在 pop 过渡的存活窗口里
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`ui/qml/pages/PlayerPage.qml:78`、`ui/qml/pages/DetailPage.qml:81`、`ui/qml/theme/Nav.qml:21`

两个页面都是 `Component.onCompleted: timer.start()`，interval 400ms，
**退场时既不 stop 也不检查 `StackView.status`**（全文件搜 `onDeactivating` /
`Component.onDestruction` 只有 PlayerPage:740 的 `setSubPos(100)`）。

`Nav.pushDetail` 走 `pageStack.push(detailPage, {...})` 的 Component 形式，
pop 后销毁发生在过渡**结束之后**：

```
t=0     push，timer 启动（400ms）
t=350   push 动画完成
t≈360   用户返回 → pop 开始，popExit 动画 350ms
t=400   timer 触发 ← 组件还活着，要到 t≈710 才销毁
t=710   销毁
```

- DetailPage → `Detail.browseItem(itemId)`，污染已经离开的页面对应的共享 Detail 单例
- PlayerPage → `Playback.playItem(...)`，**在用户已经退出的播放器上起播**

---

## C-1 · `clearImageCache()` 清不干净，残留下载会把缓存写回来
- **状态**：`已修` — 2026-08-10，`741ddc9`
- **修法**：不逐个取消，在整条异步链唯一的汇合点（IO 池写完回主线程那步）设一道闸 ——
  `doFetchImage()` 带上发起时的 `m_writeGeneration`，重试链一路传递，写回前比对；
  过期就 `QFile::remove(savePath)` 免得留孤儿文件。外加 `m_downloadQueue.clear()`。
  `m_failedUrls` **故意不清**（CDN 冷却表，清掉等于清缓存后把失败 URL 一次性重打一遍）。
- **老王定级**：中 · **核实后**：**中偏高**
- **位置**：`core/cache/cachestore.cpp:398`、`:474`、`:487`、`:505`、`core/cache/dbworker.cpp:155`、`:303`

`clearImageCache()` 只清两样：

```cpp
m_imageCache.clear();
m_pendingDownloads.clear();
```

**`m_downloadQueue` 没清**（`:474` append，`:487` takeFirst），下载完成的 `done()`
还会接着 `processDownloadQueue()` 拉下一个；`doFetchImage` 的重试
`QTimer::singleShot(kImageRetryDelayMs, ...)` 也没人取消。清空后队列里排着的任务照跑到底。

跑完就走 `putImagePath()` → 写 `m_imageCache` + 落盘 + 转发 DBWorker，
而 `DBWorker::putImagePath()` 是 **`Q_UNUSED(generation)`** —— 上游那个
`++m_writeGeneration` 对它毫无作用，SQL 照写。

**比原报告更狠的一点在时序。** `DBWorker::clearImageCache()` 是异步的
（`DELETE FROM images` → `removeRecursively()` → `mkpath()`），残留下载落在它前后是两种坏法：

```
落在 removeRecursively 之后 → 文件+内存+SQLite 三处复活，清了个寂寞（能自愈）
落在 removeRecursively 之前 → 文件被删，m_imageCache 里的条目留着 ← 幽灵条目
```

幽灵条目是不自愈的：`providerUrl()` 命中就返回路径，provider 读不到文件给张透明图，
**而且因为"命中了"永远不会重新下载**。用户看到的是几张永久空白的封面，
清缓存、重启都不一定好（`loadImageCache()` 启动时会校验文件存在，重启能好；
本次运行内不会）。

修的时候注意：清队列 + 取消重试 timer 是主线程的事，`Q_UNUSED(generation)` 是 DBWorker 的事，
两处都要动，只补一处仍会漏。

---

## D-5 · `size + firstId` 指纹碰撞，列表重复一项、缺一项
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`core/media/models/mediamodel.cpp:62`、`:84`、`core/library/librarybrowser.cpp:183`、`:263`、`:280`

```cpp
if (items.size() == m_lastSourceSize
    && (items.isEmpty() || firstId == m_lastSourceFirstId))
    return;                          // ← 认定"数据没变", 跳过 reset
```

指纹只有**条数**和**第一条的 Id**。首屏是分页拉的（`limit = m_paginationLimit`），
所以「库中部新增一部影片」这个再普通不过的情况下，两项都不变：

```
旧首屏 20 条: [A B C … S T]        firstId=A, size=20
新增一部 M', 服务器新顺序前 20 条: [A B C … M' … S]   firstId=A, size=20
                                    ↑ 指纹一模一样 → setItems 直接 return
```

模型里留着旧的 20 条。接着 `loadMore()` 用
`startIndex = m_contentModel->rowCount()`（`:263`/`:280`）—— 还是 20，
但服务器那边第 21 条已经是**旧的第 20 条**了：

- **T 重复出现**（第 20 位一次，续拉批次里再一次）
- **M' 永远看不到**

而 `appendItems()` 不去重（`:84`，直接 `m_items.append`），重复项实打实进列表。
`m_idToIndex` 里 `insert` 同 key 后指向后一个，所以之后对 T 的原地更新
（收藏/已播状态）只会改到第二份，第一份显示的是陈旧状态。

缓存预显示（`librarybrowser.cpp:183`）是这条的放大器：首屏一定先塞一次缓存数据，
网络数据回来时指纹几乎必然撞上。

---

## E-4 · 「文件夹」Tab 里点文件夹，列表跳回顶部，然后什么都不发生
- **状态**：`已核实` · **老王定级**：中 · **核实后**：中
- **位置**：`ui/qml/views/LibraryGridView.qml:221-228`、`core/detail/detailmanager.cpp:127`、`core/library/librarybrowser.cpp:249`

```qml
if (Library.currentTab === 5) { Library.browseStudio(...); return }
if (Library.currentTab === 4) { Library.browseGenre(...); return }
let type = itemType || ""
if (type === Str.typeMovie || type === Str.typeSeries || type === Str.typeEpisode)
    Nav.pushDetail(itemId)
else { grid.contentY = 0; Detail.browseItem(itemId) }   // ← Folder 落这里
```

`TabFolders = 7` 的拉取（`librarybrowser.cpp:249`）**不设 `includeTypes`、`recursive = false`**，
返回的就是目录下的原样条目，Folder 类型必然在内。两个 Tab 列表
（`BrowsePage.qml:1118` / `:1125`）都有「文件夹」入口，电影库和剧集库都能进。

兜底分支调的 `DetailManager::browseItem()` 只做三件事：清几个 model、取详情、
`emit itemDetailReady`。**它不 push 任何页面，也不改 LibraryBrowser 的浏览上下文。**
唯一在听 `itemDetailReady` 的是 DetailPage，而 DetailPage 根本没被 push。

**比原报告多的一点**：`grid.contentY = 0` 会先把网格**滚回顶部**。
所以用户的实际观感不是"点了没反应"，是"点一下，列表莫名其妙跳到最上面"——
更像 bug，也更难让人想到是点击没被处理。

全仓库没有 `kTypeFolder` 这个常量（grep 无结果），意味着 Folder 从来没被当成一种类型对待过。
这不是漏了个 `if`，是**目录浏览这条链本来就没接**。修法要先定产品：
是进目录（要 LibraryBrowser 支持 parentId 下钻 + 返回栈），还是干脆把这个 Tab 摘掉。

---

## 未核实 · 单 1 · playback + mpv（2 条）

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| B-1 | `mpvcontroller.cpp:434`、`:347`、`.h:120` | 异步 volume/speed 写入拿"旧确认值"去重，会吞掉最终请求；`m_volume` 初值 80 与 libmpv 默认 100 不符，启动时 `setVolume(80)` 被判定为无需设置，随后被观察事件改成 100 并持久化 | 低 |
| B-2 | `playbackcontroller.cpp:276`、`:164`、`:257` | 只含 MediaSources 的临时对象被当成完整详情写回缓存：顶层缺 `RunTimeTicks` → `PlayedPercentage` 恒为 0、`Played` 恒 false，并提交缺 Id/Name 的详情对象 | 低 |
| B-3 | `playbackcontroller.cpp:58`、`:501`、`mpvcontroller.cpp:474` | 外挂字幕用 `sub-add` 异步排队后，同一同步槽内立刻 `fuzzySelectSubtitle()`，此时 track-list 还没有这些轨道；`tracksChanged` 也不会重新触发选择 | 低 |

> 单 1 备注：D3D11 渲染路径、Vulkan 图像所有权/销毁顺序、锁序与控制器 teardown 已读，无额外 finding。

## 未核实 · 单 2 · cache + network（3 条）

> C-1 已于 2026-08-10 核实，移到上面。

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| C-2 | `dbworker.cpp:36` | `stop()` 设 flag + `quit()`，并不排空事件队列（与头文件注释声称的 "drains the event queue" 相反）；退出前最后一批 `putItems()` 丢失 | 低 |
| C-3 | `dbworker.cpp:115` | `putSeasons()` 事务内只做 `INSERT OR REPLACE`，不先删该 `series_id` 旧行 → DB 存的是历次季列表的并集；季被删除后幽灵季会残留到 TTL 过期 | 低 |
| C-4 | `cachestore.cpp:590` | `QFile::write()` 返回值未检查，`writeOk` 只看 `rename()`。短写/写失败后 rename 仍成功 → 截断文件被登记为有效缓存，且因 `m_imageCache` 已命中不会重新下载 | 低 |

> 单 2 备注：CurlEngine 完整读过（easy handle / slist / Task / 取消路径 / 回调销毁引擎），无达到门槛的问题。

## 未核实 · 单 3 · library / detail / server / settings / media / providers（4 条）

> D-1 ~ D-5 已于 2026-08-10 核实，移到上面。

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| D-6 | `librarybrowser.cpp:73`、`:408`、`:420` | 搜索防抖只停未触发的 timer，不失效已发出的请求；输入缩短后旧结果回填空白搜索页。人物响应仅靠布尔值路由，连续搜索会互相吞掉 | 低 |
| D-7 | `librarybrowser.cpp:329`、`embyclient.cpp:646`、`:504` | `browsePerson()` 的 `FetchParams` 不设 `parentId`，回来必然被 `parentId != m_currentLibraryId` 丢弃 → 人物浏览点了没反应（**注：老王看不到 QML，已自行降级；需结合单 4 一起判**） | 低 |
| D-8 | `credentialstore.cpp:35`、`servermanager.cpp:49` | `addServer()` 命中已有 URL+用户名时只更新凭证，不设 active 也不取消原 active；重新登录已保存的非活动服务器后，重启会恢复到错误的服务器 | 低 |
| D-9 | `settingsstore.cpp:28`、`:119`、`.h:38` | HDR 亮度/SDR 白点/seek 步长/窗口尺寸走 200ms 防抖，析构时不提交待写值 → 调整后 200ms 内 Alt+F4 丢失设置 | 低 |

> 单 3 备注：`updateCardField()` 的传播覆盖完整；similar/person/nextUp 的响应 ID 守卫有效；`MediaModel::removeItem()` 正确失效指纹并重建索引。

## 未核实 · 单 4 · ui/qml（3 条）

> E-1 ~ E-4 已于 2026-08-10 核实，移到上面。

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| E-5 | `BrowsePage.qml:1284`、`:1326` | UNC URL `file://NAS/share/x.mkv` 正则去掉 `file://` 后变成相对路径 `NAS/share/x.mkv`，丢掉 UNC 根前缀 → 从 `\\NAS\Videos` 播放/拖入失败 | 低 |
| E-6 | `PlayControlsRow.qml:190`、`:227` | 「下一集」进度的分子取 `nextEpisode.PlaybackPositionTicks`，分母却取 Series 的 `RunTimeTicks` → 空进度条或荒谬的剩余时间 | 低 |
| E-7 | `Main.qml:355`、`:395` | 登录失败的 `errorLabel` 只在下次点"连接"时隐藏，Dialog 的 `onOpened`/`onClosed` 都不重置 → 关掉再打开仍显示上一次的认证错误 | 低 |

> 单 4 备注：HDR 启动遮罩与 PQ overlay、CachedImage/ImageLoadQueue 的取消与并发槽归还、`reuseItems` delegate 的 required roles、主进度滑块的除零守卫与节流、TrackSelector、SettingsDialog、单例与 shader 数值路径已读，无其他发现。

---

## 建议的下一步顺序

1. ~~**A-1**~~ — 已修 (`b5a599d`)。
2. **E-2** — 一行的事（`switchEpisode` 的 Emby 分支补 `itemData = null`），收益却是"版本菜单不再指向错误的媒体源"。性价比最高，先修这个。
3. **D-3** — 修法明确：`if (!r.ok()) return;`。但要连带决定 `networkError` 接不接（现在是死信号）。
4. **D-2** — 污染写进 SQLite 且会自我放大一次，四条 D 里危害最实。
5. **D-1 / D-4** — 与 D-2 同形状：**异步响应不带请求身份**。三条一个修法：把请求身份放进响应，回调里比对。D-4 最省事（`onItemsFetched` 加 generation 参数）。
6. **E-1 / E-3** — 同形状：**QML 侧不校验数据/事件属于当前页**。E-1 是把守卫从"验自己内部一致"改成"验事件归属"；E-3 是退场时 stop timer 或加 `StackView.status` 检查。
7. **A-2** — 修法要和 `m_fileLoaded` 的改动放一起想（同属播放结算链）。
8. **A-3** — 实锤但只影响 OpenGL 后端，Windows 主路径碰不到，可以缓。
9. ~~**C-1**~~ — 已修 (`741ddc9`)。
10. **D-5** — 一行指纹换成"条数 + 首尾 Id"或直接去掉去重（去重是为了防闪烁，别一刀切）。改之前先想清楚它保护的是什么。
11. **E-4** — 不是补个 `if` 的事，先定产品：接目录下钻，还是摘掉「文件夹」Tab。放最后。
12. 其余 13 条未核实的，**核实之前一律不许开修**。

## 今天已修（不在上表内）

- 启动 UI 偶发过曝：HDR 检测从固定 300ms 定时器改为 warmup 后事件驱动 —— `30f1dc1`
- 视频加载失败重置上次观看进度：新增 `m_fileLoaded` / `m_startTimeTicks` 守卫 —— `0d5aa59`

两者根子相同：**拿还没就绪的状态当权威数据**。这次审计正是照着这个形状去找的。
