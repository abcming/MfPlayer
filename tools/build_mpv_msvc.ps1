# build_mpv_msvc.ps1 — Build libmpv with MSVC via vcpkg dependencies
# Run from: Developer PowerShell for VS 2022 (or equivalent MSVC environment)
# Usage: .\tools\build_mpv_msvc.ps1

$ErrorActionPreference = "Stop"
$env:VCPKG_MAX_CONCURRENCY = [Environment]::ProcessorCount
$ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$VcpkgRoot = $env:VCPKG_ROOT
if (-not $VcpkgRoot) { $VcpkgRoot = "D:/vcpkg" }
$MpvSource = "$ProjectRoot/third_party/mpv-source"
$BuildDir = "$MpvSource/build_msvc"
$InstallDir = "$ProjectRoot/third_party/mpv-msvc"
$PkgConfigDir = "$VcpkgRoot/installed/x64-windows/lib/pkgconfig"

Write-Host "=== Building libmpv with MSVC ===" -ForegroundColor Cyan
Write-Host "Project root: $ProjectRoot"
Write-Host "vcpkg root:   $VcpkgRoot"
Write-Host "MPV source:   $MpvSource"

# Step 1: Install vcpkg dependencies
Write-Host "`n=== Step 1: Installing vcpkg dependencies ===" -ForegroundColor Yellow
Push-Location $ProjectRoot
& "$VcpkgRoot/vcpkg" install --triplet x64-windows
if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install failed"; exit 1 }
Pop-Location

# Ensure pkg-config is available for meson
# vcpkg manifest mode installs to vcpkg_installed/ in the project root
$VcpkgInstalled = "$ProjectRoot/vcpkg_installed/x64-windows"
$PkgConfigDir = "$VcpkgInstalled/lib/pkgconfig"
$env:PKG_CONFIG_PATH = $PkgConfigDir
$PkgConfBin = "$VcpkgInstalled/tools/pkgconf/pkgconf.exe"
if (Test-Path $PkgConfBin) {
    $env:PKG_CONFIG = $PkgConfBin
    Write-Host "pkg-config: $PkgConfBin" -ForegroundColor Green
} else {
    Write-Warning "pkgconf.exe not found at $PkgConfBin — meson may fail to find dependencies"
}

# mpv expects spirv-cross-c-shared.pc but vcpkg installs spirv-cross-c.pc
# vcpkg's .pc is missing transitive deps, so we write a corrected one
$SpirvCrossSharedPc = "$PkgConfigDir/spirv-cross-c-shared.pc"
if (Test-Path $SpirvCrossSharedPc) { Remove-Item $SpirvCrossSharedPc -Force }
    # All spirv-cross libs listed directly (sub-library .pc files don't exist in vcpkg)
    $spirvPc = @"
prefix=`${pcfiledir}/../..
exec_prefix=`${prefix}
libdir=`${prefix}/lib
includedir=`${prefix}/include/spirv_cross

Name: spirv-cross-c-shared
Description: C API for SPIRV-Cross (all-in-one)
Version: 0.68.0
Libs: -L`${libdir} -lspirv-cross-c -lspirv-cross-core -lspirv-cross-glsl -lspirv-cross-hlsl -lspirv-cross-msl -lspirv-cross-reflect -lspirv-cross-util -lspirv-cross-cpp
Cflags: -I`${includedir}
"@
    [System.IO.File]::WriteAllText($SpirvCrossSharedPc, $spirvPc, [System.Text.UTF8Encoding]::new($false))
    Write-Host "Created spirv-cross-c-shared.pc with all libs in Libs:" -ForegroundColor Yellow

# Step 2: Check libplacebo (needs special handling)
Write-Host "`n=== Step 2: Checking libplacebo ===" -ForegroundColor Yellow
$PlaceboPc = "$PkgConfigDir/libplacebo.pc"
if (-not (Test-Path $PlaceboPc)) {
    Write-Host "libplacebo not found in vcpkg, building as subproject..." -ForegroundColor Red

    # Download libplacebo source as meson subproject
    $SubprojectsDir = "$MpvSource/subprojects"
    $PlaceboDir = "$SubprojectsDir/libplacebo"
    if (-not (Test-Path $PlaceboDir)) {
        Write-Host "Cloning libplacebo..."
        git clone --depth 1 --recurse-submodules https://code.videolan.org/videolan/libplacebo.git "$PlaceboDir"
    }
    # Create meson subproject wrap (UTF8 without BOM — meson chokes on BOM)
    $WrapFile = "$SubprojectsDir/libplacebo.wrap"
    if (Test-Path $WrapFile) { Remove-Item $WrapFile -Force }
    $wrapContent = "[wrap-git]`nurl = https://code.videolan.org/videolan/libplacebo.git`ndepth = 1`n`n[provide]`nlibplacebo = libplacebo_dep`n"
    [System.IO.File]::WriteAllText($WrapFile, $wrapContent, [System.Text.UTF8Encoding]::new($false))
} else {
    Write-Host "libplacebo found in vcpkg: $PlaceboPc" -ForegroundColor Green
}

# Step 3: Configure meson
Write-Host "`n=== Step 3: Configuring meson ===" -ForegroundColor Yellow
if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
# meson on Python 3.15 needs meson-info/ to exist before writing introspection
New-Item -ItemType Directory -Path "$BuildDir/meson-info" -Force | Out-Null

# Python 3.15 chokes on MSVC's non-UTF-8 output during compiler detection
# in Developer PowerShell. Tell Python to replace undecodable bytes.
$env:PYTHONIOENCODING = "utf-8:replace"

$MesonArgs = @(
    "setup", $BuildDir,
    "--prefix=$InstallDir",
    "--buildtype=release",
    "--pkg-config-path=$PkgConfigDir",
    "-Ddefault_library=shared",
    "-Dlibmpv=true",
    "-Dcplayer=false",
    "-Dd3d11=enabled",
    "-Dvulkan=enabled",
    "-Dlibplacebo:vulkan=enabled",
    "-Dcplugins=disabled",
    "-Dtests=false",
    "-Djavascript=disabled",
    "-Dlua=enabled",
    "-Dlibavdevice=enabled"
)

& meson @MesonArgs $MpvSource
if ($LASTEXITCODE -ne 0) {
    Write-Error "Meson setup failed. Check output above for missing dependencies."
    exit 1
}

# Step 4: Build
Write-Host "`n=== Step 4: Building mpv ===" -ForegroundColor Yellow
& ninja -C $BuildDir
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed"; exit 1 }

# Step 5: Install
Write-Host "`n=== Step 5: Installing ===" -ForegroundColor Yellow
& ninja -C $BuildDir install
if ($LASTEXITCODE -ne 0) { Write-Error "Install failed"; exit 1 }

# Step 6: Copy vcpkg runtime DLLs into install directory
Write-Host "`n=== Step 6: Deploying DLLs ===" -ForegroundColor Yellow

$DepsDir = "$InstallDir/lib/deps"
if (-not (Test-Path $DepsDir)) { New-Item -ItemType Directory -Path $DepsDir | Out-Null }
Copy-Item "$VcpkgInstalled/bin/*.dll" $DepsDir -Force

# Also deploy vulkan-1.dll from vcpkg installed or Vulkan SDK
# (vcpkg's vulkan package provides the loader; SDK provides it as fallback)
if (-not (Test-Path "$DepsDir/vulkan-1.dll")) {
    $VkDll = "$VcpkgInstalled/bin/vulkan-1.dll"
    if (-not (Test-Path $VkDll)) { $VkDll = "$env:VULKAN_SDK/Bin/vulkan-1.dll" }
    if (Test-Path $VkDll) {
        Copy-Item $VkDll $DepsDir -Force
        Write-Host "Deployed vulkan-1.dll" -ForegroundColor Green
    } else {
        Write-Host "vulkan-1.dll not found — system GPU driver should provide it" -ForegroundColor Yellow
    }
}

# Ensure updated render API headers are installed (meson may miss them)
$HeadersDir = "$InstallDir/include/mpv"
foreach ($h in @("render.h", "render_vulkan.h")) {
    $src = "$MpvSource/include/mpv/$h"
    if (Test-Path $src) {
        Copy-Item $src $HeadersDir -Force
        Write-Host "Copied $h to install" -ForegroundColor Green
    }
}

Write-Host "`n=== Done! ===" -ForegroundColor Green
Write-Host "mpv-2.dll: $InstallDir/bin/mpv-2.dll"
Write-Host "Import lib: $InstallDir/lib/mpv.lib"
Write-Host "Dependencies: $DepsDir/"
Write-Host "Backends: D3D11 + Vulkan"
Get-ChildItem "$InstallDir/bin/mpv-2.dll" | Select-Object Name, Length, LastWriteTime
