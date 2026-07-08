# MfPlayer

Emby desktop media player powered by Qt 6 QML + libmpv.

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![License](https://img.shields.io/badge/license-GPLv3-green)
![Qt](https://img.shields.io/badge/Qt-6.11-green)

## Features

- **Three rendering backends (D3D11 / Vulkan / OpenGL)** — mpv renders directly into Qt's scene graph with zero-copy texture sharing, auto-selected per platform capability with graceful fallback
- **HDR support** — HDR10/HLG passthrough with adjustable peak brightness, shared HDR10 swapchain between video and QML UI (sRGB → Rec.709 → Rec.2020 → PQ)
- **Emby integration** — browse libraries, continue watching, search, favorites, multi-server login
- **Fuzzy subtitle matching** — Jaro-Winkler algorithm for automatic subtitle track selection
- **Customizable shortcuts** — all keyboard shortcuts can be remapped
- **Local playback** — drag & drop video files and external subtitles, auto-build playlists from folders
- **SQLite caching** — image and metadata cache for fast browsing, all writes off the main thread

## Why it's harder than it looks

Compositing a GPU video decoder frame with a QML scene graph (not just an overlay) means owning the graphics device instead of letting Qt manage it. The Vulkan backend in particular required building a custom `VkDevice` shared between Qt and libmpv/libplacebo, with explicit feature negotiation — libplacebo will silently assume "core" Vulkan 1.3 features are *enabled* just because the API version supports them, which is undefined behavior on stock Qt devices and shows up as random `DEVICE_LOST` on NVIDIA. Getting this right took several rewrites; see `platform/rendering/vulkandevice.cpp` for the current (working) design.

## Screenshots

![主页](https://img.mirane.cc/docs/1783420669956.png)
![详情页](https://img.mirane.cc/docs/1783420647709.png)

## Building (Windows, MSVC)

### Prerequisites

- Visual Studio 2022 (Community or above)
- Qt 6.11+ (MSVC 2022 64-bit)
- vcpkg
- Git

### Build mpv

```powershell
.\tools\build_mpv_msvc.ps1
```

This builds libmpv from source with D3D11 render API support (PR #17764) and installs vcpkg dependencies.

### Build MfPlayer

```powershell
$env:VCPKG_ROOT = "D:/vcpkg"   # or wherever you installed vcpkg
cmake -G Ninja -S . -B build -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build
```

### Package

```powershell
.\tools\package.ps1
```

Creates a portable zip in `deploy/`.

## Project Structure

```
├── app/                  # Application entry point
├── core/
│   ├── network/          # CurlEngine — non-blocking libcurl multi interface on the main thread
│   ├── providers/        # EmbyClient — full REST API coverage
│   ├── cache/            # In-memory cache + SQLite worker (own thread) + image cache provider
│   ├── media/            # MediaModel — QAbstractListModel with O(1) id lookup
│   ├── settings/         # QSettings wrapper
│   ├── server/           # Multi-server management, credential store
│   ├── playback/         # PlaybackController — orchestrates Emby ↔ mpv
│   ├── detail/           # Detail page data (series/season selection state)
│   └── library/          # Library browsing, search, favorites, pagination
├── platform/rendering/
│   ├── mpv/              # libmpv wrapper + QSGRenderNode-based render item (3 backends)
│   └── vulkandevice.*     # Custom VkDevice shared between Qt and mpv/libplacebo
├── ui/qml/
│   ├── pages/            # Top-level pages (Browse, Detail, Player)
│   ├── views/            # Sub-views (Home, Library, PlayerControls)
│   ├── components/       # Reusable components
│   ├── dialogs/          # Popup dialogs
│   ├── shaders/          # HDR tonemap (sRGB→PQ), rounded-corner mask
│   └── theme/            # Theme, strings, navigation
├── resources/            # Icons, fonts, app manifest
├── third_party/          # Prebuilt mpv headers (include/)
└── tools/                # Build and packaging scripts
```

Everything off the main thread is explicit: SQLite writes go through a dedicated DB worker thread, image decoding runs on `QThreadPool`, and all cross-thread results come back via `QueuedConnection` — no shared-state locking beyond a couple of narrowly-scoped mutexes in the render path.

## Dependencies

- Qt 6.11+ (Quick, Qml, QuickControls2, Network, OpenGL, Sql, ShaderTools, Vulkan)
- libmpv (fork with D3D11 render API + custom-VkDevice Vulkan support)
- libcurl (HTTP/2)
- D3D11 / Vulkan (Windows SDK)

## License

GPLv3 — see [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html).

## Credits

MfPlayer is meant to be a clean, fast Emby client for Windows. Built with ❤️ and a lot of late nights.
