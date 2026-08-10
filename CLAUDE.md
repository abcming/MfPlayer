# MfPlayer — AI 开发速查

> **技术栈**: Qt 6.11 QML + C++23, libmpv (gpu-next fork), Emby REST API, libcurl
> **最后更新**: 2026-06-23

---

## 项目地图

```
common/         → 纯常量，零依赖
core/network/   → CurlEngine (libcurl multi 接口，主线程非阻塞)
core/providers/ → EmbyClient (REST 全覆盖，上帝类，先别拆，每次会话生成独立 DeviceId)
core/cache/     → CacheStore (内存缓存, 主线程) + DBWorker (SQLite, 独立线程) + ImageCacheProvider
core/media/     → MediaModel (QAbstractListModel, 17 roles, O(1) lookup)
core/settings/  → SettingsStore (QSettings wrapper, 27 属性, 也不拆)
core/server/    → ServerManager (多服务器管理, 持有其他所有 core 对象) + CredentialStore
core/playback/  → PlaybackController (播放编排: Emby→mpv 桥接)
core/library/   → LibraryBrowser (15 个 MediaModel, 浏览/搜索/收藏/分页)
core/detail/    → DetailManager (详情页数据, 6 个 MediaModel, 支持剧集/季选择状态保持)
platform/rendering/mpv/
    mpvcontroller → libmpv C API 包装, 渲染上下文管理
    mpvrenderitem → QQuickItem + QSGRenderNode, 三后端渲染 (D3D11/Vk/GL)
ui/qml/
    theme/      → Theme/Str/Nav (Singleton)
    pages/      → Main/Browse/Detail/Player
    views/      → HomeView/LibraryGridView/PlayerControls/SuggestionsView
    components/ → HdrPqOverlay/CachedImage/RoundedImage/TrackSelector/SeriesSection...
    shaders/    → hdr_pq.frag/hdr_pq.vert (sRGB→PQ), roundedmask.frag
```

## 运行时对象树 (main.cpp 构造 & 所有权)

```
ServerManager (&app)          ← 根所有者
├── SettingsStore (this)
├── EmbyClient (this)         ← 被 4 个类持裸指针, 但唯一 owner 是 ServerManager
│   └── CurlEngine (unique_ptr)
├── CacheStore (this)
│   ├── CurlEngine (unique_ptr)
│   ├── DBWorker (独立 QThread, SQL 写入+维护)
│   └── std::thread xN (图片下载验证, dtor join)
└── CredentialStore (this)

PlaybackController (&app)
├── MpvController (this)      ← 持有 mpv_handle + mpv_render_context
└── QTimer x2

LibraryBrowser (&app)         ← 持有 EmbyClient* + CacheStore* (裸指针, 不拥有)
└── MediaModel x15

DetailManager (&app)          ← 同上, 裸指针
└── MediaModel x6
```

**关键规则**: EmbyClient 和 CacheStore 的裸指针在 4 个类间共享，依赖 Qt parent-child 生命周期保证安全。不要单独 delete 它们。

## QML ↔ C++ 桥接

9 个 context property (setContextProperty):
- `Playback` (PlaybackController) — 嵌套 `Playback.mpv` (MpvController, 仅诊断/渲染直连, 业务走 Playback 转发)
- `Library` (LibraryBrowser) — 30 个 Q_PROPERTY, 15 个 MediaModel
- `Detail` (DetailManager) — 7 个 Q_PROPERTY, 6 个 MediaModel
- `Server` (ServerManager) — 嵌套 `Server.settings` + `Server.emby` + `Server.cache`
- `_appDir` — 应用程序可执行文件路径 (字体等相对路径解析)
- `_appVersion` — "MfPlayer vX.Y.Z"
- `_qtVersion` — Qt 编译版本号字符串
- `_startupFile` — "Open with" 传入的文件路径
- `_hdrActive` (bool) — warmup 全屏切换后的首个 frameSwapped 时动态设置，swapchain 预热后才能检测

qmlRegisterType: 只有 `MpvRenderItem`。

ImageProvider: `image://imgcache/<hash>/<base64url(路径)>` → ImageCacheProvider。
**路径由主线程的 `CacheStore::providerUrl()` 解析好带进 URL，provider 不持有也不访问
CacheStore** — `requestImageResponse()` 跑在 Qt 的 pixmap reader 线程 (2026-08 修，见下)。

## 播放启动链路 (user click → frame on screen)

```
QML: Playback.playItem(id, ticks)
  → PlaybackController: cache lookup → ++m_playGeneration (防竞态)
  → EmbyClient::fetchPlaybackInfo (网络, CurlEngine 主线程驱动)
  → EmbyClient::reportPlaybackStart (网络, fire-and-forget, 与 loadfile 并行不阻塞起播)
  → MpvController::play(url) → mpv_command_async("loadfile")
  → [mpv 内部: 下载→解封装→解码]
  → wakeup() → QueuedConnection → onMpvEvents() [主线程]
  → emit fileLoaded, positionChanged...
  → render update callback → emit renderUpdateNeeded → MpvRenderItem::update()
  → VideoRenderNode::prepare() + render() [Qt 渲染线程]
  → mpv_render_context_render() 直接写 swapchain
```

## 线程安全速查

| 线程 | 干什么 | 安全边界 |
|------|--------|---------|
| **主线程** | QML, CurlEngine::tick(), 内存缓存(HashMap, 无锁), 信号槽 | 默认运行域 |
| **DB Worker** (1 线程) | 所有 SQLite 写入+维护 (putItems, expire, clear, CredentialStore) | QueuedConnection slot 调用, 信号回主线程 |
| **I/O Pool** (2-4 线程) | 文件 stat, QSettings 延迟写, 目录枚举 | QThreadPool QRunnable, QMetaObject::invoke 回主线程 |
| **CPU Pool** (=CPU 核数) | 图片解码 (ImageCacheResponse::process) | QThreadPool::globalInstance() |
| **Qt 渲染线程** | VideoRenderNode::prepare/render | s_renderMutex → s_stateMutex 保护 |
| **mpv 内部线程** | demux/decode/render | 仅通过 QueuedConnection 投递到主线程 |
| **libcurl 内部** | DNS/TLS | 回调在 CurlEngine::tick() 中主线程同步执行 |

**铁律**:
- MpvController 的所有成员变量 **只能主线程访问**。唯一的例外是 `m_hasVideo` (std::atomic)。
- CurlEngine 全主线程运行，curl_multi_perform 非阻塞，不要在回调里做重活。
- 锁序: s_renderMutex → s_stateMutex。两个路径 (render() 和 ~MpvController()) 都遵守。
- DB Worker 不直接访问 CacheStore 内存缓存 — 通过 QueuedConnection 信号回主线程更新。
- 内存缓存 (m_itemsCache, m_detailCache, m_imageCache) **主线程独占**，无锁访问。
- 所有跨线程通信必须经过主线程: Worker → QueuedConnection → 主线程回调。Worker 间不直连。

## HDR 管线

```
视频帧 (mpv) → libplacebo → target-trc=pq → HDR10 swapchain (R10G10B10A2) → 显示器
QML UI (sRGB) → RGBA16F FBO → hdr_pq.vert + hdr_pq.frag (sRGB→Rec.709→Rec.2020→PQ) → 同一 swapchain
```

- mpv 和 UI 共用同一个 HDR10 swapchain，两条并行管线
- `_hdrActive` 由事件驱动: warmup 的 showNormal() 之后首个 frameSwapped 才检测 (2026-08 修)。
  **别改回固定定时器** — showFullScreen/showNormal 会重建 swapchain, 定时到点时可能读到
  未协商完的格式, 误判 SDR。而 Main.qml 的遮罩只看"值定义了没", 一旦误判就是掀开遮罩 +
  PQ shader 不开 = sRGB UI 直送 HDR10 swapchain 白爆 (启动偶发过曝的根因)。
  2s 兜底定时器是防"永不出帧 → 遮罩永久黑屏"的死锁, 也别删
- 启动时 Main.qml 有黑色遮罩层 (`hdrStartupCover`) 防过曝，检测完成后淡出
- SDR 系统自动回退: `_hdrActive=false` → `layer.enabled=false` → 零 shader 开销
- hdr_pq.frag 做完整 unpremultiply→变换→re-premultiply (2026-07 定稿): premultiplied 直接过
  PQ 曲线会让 AA 边缘变暗 (文字发虚)、半透明面板偏红。RGBA16F layer 下除法无量化噪点, 别回退。
  文字渲染保持 Qt 默认 — Curve (无 hinting 发糊) 和 Native+灰度AA (发虚) 都试过且实测否决,
  残余的一点点子像素红边是接受的取舍, 别再"修"
- 字幕不走 blend-subtitles (2026-07 拆除): 输出分辨率直接合成才清晰, sub-pos 才能独立移动;
  HDR 下字幕亮度由 gpu-next overlay 色彩管理保证, 别改回 blend-subtitles=video
- hdr_pq.vert 负责处理全屏三角形顶点变换，与 frag 配对使用
- `compile_hdr_shaders.bat` 用于 Windows 下着色器预编译

## 异步安全机制

| 机制 | 用途 | 位置 |
|------|------|------|
| `m_searchDebounceTimer` + `m_pendingSearchTerm` | 300ms 搜索防抖，批量按键合并为一次 API 调用 | LibraryBrowser |
| 代计数器 generation | 取消过期异步回调 | PlaybackController (`m_playGeneration`), ServerManager, CacheStore (`m_writeGeneration`), LibraryBrowser (`m_browseGeneration`, 丢弃列表切换后飞行中的旧分页批次) |
| QPointer 守卫 | 回调前检查对象存活 | EmbyClient 全部 50+ 回调, CacheStore, CurlEngine, DBWorker |
| `m_reauthing` | 防递归 re-auth | ServerManager |
| `m_pendingDownloads` set | 防重复下载 | CacheStore |

## DB Worker 通信模式

所有 SQLite 写入操作统一走三段式异步模式:

```
主线程: cacheStore->putItems(parentId, items)
  → 更新内存缓存 (同步, 主线程)
  → QMetaObject::invokeMethod(m_dbWorker, "putItems", QueuedConnection, args...)
  → 立即返回

DB Worker 线程: DBWorker::putItems(args...)
  → SQLite INSERT 事务
  → emit itemsWritten(parentId)  (信号, 自动 QueuedConnection)

主线程: (无需处理 — 内存缓存已在调用时更新)
```

- **写入**: 主线程更新内存缓存 → 投递 DB Worker → 完成
- **读取**: 主线程查内存缓存 (无锁) → 命中返回; 未命中查主线程 SQLite 只读连接
- **启动**: DB Worker 异步打开数据库, UI 在初始化完成前就显示
- **WAL 模式**: CacheStore 初始化时设 `PRAGMA journal_mode=WAL`，防止主线程读 + DBWorker 写并发时产生 SQLITE_BUSY
- `providerUrl()` 不做 stat() — 信任 loadImageCache() 已验证的文件

## I/O Pool 用法

```cpp
#include "core/io_pool.h"
ioPool().start([guard, ...]() {
    // 文件 I/O 在 I/O 池线程执行
    QMetaObject::invokeMethod(receiver, [guard, result]() {
        if (!guard) return;
        // 回主线程处理结果
    }, Qt::QueuedConnection);
});
```

## 关键常量和技巧

- `m_playGeneration` — 每次 play/stop 自增，回调里检查是否过期
- `m_writeGeneration` — CacheStore 的延迟 SQL 写入用，clearAll() 时自增取消旧写入
- 媒体缓存: 内存 HashMap + SQLite 双层，内存优先，过期数据留作 fallback。SQL 写入走 DB Worker
- 图片加载: CachedImage → CacheStore::providerUrl (内存缓存查命中 + 主线程解析路径, 无 stat) → ImageLoadQueue (max 3 concurrent) → ImageCacheProvider (reader 线程, 只解码)
- RoundedImage: Stretch fill + roundedmask.frag shader 做 GPU 圆角, 不用 OpacityMask (无额外 FBO)
- Icon: 23 个 MFIcon_* 全部常驻, visible 切换, 不用 Loader (避免切换延迟)
- Flickable: 全部 `interactive: false`, 用 WheelHandler + NumberAnimation 模拟滚动 (统一手感)
- 滑块防抖: hdrPeakBrightness/sdrWhiteNits/seekStep/windowSize 使用 200ms QTimer 防抖, 避免每次拖动像素写 QSettings
- MediaModel::fromJson 的 BackdropImageTags 只解析一次，复用结果（减少 O(N) 次 QJsonObject key lookup）
- langCodeToName 用 static QHash 代替 30+ if/else 链（O(1) 替代 O(N)）
- m_itemsCache 有 LRU 上限 (200 parent folders)，避免数万文件夹浏览后内存膨胀到 192MB+。
  入口统一 cacheItemsInMemory() — 绕过它直接写 m_itemsCache 会让 LRU 表失配, putItems 走 QList::move(-1) UB
- items 缓存 = items_json 表整段 JSON (键 FetchParams::cacheKey(), 写读必须共用该方法拼键)。
  切库/进库 (browseLibrary / setLibraryTab TabDefault) 先 getItems 预显示, 网络到达后 setItems 覆盖 —
  数据未变时 MediaModel 指纹跳过 reset (零闪烁)。别改回逐行列存: 缩减字段与网络数据指纹相同但内容不同,
  会被指纹挡住导致进度/收藏标缺失 (2026-07 复活该路径时的教训)
- ImageCacheProvider LRU 500→200，省 ~234MB
- CastAndCrewRow filteredModel 收集满 20 人提前 break
- sortByIndexNumber 预提取 key 代替 comparator 中 toObject()
- 写入操作全部走 DB Worker；读取走主线程无锁内存缓存
- QML delegate 用 `required property` + `lazyLoad: true` + `asynchronous: true`
- CachedImage 不用 `asynchronous: false` → ImageCacheProvider 已删除同步 requestPixmap 回退路径

## QML 编码约定

- 所有 .qml 文件以 `pragma ComponentBehavior: Bound` + `pragma ValueTypeBehavior: Assertable` 开头(单例除外)
- 所有 delegate 元素必须显式声明 `required property` (类型安全)
- `modelData` 和 `index` 必须通过 `required property` 获取, 不依赖隐式上下文
- Loader 加载的 Component 内部元素需要的数据通过外层的 id 引用 (如 `trackItem.modelData`)
- 颜色用 `Theme.xxx`, 字符串用 `Str.xxx`, 导航用 `Nav.xxx`
- 属性链很深 (如 `Server.settings.hdrPeakBrightness`) — 这是已知模式, 不要"优化"掉

## 架构债务 (已知, 等时机)

1. **上帝类**: EmbyClient (50+方法) / LibraryBrowser (45+槽 15个Model) / SettingsStore (27属性)
   → 不拆分。EmbyClient 等第二个 provider 出现, LibraryBrowser 等下一个大功能, SettingsStore 永不需要。
2. **裸指针传播**: EmbyClient*/CacheStore* 在 4 个类间共享, 所有权靠约定
   → 不改。Qt parent-child 保证运行期安全。退出时序 UAF (children 按构造顺序析构,
     serverMgr 先死 → ~PlaybackController 的 stop() 摸悬垂 m_emby/m_cache) 已修:
     main.cpp 里 aboutToQuit → PlaybackController::stop, 上报发生在对象全部存活时 (2026-07)。
3. **播放状态机不显式**: 状态散布在 m_hasVideo/m_playing/m_pendingStartSeconds 等 flag 中
   → 待加播放队列/AB 循环时引入 PlayState enum。实测散落密度低 (每个 flag 5-8 处),
     现在引入只是给 flag 改名, 无功能收益。
4. **Playback.mpv 泄露**: 业务部分已还 (2026-07)。speed/setSpeed/tracks/currentSid/
   chapters/currentChapter/setSlang/setAlang/addSubtitleFile 经 PlaybackController 转发,
   QML 业务代码用 `Playback.xxx`。`Playback.mpv` 仅保留诊断直连 (toggleStats,
   mpvVersion) 和渲染直连 (PlayerPage `player: Playback.mpv`) — 刻意保留, 不要"补全"转发。
   (DebugOverlay 及其 params/stats 侦测链已于 2026-07 整体拆除: 组件从未被实例化,
   observeStatsProperties 却常开, 属白耗。需要诊断时按 I 用 mpv 内置 stats。)

## 构建与部署

```sh
# 编译
cmake -G Ninja -S /root/myproject/mfplayer -B /root/myproject/mfplayer/build
cmake --build /root/myproject/mfplayer/build

# 注意: mpv 使用 fork 版本 (d3d11-render-api 分支), 不是上游
# OpenGL 后端有 Y-flip blit (gpu-next 不支持 FLIP_Y)
```

## 修改代码的注意事项

- platform/rendering/mpv/ 对项目其他部分零 include 依赖 → 可以独立修改和测试
- 改 mpvcontroller 的任何属性 → 检查 QML 中 `Playback.mpv.xxx` 的所有引用
- 改 MediaModel 的 roles → 检查 BrowsePage/DetailPage/PlayerPage delegate 中的 `required property`
- 改 CurlEngine → 它被 EmbyClient 和 CacheStore 各持一个实例, 两者用法相同但独立
- **改 DBWorker** → 它运行在独立线程, 只通过 QueuedConnection 与主线程通信。不要从主线程直接访问它的 QSqlDatabase
- **改用 I/O Pool** → 文件 I/O 操作通过 `ioPool().start()` 提交。回调必须用 QPointer 守卫 + QueuedConnection
- **`RowLayout` / `ColumnLayout` 放进 `Column` / `Row` / `Item` 时必须自己写 `width:`** →
  那三个不是 Layout，不给子项分配宽度。不写的话 Layout 会被内容撑成无限宽，里面的
  `Layout.fillWidth: true` 就填了个无限值，**`elide` / `wrapMode` / `maximumLineCount`
  全部静默失效**，长文本直接溢出压到隔壁。
  症状是"设了省略号却不省略"。判据: Layout 无 `width`/`anchors` **且**内部有 `fillWidth`。
  （父本身是 Layout 时不用管，用 `Layout.fillWidth` 就行。）
- **SVG 图标里「并排互不相连」的形状必须各写一个 `<path>`** → 挤在一个 `d` 里靠 `M`
  分子路径, svgtoqml 会合成**单个 `ShapePath`**, CurveRenderer 在 GPU 上可能只画其中一块
  (2026-08 `pause` 双竖条实机只出一根的根因; `skip_next`/`skip_previous` 同结构侥幸没炸,
  一并拆了)。**注意别一律拆**: `heart`/`home`/`movie`/`subtitles` 等 14 个图标的多子路径是
  **嵌套挖空**用的, 靠 `fillRule: WindingFill` 把内圈掏掉, 拆成多个 `<path>` 挖空就没了。
  判据: 子路径之间**互不嵌套**才拆。
- **文件名类文本用 `Text.Wrap` 不用 `Text.WordWrap`** → 剧集名常常是整串文件名
  (`xxx.2026.S01E02.2160p.WEB-DL.H265`)，里面没空格，WordWrap 找不到断点就一行到底
- **性能红线 — 不要回退以下优化**:
  - VideoRenderNode（D3D11/OpenGL）: OpenGL FBO 跨帧缓存，mpv 无新帧时只跳过 mpv render、缓存内容照常 blit。别改回每帧 gen/delete；也别在 render() 里提前 return 跳过 blit——Qt 每帧清屏重画，少 blit 一次视频区就黑一帧（2026-07 闪屏 bug 根因）
  - **Vulkan 路径架构（2026-07 第四轮定稿，前三轮全部翻车的教训都在这里）**：
    - 宿主自建 VkDevice（platform/rendering/vulkandevice.cpp，main.cpp 经 setGraphicsDevice 交给 Qt）：支持什么 feature 开什么（仅关 robustness 两项），feature 链经 mpv_vulkan_init_params.enabled_features 原样告知 libplacebo。**根因铁律：libplacebo 对"并入核心"的扩展只看设备 apiVersion 就直接用（pl_vulkan_import context.c:1720），并入核心 ≠ feature 已启用——Qt 默认设备没开 synchronization2/pushDescriptor，用了就是 UB，NVIDIA 表现为随机 device lost**。别改回用 Qt 默认设备；若接管失败，dll 端 fallback 会 cap max_api_version=1.2 兜底（context.c init_vulkan），这两处 cap 逻辑别删
    - 显示路径 = mpv 渲染进自有 VkImage + Qt 采样（VulkanVideoNode : QSGSimpleTextureNode，updatePaintNode 里跑 mpv render、fromNative 包纹理、每帧 setNativeLayout(out_layout)）。**别改回 blit 进 Qt 渲染目标**：QSGRenderNode::render() 在 Qt render pass 内部，vkCmdBlitImage/布局 barrier 在 pass 里非法（validation 实锤），且 Qt 的渲染目标没有 TRANSFER_DST usage
    - 历史结论修正：2026-07 早前"跨 command buffer barrier 成链在 NVIDIA 不可行"的结论是在 sync2 未启用（每个 barrier2 调用都是 UB）的污染环境里得出的，不作数；规范上同队列跨 buffer 成链合法
    - mpv fork 侧（third_party/mpv-source/video/out/gpu_next/context.c）：done_frame_vulkan 不调用 pl_gpu_finish()（渲染循环里"seriously disadvised"）；wrapped_tex 跨帧缓存 + persistent_target_tex 标记（libmpv_gpu_next.h/.c）别删；hold_ex out_layout 模式只查询不转换、经 mpv_vulkan_fbo.out_layout 回传；guard_sem 空提交防 WAR。改 render_vulkan.h 的结构体（out_layout/enabled_features 都是尾部追加）→ dll 和 exe 必须一起重编，头文件同步 cp 到 mpv-msvc/include
    - 销毁 VkImage 前必须 vkDeviceWaitIdle（VulkanVideoNode::destroyImage，resize 和节点析构/场景图失效两条路都走它）——异步化后没有每帧排空兜底，2026-07 全屏切换 device lost 就是裸销毁炸的
    - **createTextureFromRhiTexture 会拿走 QRhiTexture 所有权**（Qt 文档明示"destroyed together"）——destroyImage 靠 m_rhiTexOwnedByQsg 标记决定是否手动 delete m_rhiTex，别删标记改回双删（2026-07 全屏堆损坏闪退根因）
    - **ensureImage 必须先创建新 VkImage 再销毁旧的**——fork 的 wrap_fbo 缓存用句柄值当键，先销毁会让驱动复用句柄、dll 拿死 VkImageView 渲染（2026-07 反复全屏 device lost 根因）；fork 侧缓存判断的宽高比较是配套保险，别删
    - **fork perfdata() 必须清零输出结构体**（libmpv_gpu_next.c）——vo_libmpv 只要钩子存在就报 VO_TRUE，上层结构体未初始化，不清零则按 i 查 vo-passes 越界崩（2026-07 stats 闪退根因，与 Vulkan 无关）
    - 三个后端调 mpv_render_context_render 都必须传 BLOCK_FOR_TARGET_TIME=0——缺省 1 会睡到该帧目标显示时刻，UI 被锁到视频帧率（2026-07 Vulkan"UI 跟着视频走"根因）
  - **`m_currentMediaSources` 只在为空时才用 PlaybackInfo 的结果填**。playItem 开头会拿详情里的
    第一个源当 `MediaSourceId` 带进 PlaybackInfo 请求, Emby 就**只回那一个源** —— 拿它覆盖等于把
    详情里的全部版本抹成一个, 版本选择器只剩当前版本。详情页那条路吃 `itemData.MediaSources`
    看不出来, 卡片直接起播 (itemData 为 null) 才现原形 (2026-08)
  - PlayerControls: progressSlider.value 只在 `_lastSecond` 变化时更新。别移回每帧赋值
  - CacheStore: updateItemFieldInCache 找到 item 直接 `return`。别删外层 return
  - providerUrl / fetchImage: 不做 `QFile::exists()`。别加回 stat
  - m_imageCache / m_pendingDownloads: 主线程独占，无 mutex。别加回 m_imageCacheMutex。
    **这条一度是假的** — ImageCacheProvider 曾在 reader 线程调 `resolveImagePath()` 读
    m_imageCache，和主线程四类写入 (启动装载/过期删除/下载完成/clear) 是真数据竞争，启动
    批量 insert 触发 rehash 撞上首屏请求就是 UB。2026-08 的修法不是加锁，是**把路径在主
    线程解析好随 provider URL 带走**，让"主线程独占"重新成为事实。**别把路径解析挪回
    provider 侧**，那等于把竞争原样装回去
  - provider URL 用 base64url 编码路径，不是 percent-encoding。编码后只有 `A-Za-z0-9-_`，
    不含 `/`，QUrl 不会二次变形 — Windows 盘符、空格、非 ASCII 路径全免疫。
    编码必须留在 C++ 侧：JS 的 `btoa` 遇到非 ASCII 直接抛异常
  - provider 收到路径后**硬卡在 `imageCacheDir()` 内**，越界当没路径 (透明图)。
    带路径的 provider 等于把任意本地文件读取入口暴露给 QML，这道收口别放宽
  - ImageCacheResponse: m_image 无 mutex（时序保证：finished 前已赋值）。别加回 mutex
  - 图片缓存全链路 QImage（textureFactoryForImage 直接吃，隐式共享零拷贝）。
    别改回 QPixmap — 旧链路 QImage→QPixmap→toImage 每次请求多两次 ~1.5MB memcpy，缓存命中也逃不掉
  - ImageCacheResponse 解码跑 QThreadPool::globalInstance()。别改回每请求一个裸 std::thread
  - QImage 用 std::move 插入 LRU（避免 mutex 内拷贝）。别改回拷贝
  - mpv hwdec=auto-safe（硬解优先、失败自动回落软解, 2026-07, DV P7.6 实测直通）。别改回 no — 4K HEVC/AV1 软解吃满 CPU
  - playItem: reportPlaybackStart 是 fire-and-forget, play() 不等上报回执。别套回回调里 — 白等一个 RTT
  - TabDefault 浏览: 首屏 kPageSize(200) 立即显示 + loadMore 自动渐进拉满（万部库全量 JSON 主线程解析是 100ms+ 卡顿）。
    别改回无 limit 全量拉取。续拉走回调版 fetchItemsFiltered + m_browseGeneration 防竞态, 别改走 itemsFetched 分发
  - MediaModel 去重指纹只存 size + firstId (m_lastSourceSize/m_lastSourceFirstId)。别改回持有整个源 QJsonArray
  - m_idToIndex: setItems/appendItems 前调 `reserve()`。别删
  - MediaModel: alphaIndex 通过 Q_PROPERTY 增量维护。别让 QML 每次遍历全部 item 重建
  - getAllItems: 直建 QVariantList 不用 per-row get()。别改回循环调用 get(i)
  - m_itemsCache: 有 LRU 上限。别删除限制让它无界增长
- 不要新建 utils/helpers/common 文件 → 功能放回相关模块
- 不要给现有代码加抽象层/接口/工厂 → 保持可直接追踪的调用路径
- **CurlEngine 别改 socket-action 事件驱动** (2026-07 试过, 已回退): h2 multiplex pipe-wait
  的排队传输会因 curl timerCallback 收到 -1/STOP 而事件断流, 干等到假超时 (实测 Emby 场景
  "什么都加载不了"); Windows QSocketNotifier 的 FD_WRITE 边缘触发语义也未验证。
  10ms 轮询每请求只多 ~5ms, 不值得再冒险，轮询兼作 CDN 限流，别调小。
