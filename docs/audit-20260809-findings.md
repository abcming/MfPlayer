# MfPlayer 全仓库审计 — 2026-08-09

四单只读审计（codex / blueprint 流程），分区互不重叠，每单自行选取维度。
共 **26 条** finding。**26 条全部核实完毕**（2026-08-10）——
25 条成立、**1 条驳回**（D-7，描述的故障真实但整条路径没有调用方，是死代码不是 bug）。
**已修 10 条**：A-1、B-2、C-1、D-1、D-2、D-3、D-4、E-1、E-2、E-3
（第一梯队已全部落地，均通过编译；运行时验证待真机）。

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

## 首批核实（2026-08-10 上午）

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
- **状态**：`已修` — 2026-08-10，`246baaf`
- **修法**：endOfFile 处理器补 `if (!m_fileLoaded) return;`，与 onProgressTimer 同一道守卫
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
- **状态**：`已核实` · **处置**：**不修** — 只影响 OpenGL 后端。实际使用中默认是自动，同学也不会去动图形 API 设置 — 实锤
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
- **状态**：`已修` — 2026-08-10，`23fd85f`
- **修法**：librariesFetched 带 serverUrl + userId；m_librariesGeneration 五处赋值和声明一并删除 · **老王定级**：中 · **核实后**：中
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
- **状态**：`已修` — 2026-08-10，`44ade64`
- **修法**：signature 带上 seriesId / seasonId，槽里先比对再处理 · **老王定级**：中 · **核实后**：**中偏高**
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
- **状态**：`已修` — 2026-08-10，`0cc3b4f`
- **修法**：用 cacheKey 当请求身份 + dispatchFetch() 收口 10 个分发点 · **老王定级**：中 · **核实后**：中
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
- **状态**：`已修` — 2026-08-10，`60b4531`
- **修法**：seasonsChanged 带 seriesId，DetailPage 与 SeriesSection 两个槽都改成验归属 · **老王定级**：中 · **核实后**：中
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
- **状态**：`已修` — 2026-08-10，`a4dfb17`
- **修法**：StackView.onDeactivating 停 timer · **老王定级**：中 · **核实后**：中
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
- **状态**：`已修` — 2026-08-10，`741ddc9`（编译 + 实机验证通过）
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
- **状态**：`已修` — 2026-08-10，`64fc69c`（删掉整个 Tab）
- **决定**：当初照着 Emby Web 加的，实际用处不大。接目录下钻不值当，直接摘掉 · **老王定级**：中 · **核实后**：中
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

## 第二批核实（2026-08-10）· 单 1 · playback + mpv

### B-1 · 音量去重拿"旧确认值"比，且初值与 libmpv 默认不符
- **状态**：`已修` — 2026-08-10，`fe29479`
- **修法**：去重改比 m_requestedVolume / m_requestedSpeed，哨兵初值 -1；m_volume 初值对齐 libmpv 的 100 · **老王定级**：低 · **核实后**：**低偏中**
- **位置**：`platform/rendering/mpv/mpvcontroller.cpp:434`、`:527`、`.h:120`、
  `core/playback/playbackcontroller.cpp:19`、`:25`

```cpp
void MpvController::setVolume(int vol) {
  if (!m_mpv || vol == m_volume) return;   // ← m_volume 是"上次观察事件的值"
  ...
}
int m_volume = 80;                          // .h:120，libmpv 默认是 100
```

`m_volume` 唯一的写入点是 `:527`（mpv 属性观察事件），**不是"上次请求值"**。两个后果：

**① 启动时吞掉恢复值（每次都发生，只要设置里存的是 80）**

```
m_volume = 80                                    ← C++ 初值
setVolume(m_settings->volume())  // 恰好 80      → 80 == 80，直接 return
libmpv 保持默认 100
观察事件回来 → m_volume = 100
playbackcontroller.cpp:25 → m_settings->setVolume(100)   ← 用户的 80 被覆盖写死
```

用户设 80、退出、重启 → **音量自己跳到 100 并被持久化**。深夜戴耳机时这一下不好受。
（`m_speed` 初值 1.0 与 libmpv 默认一致，没有这一支。）

**② 拖动滑块时吞掉最终请求**

`PlayerControls.qml:48` 的 `onMoved` 触发频率很高：

```
拖到 50 → 发请求；观察事件回来 m_volume = 50
拖到 60 → 发请求；事件还没回，m_volume 仍是 50
拖回 50 → 50 == m_volume(50) → 吞掉
60 的事件回来 → m_volume = 60，UI 显示 60，用户手停在 50
```

### B-2 · 只含 MediaSources 的残缺对象被当完整详情写回缓存
- **状态**：`已修` — 2026-08-10，`2b8d052`
- **修法**：MediaSources 单独存进 m_currentMediaSources，m_currentItemDetail 恢复"要么完整要么空"的不变量 · **老王定级**：低 · **核实后**：**中偏高**
- **位置**：`core/playback/playbackcontroller.cpp:139`、`:164`、`:276-286`、`core/cache/cachestore.cpp:229`

```cpp
m_currentItemDetail = m_cache->getItemDetail(itemId);   // :139 详情未预缓存 → {}
...
m_currentItemDetail["MediaSources"] = mediaSources;     // :164 变成"只有 MediaSources"
```

这个残缺状态**代码自己知道**——`:79-80` 的注释白纸黑字写着
「详情未预缓存时 `m_currentItemDetail` 只有回填的 MediaSources, 没有 Id 字段」，
并在字幕那条路上绕开了它。但 `updateCachedProgress()` 没绕：

```cpp
QJsonObject cached = m_currentItemDetail;                 // 非空（有 MediaSources）
if (cached.isEmpty()) cached = m_cache->getItemDetail(itemId);   // ← 走不到
double totalTicks = cached["RunTimeTicks"].toDouble();    // 缺字段 → 0
ud["PlayedPercentage"] = 0;  ud["Played"] = false;        // 恒定
m_cache->putItemDetail(itemId, cached);                   // ← 写回
```

`putItemDetail()` 是**整体覆盖**（`m_detailCache[itemId] = detail` + DBWorker 存整串 JSON），
不是合并。所以这一下把内存和 SQLite 里可能存在的完整详情，换成一个没有
Id / Name / RunTimeTicks 的壳。

触发路径是**从剧集页直接点某一集播放**——代码注释里举的正是这个例子
（"episodes played from a series page"）。这不是边角情况，是日常操作。

### B-3 · 外挂字幕 `sub-add` 还在排队，就去 track-list 里模糊匹配
- **状态**：`已核实` · **处置**：**不修** — 外挂字幕一般都有名字，模糊匹配用不上。改法要动 tracksChanged 时序，风险大于收益 · **老王定级**：低 · **核实后**：低
- **位置**：`core/playback/playbackcontroller.cpp:58`、`:93`、`:501`、`platform/rendering/mpv/mpvcontroller.cpp:493`

`fileLoaded` 的槽里先循环 `addSubtitleFile()` 把所有外挂字幕加进去，
而 `addSubtitleFile()` 结尾是 **`mpv_command_async`**（`:493`）——命令只是排进队列。
同一个槽紧接着就调 `fuzzySelectSubtitle()`（`:93`），它读的是 `m_mpv->tracks()`，
**此刻那些外挂轨道还没出现在 track-list 里**。

`tracksChanged` 只是被转发给 QML（`:52`），不会重新触发选择。
所以设了偏好字幕语言的用户，模糊匹配只能在内嵌轨道里挑，外挂字幕永远轮不上。

---

## 第二批核实（2026-08-10）· 单 2 · cache + network

### C-2 · `stop()` 不排空队列 —— 而且是主动丢弃
- **状态**：`已核实` · **处置**：**不修** — 退出前丢最后一批缓存写入，数据不会错，下次启动重拉 · **老王定级**：低 · **核实后**：低
- **位置**：`core/cache/dbworker.cpp:37`、`.h:26-27`

头文件注释声称 stop 会 "drains the event queue"，实现是：

```cpp
void DBWorker::stop() {
    m_stopFlag = true;
    m_thread.quit();          // 处理完当前事件就退，不排空
    ...
}
```

比"不排空"更进一步：每个 slot 开头都是 `if (m_stopFlag) return;`，
所以队列里剩下的事件**即使被处理也会被主动丢弃**。注释是假的。

后果只是退出前最后一批写入没落盘，下次启动重新从网络拉。数据不会错。**低**。

### C-3 · `putSeasons()` 不删旧行，DB 里存的是历次季列表的并集
- **状态**：`已修` — 2026-08-10，`492e1d9`
- **修法**：`putSeasons` 事务开头补 `DELETE FROM seasons WHERE series_id = ?` · **老王定级**：低 · **核实后**：低
- **位置**：`core/cache/dbworker.cpp:111`、`core/cache/cachestore.cpp:93-96`

表结构是 `PRIMARY KEY (series_id, season_id)`（`cachestore.cpp:96`），
`putSeasons()` 事务里只有 `INSERT OR REPLACE`，**没有
`DELETE FROM seasons WHERE series_id = ?`**。

服务器上删掉第 3 季 → 新数据只含第 1、2 季 → 第 3 季那行原地不动 →
`getSeasons()` 读出来是并集，UI 上多一个点进去空空如也的幽灵季。

幽灵行的 `fetched_at` 不会被刷新，所以 3 天 TTL（`kCacheExpirySeconds`，
`dbworker.cpp:252`）到期会自己消失。**上限 3 天，能自愈**，所以是低。

> `episodes` 表把整季存成一行 JSON（同样的复合主键 + `data` 字段），
> 整体替换，没有这个问题。只有 `seasons` 是逐行存的。

### C-4 · `QFile::write()` 返回值不查，截断文件会被登记为有效缓存
- **状态**：`已修` — 2026-08-10，`492e1d9`
- **修法**：检查 `write()` 返回值与 `flush()`；短写时删掉 tmp 文件 · **老王定级**：低 · **核实后**：低
- **位置**：`core/cache/cachestore.cpp:583-590`

```cpp
QFile f(tmpPath);
if (f.open(QIODevice::WriteOnly)) {
    f.write(*dataPtr);              // ← 返回值丢弃
    f.close();                      // ← 返回值丢弃（flush 失败在这里体现）
    QFile::remove(savePath);
    writeOk = QFile::rename(tmpPath, savePath);   // writeOk 只反映 rename
}
```

前面的 `QImageReader::canRead()` 校验的是**内存里的 dataPtr**，不是落盘后的文件，
所以拦不住短写。磁盘满 / IO 错误 → 文件截断 → rename 照样成功 → `writeOk = true`
→ `putImagePath()` 登记为有效缓存 → 显示半张图，且因 `providerUrl()` 命中不会重下。

触发要磁盘满或 IO 故障，**低**。但和 C-1 的幽灵条目是同一类："缓存表说有，实际不可用"。

---

## 第二批核实（2026-08-10）· 单 3 · library / server / settings

### D-6 · 搜索防抖只停 timer 不失效请求；人物结果还会串台
- **状态**：`已修` — 2026-08-10，`93c5e1d`
- **修法**：搜索加 m_searchGeneration；人物列表改回调式，personsFetched 信号与两个布尔删除 · **老王定级**：低 · **核实后**：**中**
- **位置**：`core/library/librarybrowser.cpp:408`、`:420-431`、`:73-80`

**① 旧结果回填空白搜索页**

```cpp
void LibraryBrowser::search(const QString &term) {
    if (term.length() < 2) {
        m_searchDebounceTimer->stop();   // 只停还没触发的
        m_searchMoviesModel->clear();
        ...
```

`executeSearch()` 发出的 `searchItems()` 是回调式、**无 generation 守卫**。
输入"钢铁侠"→ 请求发出 → 用户删到"钢"→ 停 timer + 清空模型 → 旧请求回来
`setItems()` 照填。搜索框空着，结果列表却有内容。

**② 人物结果串台 —— 比原报告严重**

`m_fetchingFavPersons` / `m_fetchingSearchPersons` 是两个**布尔**，共用同一个
`personsFetched` 信号：

```cpp
if (m_fetchingFavPersons)        { m_fetchingFavPersons = false;   m_favPeopleModel->setItems(items); }
else if (m_fetchingSearchPersons){ m_fetchingSearchPersons = false; m_searchPeopleModel->setItems(items); }
```

收藏页和搜索同时在飞时，**先回来的那个响应会被判给 fav 分支** ——
搜索出来的人物被填进"收藏的人物"列表。连续两次搜索则是后一次的结果被整个丢弃
（两个布尔都已置 false，两个分支都不成立）。

### D-7 · `browsePerson()` 整条路径没有调用方 —— **驳回**
- **状态**：`驳回`（bug 定性）· `已清理` — 2026-08-10，`589eb35`（当死代码删除）
- **老王定级**：低 · **核实后**：**不是 bug，是死代码**
- **位置**：`core/library/librarybrowser.cpp:329`、`.h:124`

原报告说「人物浏览点了没反应」。代码层面的事实成立：
`browsePerson()` 的 `FetchParams` 只设 `personIds`、不设 `parentId`，
走无回调版 `fetchItemsFiltered()`（`embyclient.cpp:619`），
末尾 `emit itemsFetched(items, p.parentId /* 空 */, ...)`，
必然被 `onItemsFetched()` 的 `if (parentId != m_currentLibraryId) return;` 丢弃。

**但用户撞不上这个故障 —— 全仓库没有任何地方调用 `browsePerson()`。**

```
grep -rn 'browsePerson' --include=*.qml .      → 0 条
grep -rn 'personBrowseStarted' ...             → 只有声明和 emit，0 个 connect
```

实际的人物浏览走的是另一条路：`LibraryGridView` 的兜底分支 → `Detail.browseItem()`
→ `DetailManager` 里的 `kTypePerson` 分支 → `fetchPersonFilms()`。那条路是通的。

所以 `browsePerson()` + `personBrowseStarted` 是**被替换后没删的旧路径**。
按 bug 处理是错的定性；要处理就当死代码删，和 `cachedImageUrl()` 一个性质。

### D-8 · 重登已保存的服务器不改 `is_active`，重启会回到旧服务器
- **状态**：`已修` — 2026-08-10，`279aef9`
- **修法**：ServerManager 登录成功后调 setActiveServer()（策略留在 ServerManager，不塞进存储层） · **老王定级**：低 · **核实后**：**中**
- **位置**：`core/server/credentialstore.cpp:35-46`、`:67`、`core/server/servermanager.cpp:132`

`addServer()` 命中已有 URL+用户名时，只 UPDATE 凭证和 `last_used`：

```cpp
u.prepare("UPDATE servers SET password = ?, token = ?, user_id = ?, "
          "last_used = datetime('now') WHERE id = ?");   // ← 不设 is_active
```

新建分支才有 `UPDATE servers SET is_active = 0` + `INSERT ... is_active 1`。

**关键在于恢复走哪个字段**：`restoreSession()`（`servermanager.cpp:132`）
第一步是 `m_creds->getActiveServer()`，读的是 **`is_active`**，
不是 `getServers()` 那条 `ORDER BY last_used DESC`。

所以：A 是 active → 用户登录已保存的 B → B 只更新了凭证和 last_used →
重启 → `getActiveServer()` 仍返回 **A**。用户明确切过服务器，重启却回到旧的。

### D-9 · 防抖设置在析构时不提交
- **状态**：`已修` — 2026-08-10，`492e1d9`
- **修法**：新增析构，用 `invokeMethod(t, "timeout")` 兑现待写值（不重抄 key） · **老王定级**：低 · **核实后**：低
- **位置**：`core/settings/settingsstore.cpp:29-40`、`:119`、`core/settings/settingsstore.h`

HDR 亮度 / SDR 白点等走 200ms `singleShot` timer，值先存在成员变量里，
timer 触发才 `m_settings.setValue()`。**`SettingsStore` 没有自定义析构函数**
（`grep '~SettingsStore'` 无结果），所以待写值根本没进过 QSettings，
QSettings 自己的 sync 也救不了。

调完 HDR 亮度 200ms 内 Alt+F4 → 这次调整丢失。窗口很窄，**低**。

---

## 第二批核实（2026-08-10）· 单 4 · ui/qml

### E-5 · UNC 路径被正则吃掉根前缀
- **状态**：`已核实` · **老王定级**：低 · **核实后**：低
- **位置**：`ui/qml/pages/BrowsePage.qml:1284`、`:1326`

```qml
var path = selectedFile.toString().replace(/^file:\/{2,3}/, "")
```

UNC 共享的 URL 形式是 `file://NAS/share/x.mkv` —— `//NAS` 是 **authority**，不是路径。
正则把 `file://` 整个吃掉后剩下 `NAS/share/x.mkv`，UNC 根前缀 `\\` 没了，
变成一个相对路径。从 `\\NAS\Videos` 选片或拖入都会失败。

本地盘路径（`file:///C:/...`，三斜杠）不受影响 —— 正则的 `{2,3}` 正好吃掉三个。
两处（文件对话框、拖放）都是同一行代码。

### E-6 · 「下一集」进度条分子分母不是同一个东西
- **状态**：`已修` — 2026-08-10，`492e1d9`
- **修法**：C++ 侧给 nextEpisode 带上 `RunTimeTicks`，QML 两处分母一起换 · **老王定级**：低 · **核实后**：低
- **位置**：`ui/qml/components/PlayControlsRow.qml:190-192`、`:229-230`

```qml
property double pct: ne && root.itemData.RunTimeTicks > 0
    ? Math.min((ne.PlaybackPositionTicks || 0) / root.itemData.RunTimeTicks * 100, 100)
    : 0
```

分子是**下一集的**播放位置，分母是 `root.itemData` 的时长 —— 而「下一集」只在
**剧集（Series）详情页**出现，`itemData` 就是 Series。

Emby 对 Series 通常不返回 `RunTimeTicks` → `> 0` 为假 → `pct` 恒 0 → 空进度条。
若某些配置返回了总时长，得到的是"单集进度 ÷ 全剧总长"，一个荒谬的小比例。
`:229-230` 的剩余时间标签用同一对数值，同样错。

### E-7 · 登录错误只在下次点"连接"时才消失
- **状态**：`已修` — 2026-08-10，`492e1d9`
- **修法**：Dialog 的 `onOpened` 里重置 errorLabel · **老王定级**：低 · **核实后**：低
- **位置**：`ui/qml/pages/Main.qml:355`、`:387`、`:399-400`

`errorLabel` 全文件只有 4 处引用：声明（`:355`，`visible: false`）、
连接按钮的 `onClicked` 里 `visible = false`（`:387`）、
`onPlayError` 里置文本并 `visible = true`（`:399-400`）。

Dialog 没有 `onOpened` / `onClosed` 重置。所以认证失败后关掉登录框、
过一会儿再打开，上一次的红字还在那儿。

---

## 建议的下一步顺序

**已修 21 条**：A-1 · A-2 · B-1 · B-2 · C-1 · C-3 · C-4 · D-1 · D-2 · D-3 · D-4 · D-5 · D-6 · D-8 · D-9 · E-1 · E-2 · E-3 · E-4 · E-6 · E-7
**驳回并清理 1 条**：D-7（死代码删除）
**明确不修 3 条**：B-3 · C-2 · A-3（理由见各条）

**这份审计到此收口 —— 26 条全部有结论。**
**驳回 1 条**：D-7（当死代码处理，不当 bug 修）。
剩 5 条按下面的顺序走 —— 排序依据是「危害 × 用户撞上的频率 ÷ 修复成本」，不是定级高低。

### 第一梯队 · 已全部落地（2026-08-10）

三条根因，六个提交：

| 根因 | 条目 | 修法 |
|---|---|---|
| 半截对象被当完整数据用 | B-2 | MediaSources 单独存，`m_currentItemDetail` 恢复"要么完整要么空" |
| 异步响应不带请求身份 | D-2 / D-4 / D-1 | 响应带上 id / cacheKey / server+user，回调里比对 |
| QML 不校验事件归属 | E-1 / E-3 | 信号带 seriesId；入场 timer 在 `onDeactivating` 停掉 |

修的过程里顺手清掉两处死代码（`m_librariesGeneration` 的五处赋值、
`cachedImageUrl()`），并给 LibraryBrowser 加了 `dispatchFetch()` 收口 ——
**新增分发点必须走它**，绕过去就等于没有守卫。

> 全部通过编译，**运行时行为尚未在真机验证**。要看的几处：
> 切服务器后库列表不串、切 tab/换排序时列表不混批、详情页选好的季不被别的页重置、
> 快速 push→pop 详情页/播放器不留后台动作、从剧集页直接播某集后详情缓存仍完整。

### 第二梯队 · 已全部落地（2026-08-10）

| 条目 | 修法 | 提交 |
|---|---|---|
| D-6 | 搜索加代守卫；人物列表改回调式，消灭共用信号 | `93c5e1d` |
| D-8 | 登录成功后 `setActiveServer()` | `279aef9` |
| B-1 | 去重比**请求值**不比确认值，哨兵初值 | `fe29479` |
| D-5 | 指纹换成全量 Id 滚动哈希 | `3e8876c` |
| A-2 | `endOfFile` 补 `!m_fileLoaded` 守卫 | `246baaf` |

修的过程里又挖出三个零调用方的死代码：`EmbyClient::fetchPersons()`、
`DBWorker::addServer()` / `setActiveServer()`（与 CredentialStore 重复实现）。
连同之前的 `fetchItems()` / `fetchSuggestions()` / `browsePerson()`，一并记在下面。

> 真机要看：搜索时快速删字不留残留结果、收藏页和搜索的人物不串、重登已保存
> 服务器后重启回到它、音量设 80 重启不跳 100、库里新增一部后列表不重复不缺项、
> 切集时上一集刚好播完不丢新集进度。

### 原第二梯队清单（留档）

4. **D-6** — 人物结果串台那一支优先（搜索结果填进"收藏的人物"）。
   两个布尔换成请求 id / generation。搜索请求本身也该带上守卫。
5. **D-8** — `addServer()` 命中已有记录的分支补上 `is_active` 的置位与互斥。改动很小。
6. **B-1** — 去重改成比"上次**请求**值"而非"上次**确认**值"，
   并把 `m_volume` 初值对齐 libmpv 默认（100），或者干脆用哨兵值 -1 表示"还没同步过"。
7. **D-5** — 指纹换成"条数 + 首尾 Id"或加一个内容摘要。
   **改之前先想清楚它保护的是什么** —— 它是为了首屏缓存预显示不闪烁，不能一刀切掉。
8. **A-2** — 修法要和 `m_fileLoaded` 的改动放一起想（同属播放结算链）。

### 第三梯队 · 已全部落地（2026-08-10，`492e1d9`）

五条打包一次提交。**E-6 不是一行** —— nextEpisode 根本没带 RunTimeTicks，
得先在 C++ 侧补上，QML 才有正确的分母可用。这是"看起来一行"和"实际改动"
的一次典型偏差，下次估工作量记得先确认数据到底存不存在。

### 原第三梯队清单（留档）

9. **E-7** — Dialog 的 `onOpened` 里 `errorLabel.visible = false`。1 行。
10. **C-3** — `putSeasons()` 事务开头加 `DELETE FROM seasons WHERE series_id = ?`。2 行。
11. **C-4** — 检查 `f.write()` 返回值是否等于数据长度，以及 `f.close()` 的返回值。2 行。
12. **D-9** — 给 `SettingsStore` 加析构，把还在等的 timer 立即触发一次。几行。
13. **E-6** — 分母换成 `ne.RunTimeTicks`。1 行。
14. **E-5** — UNC 前缀还原：`file://host/...` 要还原成 `\\host\...`。
    注意别把 `file:///C:/...` 一起改坏了。

### 缓办（已定案，不再排期）

| 条目 | 处置 | 理由 |
|---|---|---|
| B-3 | 不修 | 外挂字幕一般都带名字，模糊匹配用不上；改法要动 `tracksChanged` 时序，风险大于收益 |
| C-2 | 不修 | 退出前丢最后一批缓存写入，数据不会错，下次启动重拉 |
| A-3 | 不修 | 只影响 OpenGL 后端，默认是自动选择，实际碰不到 |
| E-4 | 已删 | 「文件夹」Tab 整个摘掉（`64fc69c`） |
| D-7 | 已清 | 当死代码删除（`589eb35`） |

### 顺带记下的、不在 26 条里的

- **`networkError` 是死信号**（3 处 emit、0 个 connect）。D-3 修完后，
  网络失败对用户就是"UI 不动"。要不要给可见提示是一个**产品决定**，不是 bug 修复。
- **`getJson()` 也无条件调 cb**（`embyclient.cpp:153`），和 D-3 同形状但**不能同法修** ——
  它的 cb 还负责结束 loading 状态，直接 `return` 会让转圈停不下来。要单独设计。
- **DBWorker 五个 `generation` 形参全是 `Q_UNUSED`**。核实下来**它们本来就不需要**
  （QueuedConnection + 串行处理保证 clear 之前投递的写入必在 DELETE 之前执行）。
  是五个死参数，不是五个漏洞。要清就当签名清理做。

## 今天已修（不在上表内）

- 启动 UI 偶发过曝：HDR 检测从固定 300ms 定时器改为 warmup 后事件驱动 —— `30f1dc1`
- 视频加载失败重置上次观看进度：新增 `m_fileLoaded` / `m_startTimeTicks` 守卫 —— `0d5aa59`

两者根子相同：**拿还没就绪的状态当权威数据**。这次审计正是照着这个形状去找的。
