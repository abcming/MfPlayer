# MfPlayer

Emby desktop media player powered by Qt 6 QML + libmpv.

![Platform](https://img.shields.io/badge/platform-Windows-blue)
![License](https://img.shields.io/badge/license-GPLv3-green)
![Qt](https://img.shields.io/badge/Qt-6.8-green)

## Features

- **D3D11 hardware rendering** — libmpv renders directly into Qt's D3D11 render target, zero-copy
- **HDR support** — HDR10/HLG passthrough with adjustable peak brightness
- **Emby integration** — browse libraries, continue watching, search, favorites, multi-server login
- **Fuzzy subtitle matching** — Jaro-Winkler algorithm for automatic subtitle track selection
- **Customizable shortcuts** — all keyboard shortcuts can be remapped
- **Local playback** — drag & drop video files and external subtitles, auto-build playlists from folders
- **SQLite caching** — image and metadata cache for fast browsing

## Screenshots

> TODO: add screenshots

## Building (Windows, MSVC)

### Prerequisites

- Visual Studio 2022 (Community or above)
- Qt 6.8+ (MSVC 2022 64-bit)
- vcpkg
- Git

### Build mpv

```powershell
.\tools\build_mpv_msvc.ps1
```

This builds libmpv from source with D3D11 render API support (PR #17764) and installs vcpkg dependencies.

### Build MfPlayer

```powershell
cmake -G Ninja -S . -B build -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

### Package

```powershell
.\tools\package.ps1
```

Creates a portable zip in `deploy/`.

## Project Structure

```
├── app/              # Application entry point
├── core/             # Business logic (playback, library, detail, server, cache)
├── platform/         # Platform-specific rendering (mpv D3D11/OpenGL)
├── ui/qml/           # QML UI
│   ├── pages/        # Top-level pages (Browse, Detail, Player)
│   ├── views/        # Sub-views (Home, Library, PlayerControls)
│   ├── components/   # Reusable components
│   ├── dialogs/      # Popup dialogs
│   └── theme/        # Theme, strings, navigation
├── resources/        # Icons, fonts, app manifest
├── third_party/      # Prebuilt mpv headers (include/)
└── tools/            # Build and packaging scripts
```

## Dependencies

- Qt 6.8+ (Quick, Qml, QuickControls2, Network, OpenGL, Sql, ShaderTools)
- libmpv (D3D11 render API fork)
- libcurl (HTTP/2)
- D3D11 (Windows SDK)

## License

GPLv3 — see [LICENSE](LICENSE).

## Credits

MfPlayer is meant to be a clean, fast Emby client for Windows. Built with ❤️ and a lot of late nights.
