# Package MfPlayer for Windows distribution (MSVC build)
# Run in PowerShell after building:
#   .\tools\package.ps1

$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectDir = Resolve-Path "$ScriptDir\.."
$BuildDir = "$ProjectDir\build\release"
$DeployDir = "$ProjectDir\deploy\MfPlayer"
$AppVersion = (Get-Content "$ProjectDir\VERSION.txt" -TotalCount 1).Trim()

if ($AppVersion -notmatch '^\d+\.\d+\.\d+$') {
    Write-Error "Invalid application version in $ProjectDir\VERSION.txt: $AppVersion"
    exit 1
}

# ── Pre-flight checks ──
$Exe = "$BuildDir\MfPlayer.exe"
if (-not (Test-Path $Exe)) {
    Write-Error "MfPlayer.exe not found at $Exe. Build first: cmake --build $BuildDir"
    exit 1
}

# ── Find Qt (windeployqt6) ──
# Try PATH first, then auto-search common install locations
$Windeployqt = Get-Command windeployqt6 -ErrorAction SilentlyContinue
if (-not $Windeployqt) { $Windeployqt = Get-Command windeployqt -ErrorAction SilentlyContinue }

if (-not $Windeployqt) {
    # Auto-search: look in C:\Qt\ for the latest version
    $qtBase = "${env:ProgramFiles}\Qt"
    if (Test-Path "C:\Qt") { $qtBase = "C:\Qt" }
    $qtVersions = Get-ChildItem $qtBase -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+' } |
        Sort-Object { [version]$_.Name } -Descending
    foreach ($ver in $qtVersions) {
        foreach ($arch in @("msvc2022_64", "msvc2022_arm64", "mingw_64")) {
            $candidate = Join-Path $ver.FullName "$arch\bin\windeployqt6.exe"
            if (Test-Path $candidate) {
                $Windeployqt = $candidate
                break
            }
        }
        if ($Windeployqt) { break }
    }
}
if (-not $Windeployqt) {
    Write-Error @"
windeployqt6 not found in PATH or C:\Qt\.
Set `$env:PATH manually, e.g.:
  `$env:PATH = "C:\Qt\6.11.0\msvc2022_64\bin;`$env:PATH"
"@
    exit 1
}

# Normalize: if $Windeployqt is a string (path), wrap it; if it's a command object, use .Source
if ($Windeployqt -is [string]) {
    $WindeployqtPath = $Windeployqt
} else {
    $WindeployqtPath = $Windeployqt.Source
}

Write-Host "=== Packaging MfPlayer (MSVC) ===" -ForegroundColor Cyan
Write-Host "Version: $AppVersion"

# Qt root = bin/.. (e.g. C:\Qt\6.11.0\msvc2022_64)
$QtRoot = Split-Path -Parent (Split-Path -Parent $WindeployqtPath)
Write-Host "Qt root: $QtRoot"

# ── Clean deploy dir ──
if (Test-Path $DeployDir) { Remove-Item -Recurse -Force $DeployDir }
New-Item -ItemType Directory -Force $DeployDir | Out-Null

# ── Copy exe ──
Copy-Item $Exe $DeployDir
Write-Host "[1/7] MfPlayer.exe"

# ── windeployqt6 ──
Write-Host "[2/7] Collecting Qt dependencies..."
& $WindeployqtPath --release --qmldir "$ProjectDir\ui\qml" --no-translations --no-opengl-sw `
    --skip-plugin-types qmltooling `
    --exclude-plugins qsqlibase,qsqlmimer,qsqloci,qsqlodbc,qsqlpsql `
    "$DeployDir\MfPlayer.exe"
if ($LASTEXITCODE -ne 0) {
    Write-Error "windeployqt failed with exit code $LASTEXITCODE"
    exit 1
}

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
    Get-ChildItem $VectorDst -Recurse -Filter "*plugind.dll" | Remove-Item -Force
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
    Copy-Item "$SqlSrc\qsqlite.dll" $SqlDst
    Write-Host "  SQLite driver deployed"
} else {
    Write-Host "  ERROR: sqldrivers not found" -ForegroundColor Red
}

# ── Copy libmpv + deps ──
Write-Host "[3/7] Copying libmpv..."
$MpvDir = "$ProjectDir\third_party\mpv-msvc"
Copy-Item "$MpvDir\bin\mpv-2.dll" $DeployDir
if (Test-Path "$MpvDir\lib\deps") {
    Copy-Item "$MpvDir\lib\deps\*.dll" $DeployDir
    $count = (Get-ChildItem "$MpvDir\lib\deps\*.dll").Count
    Write-Host "  Copied $count dependency DLLs"
}

# ── MSVC runtime ──
Write-Host "[4/7] Checking MSVC runtime..."
$vcruntime = Get-ChildItem $DeployDir -Filter "VCRUNTIME*.dll" -ErrorAction SilentlyContinue
$msvcp = Get-ChildItem $DeployDir -Filter "MSVCP*.dll" -ErrorAction SilentlyContinue
if (-not $vcruntime -or -not $msvcp) {
    Write-Host "  MSVC runtime not deployed by windeployqt6, searching..."
    $vsRedist = "$env:VCToolsRedistDir"
    if (-not $vsRedist) {
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            $redistRoot = "$vsInstall\VC\Redist\MSVC"
            if (Test-Path $redistRoot) {
                $latest = Get-ChildItem $redistRoot -Directory |
                    Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
                    Sort-Object { [version]$_.Name } -Descending |
                    Select-Object -First 1
                $vsRedist = Get-ChildItem "$($latest.FullName)\x64" -Directory -ErrorAction SilentlyContinue |
                    Where-Object { Test-Path "$($_.FullName)\VCRUNTIME140.dll" } |
                    Select-Object -First 1 -ExpandProperty FullName
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
Write-Host "[5/7] Copying fonts..."
$FontSrc = "$ProjectDir\resources\fonts"
if (Test-Path $FontSrc) {
    Copy-Item -Recurse $FontSrc "$DeployDir\fonts"
    $fontCount = (Get-ChildItem "$FontSrc\*.ttc", "$FontSrc\*.ttf", "$FontSrc\*.otf" -ErrorAction SilentlyContinue).Count
    Write-Host "  $fontCount font files"
}

# ── Resolve missing Qt DLLs (transitive dependencies) ──
# After windeployqt6, scan every DLL in deploy with dumpbin and auto-copy
# any missing Qt dependency from the Qt install. Loop until stable.
Write-Host "[6/7] Resolving transitive dependencies..."
$dumpbin = Get-Command dumpbin -ErrorAction SilentlyContinue
if (-not $dumpbin) {
    if ($env:VCToolsInstallDir) {
        $candidate = Join-Path $env:VCToolsInstallDir "bin\Hostx64\x64\dumpbin.exe"
        if (Test-Path $candidate) { $dumpbin = $candidate }
    }
}
if (-not $dumpbin) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        $toolsRoot = "$vsInstall\VC\Tools\MSVC"
        if (Test-Path $toolsRoot) {
            $latestTools = Get-ChildItem $toolsRoot -Directory |
                Sort-Object { [version]$_.Name } -Descending |
                Select-Object -First 1
            $candidate = "$($latestTools.FullName)\bin\Hostx64\x64\dumpbin.exe"
            if (Test-Path $candidate) { $dumpbin = $candidate }
        }
    }
}
if ($dumpbin) {
    $dumpbinPath = if ($dumpbin -is [string]) { $dumpbin } else { $dumpbin.Source }
    Write-Host "  dumpbin: $dumpbinPath"

    # Build the Qt DLL lookup once instead of recursively searching Qt for every dependency.
    $qtDllIndex = @{}
    foreach ($root in @("$QtRoot\bin", "$QtRoot\plugins", "$QtRoot\qml")) {
        Get-ChildItem $root -Recurse -Filter "*.dll" -ErrorAction SilentlyContinue | ForEach-Object {
            $key = $_.Name.ToLowerInvariant()
            if (-not $qtDllIndex.ContainsKey($key)) { $qtDllIndex[$key] = $_.FullName }
        }
    }

    $pass = 0
    $anyCopied = $true
    while ($anyCopied -and $pass -lt 5) {
        $pass++
        $anyCopied = $false
        $allBin = Get-ChildItem $DeployDir -Recurse -Include *.dll,*.exe
        # Collect all known filenames in deploy
        $known = @{}
        foreach ($f in $allBin) { $known[$f.Name.ToLower()] = $true }
        foreach ($f in $allBin) {
            if ($f.Name -match '^(VCRUNTIME|MSVCP|CONCRT|api-ms-|ext-ms-)') { continue }
            $deps = & $dumpbinPath /dependents $f.FullName 2>$null |
                Select-String '^\s{4}(\S+\.dll)' |
                ForEach-Object { $_.Matches.Groups[1].Value.ToLower() }
            foreach ($dep in $deps) {
                # Skip Windows system DLLs
                if ($dep -match '^(kernel32|user32|gdi32|shell32|ole32|comdlg32|advapi32|ws2_32|d3d11|dxgi|dwmapi|d3dcompiler|opengl32|bcrypt|crypt32|secur32|ncrypt|ntdll|msvcrt|ucrtbase|combase|shlwapi|oleaut32|winmm|winhttp|mswsock|iphlpapi|dnsapi|powrprof|propsys|setupapi|cfgmgr32|rpcrt4|kernelbase|imm32|version|gdi32full|win32u|bcryptprimitives|userenv|wtsapi32|shcore|d2d1|dwrite|msimg32|windows\.storage|twinapi)') { continue }
                if ($known.ContainsKey($dep)) { continue }
                if ($qtDllIndex.ContainsKey($dep)) {
                    Copy-Item $qtDllIndex[$dep] $DeployDir
                    Write-Host "  + $dep  (needed by $($f.Name))"
                    $known[$dep] = $true
                    $anyCopied = $true
                }
            }
        }
    }
    Write-Host "  Dependency scan complete ($pass pass(es))"
} else {
    Write-Error "dumpbin.exe not found. Install the Visual Studio C++ tools or run this script from Developer PowerShell."
    exit 1
}

# ── Smoke test: check the things windeployqt6 might have missed ──
Write-Host "[7/7] Quick smoke test..."
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
    Write-Host "  CRITICAL: Required modules missing!" -ForegroundColor Red
}

# ── Compile installer (Inno Setup) ──
Write-Host ""
Write-Host "=== Compiling installer ===" -ForegroundColor Cyan

# Inno Setup doesn't add itself to PATH by default — find it
$Iscc = Get-Command iscc -ErrorAction SilentlyContinue
if (-not $Iscc) {
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe"
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $Iscc = $c; break }
    }
}

$IssFile = "$ScriptDir\installer.iss"
if ($Iscc -and (Test-Path $IssFile)) {
    $IsccPath = if ($Iscc -is [string]) { $Iscc } else { $Iscc.Source }
    & $IsccPath "/DMyAppVersion=$AppVersion" $IssFile
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Inno Setup failed with exit code $LASTEXITCODE"
        exit 1
    }
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
