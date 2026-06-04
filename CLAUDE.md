# MfPlayer — AI 开发速查

> **技术栈**: Qt 6.11 QML + C++23, libmpv (gpu-next fork), Emby REST API, libcurl
> **最后更新**: 2026-06-04

---

## 项目地图

```
common/         → 纯常量，零依赖
core/network/   → CurlEngine (libcurl multi 接口，主线程非阻塞)
core/providers/ → EmbyClient (REST 全覆盖，上帝类，先别拆)
core/cache/     → CacheStore (内存缓存, 主线程) + DBWorker (SQLite, 独立线程) + ImageCacheProvider
core/media/     → MediaModel (QAbstractListModel, 17 roles, O(1) lookup)
core/settings/  → SettingsStore (QSettings wrapper, 27 属性, 也不拆)
core/server/    → ServerManager (多服务器管理, 持有其他所有 core 对象) + CredentialStore
core/playback/  → PlaybackController (播放编排: Emby→mpv 桥接)
core/library/   → LibraryBrowser (15 个 MediaModel, 浏览/搜索/收藏/分页)
core/detail/    → DetailManager (详情页数据, 6 个 MediaModel)
platform/rendering/mpv/
    mpvcontroller → libmpv C API 包装, 渲染上下文管理
    mpvrenderitem → QQuickItem + QSGRenderNode, 三后端渲染 (D3D11/Vk/GL)
ui/providers/   → ImageCacheProvider（IconProvider 已删除，死代码）
ui/qml/
    theme/      → Theme/Str/Nav (Singleton)
    pages/      → Main/Browse/Detail/Player
    views/      → HomeView/LibraryGridView/PlayerControls/DebugOverlay...
    components/ → HdrPqOverlay/CachedImage/RoundedImage/TrackSelector...
    shaders/    → hdr_pq.frag (sRGB→PQ), roundedmask.frag
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

5 个 context property (setContextProperty):
- `Playback` (PlaybackController) — 包含嵌套 `Playback.mpv` (MpvController)
- `Library` (LibraryBrowser) — 30 个 Q_PROPERTY, 15 个 MediaModel
- `Detail` (DetailManager) — 7 个 Q_PROPERTY, 6 个 MediaModel
- `Server` (ServerManager) — 嵌套 `Server.settings` + `Server.emby` + `Server.cache`
- `_hdrActive` (bool), `_appDir`, `_appVersion`, `_qtVersion`, `_startupFile`

qmlRegisterType: 只有 `MpvRenderItem`。

ImageProvider: `image://imgcache/` → ImageCacheProvider → CacheStore。

## 播放启动链路 (user click → frame on screen)

```
QML: Playback.playItem(id, ticks)
  → PlaybackController: cache lookup → ++m_playGeneration (防竞态)
  → EmbyClient::fetchPlaybackInfo (网络, CurlEngine 主线程驱动)
  → EmbyClient::reportPlaybackStart (网络)
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
| **CPU Pool** (=CPU 核数) | 图片解码 (QImageReader), 大 JSON 解析 | QThreadPool::globalInstance() |
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
QML UI (sRGB) → RGBA16F FBO → hdr_pq.frag shader (sRGB→Rec.709→Rec.2020→PQ) → 同一 swapchain
```

- mpv 和 UI 共用同一个 HDR10 swapchain，两条并行管线
- `_hdrActive` 在启动 300ms 后才被设置 (swapchain warmup 后才能检测)
- 启动时 Main.qml 有黑色遮罩层 (`hdrStartupCover`) 防过曝，检测完成后淡出
- SDR 系统自动回退: `_hdrActive=false` → `layer.enabled=false` → 零 shader 开销
- hdr_pq.frag 刻意不 unpremultiply alpha (避免 ClearType 子像素偏色)

## 异步安全机制

| 机制 | 用途 | 位置 |
|------|------|------|
| 代计数器 generation | 取消过期异步回调 | PlaybackController (`m_playGeneration`), ServerManager, CacheStore (`m_writeGeneration`) |
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
- `resolveImagePath()` 不再做 stat() — 信任 loadImageCache() 已验证的文件

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
- 图片加载: CachedImage → CacheStore::cachedImageUrl (内存缓存, 无 stat) → ImageLoadQueue (max 3 concurrent) → ImageCacheProvider
- RoundedImage: Stretch fill + roundedmask.frag shader 做 GPU 圆角, 不用 OpacityMask (无额外 FBO)
- Icon: 23 个 MFIcon_* 全部常驻, visible 切换, 不用 Loader (避免切换延迟)
- Flickable: 全部 `interactive: false`, 用 WheelHandler + NumberAnimation 模拟滚动 (统一手感)
- 滑块防抖: hdrPeakBrightness/sdrWhiteNits/seekStep/windowSize 使用 200ms QTimer 防抖, 避免每次拖动像素写 QSettings
- MediaModel::fromJson 的 BackdropImageTags 只解析一次，复用结果（减少 O(N) 次 QJsonObject key lookup）
- langCodeToName 用 static QHash 代替 30+ if/else 链（O(1) 替代 O(N)）
- m_itemsCache 有 LRU 上限 (200 parent folders)，避免数万文件夹浏览后内存膨胀到 192MB+
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
   → 不改。Qt parent-child 已保证生命周期正确。
3. **播放状态机不显式**: 状态散布在 m_hasVideo/m_playing/m_pendingStartSeconds 等 flag 中
   → 待加播放队列/AB 循环时引入 PlayState enum。
4. **Playback.mpv 泄露**: QML 绕过 PlaybackController 直接操作 MpvController
   → 待加转发层时统一。

## 构建与部署

```sh
# 编译
cmake -G Ninja -S /root/myproject/mfplayer -B /root/myproject/mfplayer/build
cmake --build /root/myproject/mfplayer/build

# 注意: mpv 使用 fork 版本 (d3d11-render-api 分支), 不是上游
# Vulkan 路径依赖 Qt 私有头文件 (QVkSwapChain) — Qt 版本升级时需要验证
# OpenGL 后端有 Y-flip blit (gpu-next 不支持 FLIP_Y)
```

## 修改代码的注意事项

- platform/rendering/mpv/ 对项目其他部分零 include 依赖 → 可以独立修改和测试
- 改 mpvcontroller 的任何属性 → 检查 QML 中 `Playback.mpv.xxx` 的所有引用
- 改 MediaModel 的 roles → 检查 BrowsePage/DetailPage/PlayerPage delegate 中的 `required property`
- 改 CurlEngine → 它被 EmbyClient 和 CacheStore 各持一个实例, 两者用法相同但独立
- **改 DBWorker** → 它运行在独立线程, 只通过 QueuedConnection 与主线程通信。不要从主线程直接访问它的 QSqlDatabase
- **改用 I/O Pool** → 文件 I/O 操作通过 `ioPool().start()` 提交。回调必须用 QPointer 守卫 + QueuedConnection
- **性能红线 — 不要回退以下优化**:
  - VideoRenderNode: OpenGL FBO+纹理跨帧缓存，Pause 3帧无变化跳过 render。别改回每帧 gen/delete
  - PlayerControls: progressSlider.value 只在 `_lastSecond` 变化时更新。别移回每帧赋值
  - CacheStore: updateItemFieldInCache 找到 item 直接 `return`。别删外层 return
  - resolveImagePath / fetchImage: 不做 `QFile::exists()`。别加回 stat
  - m_imageCache / m_pendingDownloads: 主线程独占，无 mutex。别加回 m_imageCacheMutex
  - ImageCacheResponse: m_pixmap 无 mutex（时序保证：finished 前已赋值）。别加回 m_pixmapMutex
  - QPixmap 用 std::move 插入 LRU（避免 mutex 内拷贝）。别改回拷贝
  - m_idToIndex: setItems/appendItems 前调 `reserve()`。别删
  - MediaModel: alphaIndex 通过 Q_PROPERTY 增量维护。别让 QML 每次遍历全部 item 重建
  - getAllItems: 直建 QVariantList 不用 per-row get()。别改回循环调用 get(i)
  - m_itemsCache: 有 LRU 上限。别删除限制让它无界增长
- 不要新建 utils/helpers/common 文件 → 功能放回相关模块
- 不要给现有代码加抽象层/接口/工厂 → 保持可直接追踪的调用路径
