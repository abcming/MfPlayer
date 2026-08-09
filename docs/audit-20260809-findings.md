# MfPlayer 全仓库审计 — 2026-08-09

四单只读审计（codex / blueprint 流程），分区互不重叠，每单自行选取维度。
共 **26 条** finding。

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

## 已核实（3 条）

### A-1 · ImageProvider 在 reader 线程无保护读取 `m_imageCache`
- **状态**：`已核实` — 实锤
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

## 未核实 · 单 1 · playback + mpv（2 条）

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| B-1 | `mpvcontroller.cpp:434`、`:347`、`.h:120` | 异步 volume/speed 写入拿"旧确认值"去重，会吞掉最终请求；`m_volume` 初值 80 与 libmpv 默认 100 不符，启动时 `setVolume(80)` 被判定为无需设置，随后被观察事件改成 100 并持久化 | 低 |
| B-2 | `playbackcontroller.cpp:276`、`:164`、`:257` | 只含 MediaSources 的临时对象被当成完整详情写回缓存：顶层缺 `RunTimeTicks` → `PlayedPercentage` 恒为 0、`Played` 恒 false，并提交缺 Id/Name 的详情对象 | 低 |
| B-3 | `playbackcontroller.cpp:58`、`:501`、`mpvcontroller.cpp:474` | 外挂字幕用 `sub-add` 异步排队后，同一同步槽内立刻 `fuzzySelectSubtitle()`，此时 track-list 还没有这些轨道；`tracksChanged` 也不会重新触发选择 | 低 |

> 单 1 备注：D3D11 渲染路径、Vulkan 图像所有权/销毁顺序、锁序与控制器 teardown 已读，无额外 finding。

## 未核实 · 单 2 · cache + network（4 条）

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| C-1 | `cachestore.cpp:385` | `clearImageCache()` 不清 `m_downloadQueue`、不取消 curl 请求/重试定时器/IO 池任务；旧任务完成后 `putImagePath()` 把刚清空的缓存重新写回磁盘+内存+SQLite。`DBWorker::putImagePath()` 更是直接 `Q_UNUSED(generation)` | 中 |
| C-2 | `dbworker.cpp:36` | `stop()` 设 flag + `quit()`，并不排空事件队列（与头文件注释声称的 "drains the event queue" 相反）；退出前最后一批 `putItems()` 丢失 | 低 |
| C-3 | `dbworker.cpp:115` | `putSeasons()` 事务内只做 `INSERT OR REPLACE`，不先删该 `series_id` 旧行 → DB 存的是历次季列表的并集；季被删除后幽灵季会残留到 TTL 过期 | 低 |
| C-4 | `cachestore.cpp:590` | `QFile::write()` 返回值未检查，`writeOk` 只看 `rename()`。短写/写失败后 rename 仍成功 → 截断文件被登记为有效缓存，且因 `m_imageCache` 已命中不会重新下载 | 低 |

> 单 2 备注：CurlEngine 完整读过（easy handle / slist / Task / 取消路径 / 回调销毁引擎），无达到门槛的问题。

## 未核实 · 单 3 · library / detail / server / settings / media / providers（9 条）

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| D-1 | `servermanager.cpp:89`、`:167`、`embyclient.cpp:198` | `m_librariesGeneration` 是共享"当前代"，`librariesFetched` 不带发起时的 generation 或服务器标识；连续切 A→B→C 时旧响应仍满足相等检查，旧库列表被当成 C 的发出 | 中 |
| D-2 | `embyclient.cpp:237`、`:250`、`detailmanager.cpp:257`、`:268` | `seasonsFetched`/`episodesFetched` 不带 `seriesId/seasonId`，DetailManager 用可变的当前 ID 写缓存 → S1 的集列表被写进 `(seriesId, S2)` 键下，持久污染 | 中 |
| D-3 | `embyclient.cpp:165`、`:185`、`:523`、`:539` | `postJson()`/`deleteJson()` 在 `!r.ok()` 时发 `networkError` 后**仍无条件调业务 callback** → 断网时收藏/已看谎报成功并污染本地状态 | 中 |
| D-4 | `librarybrowser.cpp:498`、`:504`、`:525` | `m_browseGeneration` 只被 `loadMore()` 捕获；首屏走 `onItemsFetched()` 只比 `parentId`，同库内切排序/筛选/Tab 时旧响应能通过 → 混合排序列表、重复或缺项 | 中 |
| D-5 | `mediamodel.cpp:62`、`librarybrowser.cpp:181`、`:500` | `size + firstId` 指纹碰撞：库中部新增一部影片时数量与首个 ID 都不变，网络首屏被跳过，分页却按服务器新顺序续拉 → 当前列表重复一项、缺一项 | 中 |
| D-6 | `librarybrowser.cpp:73`、`:408`、`:420` | 搜索防抖只停未触发的 timer，不失效已发出的请求；输入缩短后旧结果回填空白搜索页。人物响应仅靠布尔值路由，连续搜索会互相吞掉 | 低 |
| D-7 | `librarybrowser.cpp:329`、`embyclient.cpp:646`、`:504` | `browsePerson()` 的 `FetchParams` 不设 `parentId`，回来必然被 `parentId != m_currentLibraryId` 丢弃 → 人物浏览点了没反应（**注：老王看不到 QML，已自行降级；需结合单 4 一起判**） | 低 |
| D-8 | `credentialstore.cpp:35`、`servermanager.cpp:49` | `addServer()` 命中已有 URL+用户名时只更新凭证，不设 active 也不取消原 active；重新登录已保存的非活动服务器后，重启会恢复到错误的服务器 | 低 |
| D-9 | `settingsstore.cpp:28`、`:119`、`.h:38` | HDR 亮度/SDR 白点/seek 步长/窗口尺寸走 200ms 防抖，析构时不提交待写值 → 调整后 200ms 内 Alt+F4 丢失设置 | 低 |

> 单 3 备注：`updateCardField()` 的传播覆盖完整；similar/person/nextUp 的响应 ID 守卫有效；`MediaModel::removeItem()` 正确失效指纹并重建索引。

## 未核实 · 单 4 · ui/qml（7 条）

| # | 位置 | 摘要 | 老王定级 |
|---|---|---|---|
| E-1 | `DetailPage.qml:126` | 每个留在 StackView 里的 DetailPage 都连着全局 `Detail.seasonsChanged`，且不校验事件属于哪个剧集 → 打开剧集 B 会把后台 A 的季选择重置为 0，破坏"季选择状态保持" | 中 |
| E-2 | `PlayerPage.qml:60`、`:150` | `_versionSources` 只要 `itemData.MediaSources` 非空就采用，不校验它属于当前 `itemId`；`switchEpisode()` 不清 `itemData` → 切下一集后版本菜单仍是上一集的，选中会用错的 MediaSourceId 起播 | 中 |
| E-3 | `PlayerPage.qml:78`、`DetailPage.qml:81` | 两个 400ms Timer 在 `Component.onCompleted` 启动，退场时不停止也不检查是否仍是当前项。入场 350ms / 退场 350ms，约 360ms 时返回必定撞上 → 后台重新起播、或跨详情页污染共享模型 | 中 |
| E-4 | `LibraryGridView.qml:221`、`:227` | 点击分支只处理 Studio/Genre/Movie/Series/Episode；「文件夹」Tab 里真正的 Folder 项落到兜底分支，只调 `Detail.browseItem()`，既不进目录也不 push 详情 → 点了没反应 | 中 |
| E-5 | `BrowsePage.qml:1284`、`:1326` | UNC URL `file://NAS/share/x.mkv` 正则去掉 `file://` 后变成相对路径 `NAS/share/x.mkv`，丢掉 UNC 根前缀 → 从 `\\NAS\Videos` 播放/拖入失败 | 低 |
| E-6 | `PlayControlsRow.qml:190`、`:227` | 「下一集」进度的分子取 `nextEpisode.PlaybackPositionTicks`，分母却取 Series 的 `RunTimeTicks` → 空进度条或荒谬的剩余时间 | 低 |
| E-7 | `Main.qml:355`、`:395` | 登录失败的 `errorLabel` 只在下次点"连接"时隐藏，Dialog 的 `onOpened`/`onClosed` 都不重置 → 关掉再打开仍显示上一次的认证错误 | 低 |

> 单 4 备注：HDR 启动遮罩与 PQ overlay、CachedImage/ImageLoadQueue 的取消与并发槽归还、`reuseItems` delegate 的 required roles、主进度滑块的除零守卫与节流、TrackSelector、SettingsDialog、单例与 shader 数值路径已读，无其他发现。

---

## 建议的下一步顺序

1. **A-1** — 唯一的高危，而且要重做一个既定决策。先想清楚方案再动手，别直接加锁。
2. **D-3** — 断网时谎报成功，是"用户看到的和服务器上的不一致"，一眼可验。
3. **A-2** — 已核实，修法要和今天那个 `m_fileLoaded` 的改动放一起想（同属播放结算链）。
4. **D-1 / D-2 / D-4** — 三条同形状：**异步响应不带请求身份**。可以一起看，多半是同一个修法。
5. **E-1 / E-2 / E-3** — 三条同形状：**QML 侧不校验数据/事件属于当前页**。同上，一起看。
6. **A-3** — 实锤但只影响 OpenGL 后端，Windows 主路径碰不到，可以缓。
7. 其余低危按需。

## 今天已修（不在上表内）

- 启动 UI 偶发过曝：HDR 检测从固定 300ms 定时器改为 warmup 后事件驱动 —— `30f1dc1`
- 视频加载失败重置上次观看进度：新增 `m_fileLoaded` / `m_startTimeTicks` 守卫 —— `0d5aa59`

两者根子相同：**拿还没就绪的状态当权威数据**。这次审计正是照着这个形状去找的。
