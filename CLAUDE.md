# mfplayer — Emby 桌面播放器

基于 Qt 6.8.2 QML + C++，libmpv 渲染内核，Emby REST API 的桌面视频播放客户端。

## 技术栈

| 层 | 技术 |
|---|---|
| UI | Qt 6.2+ QML (Quick Controls 2, Fusion 风格) |
| 视频渲染 | libmpv + D3D11 (Windows) / OpenGL (Linux)，QSGRenderNode 嵌入 QML |
| 网络 | libcurl (CurlEngine)，HTTP/2 开启 |
| 缓存 | SQLite (Qt Sql)，缓存 item 详情、图片、季列表 |
| 持久化 | QSettings (INI 格式)，存 Emby 凭据、音量、窗口大小 |
| 构建 | CMake 3.22+，C++23，AUTOMOC，Windows: MSYS2 UCRT + Clang |

## 项目结构

```
/root/myproject/mfplayer/
├── CMakeLists.txt
├── app/
│   └── main.cpp                       # 入口
├── core/                              # 核心业务逻辑（不依赖 UI）
│   ├── playback/
│   │   └── playbackcontroller.h/.cpp  # 播放控制、音量、全屏、HDR、进度上报 (→ QML "Playback")
│   ├── library/
│   │   └── librarybrowser.h/.cpp      # 浏览、搜索、推荐、分页 (→ QML "Library")
│   ├── detail/
│   │   └── detailmanager.h/.cpp       # 详情页、季/集、相似、人物、收藏 (→ QML "Detail")
│   ├── server/
│   │   └── servermanager.h/.cpp       # 登录、服务器切换、session，拥有 emby/cache/settings (→ QML "Server")
│   ├── media/models/
│   │   ├── mediamodel.h               # QAbstractListModel，16 个 Role
│   │   └── mediamodel.cpp             # fromJson、data、roleNames、get
│   ├── providers/emby/
│   │   ├── embyclient.h               # Emby REST API 客户端声明
│   │   └── embyclient.cpp             # 实现
│   ├── cache/
│   │   ├── cachestore.h               # SQLite 缓存声明
│   │   ├── cachestore.cpp             # item/图片/季缓存实现
│   │   ├── imagecacheprovider.h       # QQuickImageProvider 子类
│   │   └── imagecacheprovider.cpp
│   ├── network/
│   │   ├── curlengine.h               # libcurl 异步 HTTP 引擎
│   │   └── curlengine.cpp
│   └── settings/
│       ├── settingsstore.h            # QSettings 封装声明
│       └── settingsstore.cpp          # 持久化实现
├── platform/
│   └── rendering/mpv/
│       ├── mpvcontroller.h            # libmpv 封装，14 个 Q_PROPERTY
│       ├── mpvcontroller.cpp          # mpv 事件循环、属性观测
│       ├── mpvrenderitem.h            # QSGRenderNode 子类声明
│       └── mpvrenderitem.cpp          # D3D11 (Win) / OpenGL (Linux) 渲染到 QML
├── ui/
│   └── qml/
│       ├── pages/                     # 顶层页面
│       │   ├── Main.qml               # ApplicationWindow + StackView + 登录
│       │   ├── BrowsePage.qml         # 主页：继续观看、我的媒体、最新、媒体库浏览
│       │   ├── DetailPage.qml         # 详情页：海报、元数据、轨道选择、剧集、媒体信息
│       │   └── PlayerPage.qml         # 播放器：视频面、顶栏、轨道切换、播放列表
│       ├── views/                     # 子视图/嵌入视图
│       │   ├── HomeView.qml           # 主页内容区
│       │   ├── SuggestionsView.qml    # 推荐标签页
│       │   ├── LibraryGridView.qml    # 媒体库网格
│       │   ├── PlayerControls.qml     # 底部控制条
│       │   └── DebugOverlay.qml       # 调试信息覆盖层
│       ├── components/                # 可复用组件
│       │   ├── CachedImage.qml        # 带 SQLite 缓存的异步 Image
│       │   ├── RoundedImage.qml       # ShaderEffect 圆角图片
│       │   ├── Icon.qml               # SVG 动态着色图标
│       │   ├── HorizontalMediaRow.qml # 可复用水平滚动行
│       │   ├── TrackSelector.qml      # 视频版本/音轨/字幕统一选择器
│       │   ├── StyledPopup.qml        # 统一样式弹窗（动画+背景）
│       │   ├── PlayControlsRow.qml    # DetailPage 播放按钮+下一集+进度条
│       │   ├── PersonBioSection.qml   # DetailPage 人物传记信息
│       │   ├── SeriesSection.qml      # DetailPage 剧集季选择+剧集列表
│       │   ├── CastAndCrewRow.qml     # DetailPage 演员/导演横滚行
│       │   ├── StudiosSection.qml     # DetailPage 制片商
│       │   ├── SimilarItemsSection.qml# DetailPage 相似推荐/人物作品
│       │   └── MediaInfoSection.qml   # DetailPage 媒体信息卡片
│       ├── dialogs/
│       │   └── ServerPanel.qml        # 服务器管理面板
│       └── theme/                     # 单例
│           ├── Theme.qml              # 颜色/动画/圆角常量
│           ├── Str.qml                # 本地化字符串 + 格式化函数
│           └── Nav.qml                # 集中导航（push/pop）
├── common/
│   ├── version.h
│   ├── constants.h                    # 跨模块共享常量（ticks、bitrate、item types 等）
│   └── embyfields.h                   # Emby API Fields 字段常量
├── resources/                         # 图标、字体、appicon
│   ├── fonts/                         # 思源黑体 (SourceHanSans)
│   └── icons/                         # Material Symbols SVG × 19
├── third_party/
│   ├── mpv-msvc/                      # MSVC 编译的 libmpv 输出
│   │   ├── include/mpv/               # C 头文件 (含 render_d3d11.h)
│   │   ├── bin/mpv-2.dll              # 运行时 DLL
│   │   └── lib/                       # mpv.lib (导入库) + deps/ (vcpkg DLLs)
│   └── mpv-source/                    # 完整 mpv 源码 (PR #17764: D3D11 render API)
├── tools/
│   ├── build_mpv_msvc.ps1            # MSVC + vcpkg 一键编译 mpv
│   └── package.sh                     # Windows 打包脚本
└── build/                             # CMake 构建输出
```

## 架构与数据流

### 对象所有权

```
ServerManager (→ QML "Server")
├── SettingsStore       # QSettings 持久化
├── EmbyClient          # REST API 客户端
│   └── CurlEngine      # libcurl 异步 HTTP
├── CacheStore          # SQLite 内容缓存 + 图片下载
│   └── CurlEngine      # 图片下载专用
└── CredentialStore     # 服务器凭据 (servers 表)

PlaybackController (→ QML "Playback")
├── MpvController       # libmpv 封装 (→ QML "Playback.mpv")
└── QTimer              # 进度上报 (10s)

LibraryBrowser (→ QML "Library")
└── MediaModel × 20+    # 浏览/搜索/收藏/推荐模型

DetailManager (→ QML "Detail")
└── MediaModel × 6      # 详情/季集/相似/人物模型
```

### 关键数据流

**登录**: QML → `Server.connectEmby()` → EmbyClient::login() → POST `/Users/AuthenticateByName` → fetchLibraries → emit `librariesReady` → Library.onLibrariesFetched()

**浏览详情**: QML → `Detail.browseItem(itemId)` → 先查 SQLite 缓存 → 未命中则 HTTP → emit `itemDetailReady(itemId, data)` → QML 快照到本地 `itemData`

**播放**: QML → `Playback.playItem()` → fetchPlaybackInfo → reportPlaybackStart → mpv->play() → FILE_LOADED → 注入外挂字幕 → 选轨道 → 启动进度定时器

**进度上报**: 每 10s: `onProgressTimer` → reportPlaybackProgress。停止/EOF: reportPlaybackStop

**排序/筛选**: QML → `Library.setSortBy(n)` / `setFilterPlayed(n)` → setter 更新状态 + emit signal + `applySortAndFilter()` → `fetchItemsFiltered({sortBy, sortOrder, filters})` → `itemsFetched` → `contentModel` 更新 → GridView 自动刷新。排序偏好持久化到 QSettings (`library/sortBy`, `library/sortAscending`)。

### FetchParams (EmbyClient)

```cpp
struct FetchParams {
    QString parentId, includeTypes, filters;
    QString sortBy, sortOrder;       // 排序字段 + 方向 (Ascending/Descending)
    QString genreIds, studioIds, personIds;
    QString tags, years, officialRatings, searchTerm;
    int limit = 0, startIndex = 0;
};
```

### LibraryBrowser 排序/筛选属性

| Q_PROPERTY | 类型 | 说明 |
|---|---|---|
| sortBy | int | 0=名称, 1=年份, 2=评分, 3=添加时间, 4=播放时间 |
| sortAscending | bool | true=升序, false=降序 |
| filterFavorites | bool | 仅显示收藏 |
| filterPlayed | int | 0=全部, 1=已看, 2=未看 |

适用 Tab: Default(0), Episodes(6), Folders(7)。Suggestions/Favorites/Genres/Studios 不受影响。

### EmbyClient API 端点一览

| 方法 | 端点 | 用途 |
|------|------|------|
| POST | `/emby/Users/AuthenticateByName` | 登录 |
| GET | `/emby/Users/{id}/Views` | 获取媒体库列表 |
| GET | `/emby/Users/{id}/Items` | 浏览/搜索 item |
| GET | `/emby/Users/{id}/Items/Resume` | 继续观看（服务端按系列分组） |
| GET | `/emby/Users/{id}/Items/{itemId}` | item 详情 |
| GET | `/emby/Shows/{id}/Seasons` | 季列表 |
| GET | `/emby/Shows/{id}/Episodes` | 剧集列表 |
| GET | `/emby/Users/{id}/Items/Latest` | 最新添加 |
| GET | `/emby/Items` (search) | 搜索提示 |
| POST | `/emby/Items/{id}/PlaybackInfo` | 获取流 URL + PlaySessionId |
| POST | `/emby/Sessions/Playing` | 上报播放开始 |
| POST | `/emby/Sessions/Playing/Progress` | 上报播放进度 |
| POST | `/emby/Sessions/Playing/Stopped` | 上报播放停止 |
| POST | `/emby/Sessions/Capabilities/Full` | 注册播放能力 |
| POST | `/emby/Users/{id}/PlayedItems/{itemId}` | 标记已看 |
| DELETE | `/emby/Users/{id}/PlayedItems/{itemId}` | 标记未看 |
| POST/DELETE | `/emby/Users/{id}/FavoriteItems/{itemId}` | 收藏/取消 |

### MediaModel Role 映射 (QML 绑定名)

| C++ Role 枚举 | QML 属性名 | 来源 JSON 字段 |
|---|---|---|
| IdRole | itemId | Id |
| NameRole | itemName | Name |
| TypeRole | itemType | Type |
| ImageUrlRole | imageUrl | 拼接自 ImageTags |
| YearRole | year | ProductionYear |
| OverviewRole | overview | Overview |
| ParentIdRole | parentId | ParentId |
| IndexNumberRole | indexNumber | IndexNumber |
| ChildCountRole | childCount | ChildCount |
| SeriesNameRole | seriesName | SeriesName |
| SortNameRole | sortName | SortName (fallback Name) |
| PlaybackPositionTicksRole | playbackPositionTicks | UserData.PlaybackPositionTicks |
| PlayedPercentageRole | playedPercentage | UserData.PlayedPercentage |
| RunTimeTicksRole | runTimeTicks | RunTimeTicks |
| PlayedRole | played | UserData.Played |
| BackdropUrlRole | backdropUrl | BackdropImageTags[0] |
| IsFavoriteRole | isFavorite | UserData.IsFavorite |

### MpvController 暴露的属性

| Q_PROPERTY | 类型 | 说明 |
|---|---|---|
| position | double | 当前播放位置（秒） |
| duration | double | 总时长（秒） |
| playing | bool | 是否正在播放 |
| volume | int | 音量 (0-100) |
| hasVideo | bool | 是否有视频轨 |
| tracks | QVariantList | 当前轨道列表 [{id, type, title, lang, selected, ...}] |
| currentSid | int | 当前字幕轨道 ID |
| currentAid | int | 当前音频轨道 ID |
| videoOutParams | QVariantMap | 视频输出参数 (width, height, pixelFormat, etc.) |
| videoParams | QVariantMap | 视频编码参数 (codec, bitrate, hdr, etc.) |
| audioOutParams | QVariantMap | 音频输出参数 (samplerate, channels, etc.) |
| stats | QVariantMap | 播放统计 (droppedFrames, cacheSpeed, etc.) |
| chapters | QVariantList | 章节列表 |
| currentChapter | int | 当前章节索引 |

### QML 页面导航 (StackView)

```
Main.qml
  StackView { initialItem: browsePage }
    ├── BrowsePage       # 主页
    │   ├── click 我的媒体 → 切到 library view
    │   ├── click 继续观看 → push PlayerPage (Episode) / push DetailPage (Movie)
    │   ├── click 最新 → push DetailPage
    │   └── click 媒体库 grid → push DetailPage (Movie/Series) / push PlayerPage (Episode)
    ├── DetailPage       # 详情页（可多层嵌套）
    │   ├── click 播放 → push PlayerPage
    │   ├── click 单集 → push DetailPage (episode)
    │   ├── click 相似推荐 → push DetailPage (new item)
    │   └── 返回 → pageStack.pop()
    └── PlayerPage       # 播放器
        ├── 返回 → pop
        ├── EOF → 自动播下一集或 pop
        └── 切换视频版本 → Player.playItem() 重启播放
```

## 渲染架构 (D3D11 / OpenGL 双后端)

### 平台差异

| 平台 | Qt 图形 API | mpv 渲染 API | 关键头文件 |
|------|------------|-------------|-----------|
| Windows | `Direct3D11` (QRhi D3D11) | `MPV_RENDER_API_TYPE_D3D11` | `mpv/render_d3d11.h` |
| Linux | `OpenGL` (QRhi OpenGL) | `MPV_RENDER_API_TYPE_OPENGL` | `mpv/render_gl.h` |

平台分支通过 `#ifdef Q_OS_WIN` 隔离，不改 Linux 路径。

### 渲染管线 (Windows D3D11)

```
Qt SceneGraph render pass
  → VideoRenderNode::render()
    → win->beginExternalCommands()        // Qt 挂起 D3D11 render pass
    → QRhi::nativeHandles()               // 拿 ID3D11DeviceContext
    → ctx->OMGetRenderTargets()           // 拿当前 RTV
    → rtv->GetResource()                  // 拿到 ID3D11Texture2D
    → mpv_d3d11_fbo{tex, w, h}           // 填 mpv FBO 结构
    → mpv_render_context_render()         // mpv D3D11 直接画到 Qt 纹理
    → rtv->Release(); res->Release()      // 释放 COM 引用
    → win->endExternalCommands()          // Qt 恢复 render pass
```

### 关键参数

- `BLOCK_FOR_TARGET_TIME = 0` — 不阻塞渲染线程，Qt 自己控制 frame pacing
- `ADVANCED_CONTROL = 1` — mpv 仅渲染，不接管 swapchain/timing
- 不传 `FLIP_Y` — D3D11 纹理坐标与 Qt 一致，无需翻转

### MpvController::ensureRenderCtx() 双路径

- **Windows**: 从 `window->rhi()->nativeHandles()` 取 `ID3D11Device*`，创建 `MPV_RENDER_API_TYPE_D3D11` 的 render context
- **Linux**: 保持原有 OpenGL 路径不变

### libmpv 源码构建

- 源码位于 `mpv-source/`，基于 mpv PR #17764（D3D11 render API backend）
- `build_mpv_msvc.ps1` — MSVC + vcpkg 一键编译，输出到 `mpv-msvc/`，自动收集依赖 DLL 到 `mpv-msvc/lib/deps/`
- DLL 部署: `mpv-msvc/bin/mpv-2.dll` + `mpv-msvc/lib/deps/*.dll` 由 CMake post-build 自动复制到 exe 同目录
- `done_frame()` 中释放 wrapped texture 引用，避免全屏切换 swapchain resize 崩溃

### 渲染循环驱动

- **不再使用 Timer** — 之前 `setInterval(0)` 忙等导致 CPU 常驻高占用
- 由 mpv 的 `renderUpdateNeeded` 回调解耦驱动，Qt SceneGraph 自身处理 UI 重绘

### D3D11 待解决问题

1. **Device lost 处理** — HDR 开关/显卡切换/睡眠恢复时 D3D11 device 可能 reset，需监听 `sceneGraphInvalidated` 重建 render context
2. **Render target 获取** — 当前用 `OMGetRenderTargets` 是捷径，未来 Qt 可能不保证 RTV 永远是 swapchain backbuffer

## 关键架构陷阱

### 0. User-Agent 必须全局统一
- 所有 HTTP 请求（Emby API、图片下载、mpv 流媒体）的 UA 必须一致为 `MfPlayer/1.0`
- 认证头里的 `Client`、`DeviceId` 也要一致
- UA 不统一会导致 CDN 拒绝服务（尤其是 115 CDN 的 307 重定向场景）

### 1. `Detail.itemDetailReady` 是全局广播信号
- 多个 DetailPage 实例连接同一个 `Detail` 信号
- 页面 A 的 `onItemDetailReady` 会在页面 B 加载数据 时触发
- **修复方案**：每个 DetailPage 在 `onItemDetailReady` 时快照到本地 `itemData`（`property var`），handler 开头加 `if (fetchedId !== itemId) return` 守卫

### 2. `browseItem()` 是异步的
- 先查 SQLite 缓存（同步），未命中走 HTTP（异步）
- 缓存命中时 `itemDetailReady` 同步发出；未命中时异步发出
- 调用 `browseItem()` 后不能假设数据已更新

### 3. StackView 页面生命周期
- `Component.onCompleted` 在 push 时触发
- 被 push 下去的页面 `visible = false`（不销毁）
- `onVisibleChanged` 可检测页面被 pop 回来
- pop 时页面被销毁，`Component.onDestruction` 触发

### 4. CachedImage 图片下载
- 使用 `onEmbyUrlChanged` 触发下载
- URL 变化时需先清 `source`，否则显示旧图
- 页面销毁时需清 `source` + `_pendingUrl`，否则产生 JPEG 半截解码错误
- **缓存命中时必须通过 `image://imgcache/<md5>` Provider URL 加载，不能用 `file://` 路径**。file:// 路径绕过 ImageCacheProvider，Qt 内部解码器不按 KeepAspectRatio 处理 sourceSize，导致图片尺寸错误

### 5. Emby MediaSources 与 MediaStreams 的关系
- 顶层 `MediaStreams` 是第一个 MediaSource 的流
- 每个 `MediaSources[i].MediaStreams` 是该版本特有的流
- `mediaStreamsForSelectedVideo()` 函数按 `selectedVideoId` 选取对应源的流
- 音轨/字幕选择器的数据必须跟当前视频版本走

### 6. ShaderEffect 与 Image.fillMode 不兼容
- QML Image 的 `fillMode`（PreserveAspectCrop/Fit）只影响自身的绘制阶段，**不会**体现在 ShaderEffect 的 source 纹理坐标上
- ShaderEffect 始终拿到原始 UV (0,0)~(1,1)，fillMode 的裁剪/缩放效果丢失，导致图片拉伸
- **解决方案**：CachedImage 使用 `fillMode: Stretch`，在 fragment shader 中手动实现 PreserveAspectCrop 的 UV 映射
- **uniform 顺序陷阱**：QML ShaderEffect 的 `property` 声明顺序必须和 GLSL `std140` uniform block 严格一致，否则 buffer 布局错位导致渲染异常
- `RoundedImage` 的 shader (`roundedmask.frag`) 同时处理圆角和宽高比裁剪
