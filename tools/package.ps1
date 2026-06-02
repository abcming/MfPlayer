# Package MfPlayer for Windows distribution (MSVC build)
# Run in PowerShell after building:
#   .\tools\package.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path "$ScriptDir\.."
$BuildDir = "$ProjectDir\build"
$DeployDir = "$ProjectDir\deploy\MfPlayer"

# ── Pre-flight checks ──
$Exe = "$BuildDir\MfPlayer.exe"
if (-not (Test-Path $Exe)) {
    Write-Error "MfPlayer.exe not found at $Exe. Build first: cmake --build $BuildDir"
    exit 1
}

$Windeployqt = Get-Command windeployqt6 -ErrorAction SilentlyContinue
if (-not $Windeployqt) { $Windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue }
if (-not $Windeployqt) {
    Write-Error @"
windeployqt6 not found in PATH.
Add your Qt bin directory, e.g.:
  `$env:PATH = "C:\Qt\6.11.0\msvc2022_64\bin;`$env:PATH"
"@
    exit 1
}

Write-Host "=== Packaging MfPlayer (MSVC) ===" -ForegroundColor Cyan

# Qt root = bin/.. (e.g. C:\Qt\6.11.0\msvc2022_64)
$QtRoot = Split-Path -Parent (Split-Path -Parent $Windeployqt.Source)
Write-Host "Qt root: $QtRoot"

# ── Clean deploy dir ──
if (Test-Path $DeployDir) { Remove-Item -Recurse -Force $DeployDir }
New-Item -ItemType Directory -Force $DeployDir | Out-Null

# ── Copy exe ──
Copy-Item $Exe $DeployDir
Write-Host "[1/6] MfPlayer.exe"

# ── windeployqt6 ──
Write-Host "[2/6] Collecting Qt dependencies..."
& $Windeployqt.Source --qmldir "$ProjectDir\ui\qml" --no-translations --no-opengl-sw "$DeployDir\MfPlayer.exe"

# ── qt.conf ──
@"
[Paths]
Plugins = .
Imports = qml
Qml2Imports = qml
"@ | Out-File -Encoding ASCII "$DeployDir\qt.conf"

# ── Copy mfplayer QML plugin ──
$QmlPluginDir = "$BuildDir\mfplayer"
if (Test-Path $QmlPluginDir) {
    New-Item -ItemType Directory -Force "$DeployDir\mfplayer" | Out-Null
    Copy-Item "$QmlPluginDir\*.dll" "$DeployDir\mfplayer\" -ErrorAction SilentlyContinue
    Copy-Item "$QmlPluginDir\qmldir" "$DeployDir\mfplayer\" -ErrorAction SilentlyContinue
    Write-Host "  mfplayer QML plugin deployed"
}

# ── Copy QtQuick.VectorImage (MFIcon QML uses it, windeployqt6 doesn't copy it) ──
$VectorSrc = "$QtRoot\qml\QtQuick\VectorImage"
$VectorDst = "$DeployDir\qml\QtQuick\VectorImage"
if (Test-Path $VectorSrc) {
    Remove-Item -Recurse -Force $VectorDst -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $VectorDst | Out-Null
    Copy-Item -Recurse -Force "$VectorSrc\*" $VectorDst
    Write-Host "  QtQuick.VectorImage deployed"
} else {
    Write-Host "  ERROR: QtQuick.VectorImage not found at $VectorSrc" -ForegroundColor Red
}

# VectorImage plugin needs Qt6Svg.dll at runtime (SVG rendering),
# but windeployqt6 doesn't detect it because icons are pre-converted to QML Shape.
$QtSvgDll = "$QtRoot\bin\Qt6Svg.dll"
if (Test-Path $QtSvgDll) {
    Copy-Item $QtSvgDll $DeployDir
    Write-Host "  Qt6Svg.dll deployed"
}

# ── Copy Qt SQL plugin (C++ code uses SQLite, windeployqt6 can't detect C++ dependency) ──
$SqlSrc = "$QtRoot\plugins\sqldrivers"
$SqlDst = "$DeployDir\sqldrivers"
if (Test-Path $SqlSrc) {
    Remove-Item -Recurse -Force $SqlDst -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force $SqlDst | Out-Null
    Get-ChildItem "$SqlSrc\qsql*.dll" | Copy-Item -Destination $SqlDst
    Write-Host "  SQL drivers deployed: $((Get-ChildItem $SqlDst\*.dll).Count) files"
} else {
    Write-Host "  ERROR: sqldrivers not found" -ForegroundColor Red
}

# ── Copy libmpv + deps ──
Write-Host "[3/6] Copying libmpv..."
$MpvDir = "$ProjectDir\third_party\mpv-msvc"
Copy-Item "$MpvDir\bin\mpv-2.dll" $DeployDir
if (Test-Path "$MpvDir\lib\deps") {
    Copy-Item "$MpvDir\lib\deps\*.dll" $DeployDir
    $count = (Get-ChildItem "$MpvDir\lib\deps\*.dll").Count
    Write-Host "  Copied $count dependency DLLs"
}

# ── MSVC runtime ──
Write-Host "[4/6] Checking MSVC runtime..."
$vcruntime = Get-ChildItem $DeployDir -Filter "VCRUNTIME*.dll" -ErrorAction SilentlyContinue
$msvcp = Get-ChildItem $DeployDir -Filter "MSVCP*.dll" -ErrorAction SilentlyContinue
if (-not $vcruntime -or -not $msvcp) {
    Write-Host "  MSVC runtime not deployed by windeployqt6, searching..."
    $vsRedist = "$env:VCToolsRedistDir"
    if (-not $vsRedist) {
        foreach ($edition in @("Community", "Professional", "Enterprise")) {
            $candidate = "${env:ProgramFiles}\Microsoft Visual Studio\2022\$edition\VC\Redist\MSVC"
            if (Test-Path $candidate) {
                $latest = Get-ChildItem $candidate -Directory | Sort-Object Name -Descending | Select-Object -First 1
                $vsRedist = "$candidate\$($latest.Name)\x64\Microsoft.VC143.CRT"
                if (Test-Path "$vsRedist\VCRUNTIME140.dll") { break }
                $vsRedist = $null
            }
        }
    }
    if ($vsRedist -and (Test-Path $vsRedist)) {
        Get-ChildItem "$vsRedist\*.dll" | Where-Object { $_.Name -match '^(VCRUNTIME|MSVCP|CONCRT)' } | Copy-Item -Destination $DeployDir
        Write-Host "  Copied MSVC runtime from $vsRedist"
    }
} else {
    Write-Host "  MSVC runtime already present"
}

# ── Copy fonts ──
Write-Host "[5/6] Copying fonts..."
$FontSrc = "$ProjectDir\resources\fonts"
if (Test-Path $FontSrc) {
    Copy-Item -Recurse $FontSrc "$DeployDir\fonts"
    $fontCount = (Get-ChildItem "$FontSrc\*.ttc", "$FontSrc\*.ttf", "$FontSrc\*.otf" -ErrorAction SilentlyContinue).Count
    Write-Host "  $fontCount font files"
}

# ── Smoke test: check the things windeployqt6 might have missed ──
Write-Host "[6/6] Quick smoke test..."
$checks = @(
    @{Name="QML Controls"; Path="$DeployDir\qml\QtQuick\Controls"; MustExist=$true},
    @{Name="VectorImage"; Path="$DeployDir\qml\QtQuick\VectorImage"; MustExist=$true},
    @{Name="Qt6Svg.dll"; Path="$DeployDir\Qt6Svg.dll"; MustExist=$true},
    @{Name="SQL drivers"; Path="$DeployDir\sqldrivers"; MustExist=$true},
    @{Name="mfplayer plugin"; Path="$DeployDir\mfplayer"; MustExist=$true},
    @{Name="mpv-2.dll"; Path="$DeployDir\mpv-2.dll"; MustExist=$true}
)
$ok = $true
foreach ($c in $checks) {
    if (Test-Path $c.Path) {
        Write-Host "  OK  $($c.Name)"
    } else {
        $fg = if ($c.MustExist) { "Red" } else { "Yellow" }
        Write-Host "  MISS $($c.Name) - $($c.Path)" -ForegroundColor $fg
        if ($c.MustExist) { $ok = $false }
    }
}
if (-not $ok) {
    Write-Host ""
    Write-Host "  CRITICAL: Required modules missing! Do this first:" -ForegroundColor Red
    Write-Host "    cd $QtRoot\qml\QtQuick\VectorImage" -ForegroundColor Yellow
    Write-Host "    dir" -ForegroundColor Yellow
}

# ── Compile installer (Inno Setup) ──
Write-Host ""
Write-Host "=== Compiling installer ===" -ForegroundColor Cyan

# Inno Setup doesn't add itself to PATH by default — find it
$Iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $candidates = @(
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $Iscc = $c; break }
    }
}

$IssFile = "$ScriptDir\installer.iss"
if ($Iscc -and (Test-Path $IssFile)) {
    & $Iscc $IssFile
    $setup = Get-ChildItem "$ProjectDir\deploy\MfPlayer-*-setup.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if ($setup) {
        Write-Host ""
        Write-Host "=== All done ===" -ForegroundColor Green
        Write-Host "Installer: $($setup.FullName)"
        Write-Host ("Size:      {0:N1} MB" -f ($setup.Length / 1MB))
    }
} elseif (-not (Test-Path $IssFile)) {
    Write-Host "  Skipped: installer.iss not found" -ForegroundColor Yellow
} else {
    Write-Host "  Skipped: Inno Setup not found. Install: winget install InnoSetup" -ForegroundColor Yellow
    Write-Host "  Then run: iscc .\tools\installer.iss" -ForegroundColor Yellow
}
