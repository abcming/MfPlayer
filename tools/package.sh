#!/bin/bash
# Package MfPlayer for Windows distribution (MSVC build)
# Run in Git Bash or Developer PowerShell after building
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"
DEPLOY_DIR="$PROJECT_DIR/deploy/MfPlayer"

# ── Pre-flight checks ──
EXE="$BUILD_DIR/MfPlayer.exe"
if [ ! -f "$EXE" ]; then
    echo "Error: $EXE not found. Build first:"
    echo "  cmake -G Ninja -S . -B build && cmake --build build"
    exit 1
fi

# Find windeployqt6
if command -v windeployqt6 >/dev/null 2>&1; then
    WINDEPLOYQT=windeployqt6
elif command -v windeployqt >/dev/null 2>&1; then
    WINDEPLOYQT=windeployqt
else
    echo "Error: windeployqt6 not found in PATH."
    echo "Add your Qt bin directory to PATH, e.g.:"
    echo "  export PATH=\"/c/Qt/6.11.0/msvc2022_64/bin:\$PATH\""
    exit 1
fi

echo "=== Packaging MfPlayer (MSVC) ==="

# ── Clean deploy dir ──
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"

# ── Copy exe ──
cp "$EXE" "$DEPLOY_DIR/"
echo "[1/5] MfPlayer.exe"

# ── Qt windeployqt6 ──
echo "[2/5] Collecting Qt dependencies..."
if command -v cygpath >/dev/null 2>&1; then
    WIN_EXE=$(cygpath -w "$DEPLOY_DIR/MfPlayer.exe")
    WIN_QMLDIR=$(cygpath -w "$PROJECT_DIR/ui/qml")
else
    WIN_EXE="$DEPLOY_DIR/MfPlayer.exe"
    WIN_QMLDIR="$PROJECT_DIR/ui/qml"
fi
"$WINDEPLOYQT" --qmldir "$WIN_QMLDIR" \
    --no-translations \
    --no-opengl-sw \
    "$WIN_EXE"

# ── qt.conf ──
cat > "$DEPLOY_DIR/qt.conf" << 'EOF'
[Paths]
Plugins = .
Imports = qml
Qml2Imports = qml
EOF

# ── Copy mfplayer QML plugin ──
QML_PLUGIN_DIR="$BUILD_DIR/mfplayer"
if [ -d "$QML_PLUGIN_DIR" ]; then
    mkdir -p "$DEPLOY_DIR/mfplayer"
    cp "$QML_PLUGIN_DIR/"*.dll "$DEPLOY_DIR/mfplayer/" 2>/dev/null || true
    cp "$QML_PLUGIN_DIR/qmldir" "$DEPLOY_DIR/mfplayer/" 2>/dev/null || true
    echo "  mfplayer QML plugin deployed"
else
    echo "  Warning: $QML_PLUGIN_DIR not found"
fi

# ── Copy libmpv + deps ──
echo "[3/5] Copying libmpv..."
MPV_DIR="$PROJECT_DIR/third_party/mpv-msvc"
cp "$MPV_DIR/bin/mpv-2.dll" "$DEPLOY_DIR/"
if [ -d "$MPV_DIR/lib/deps" ]; then
    cp "$MPV_DIR/lib/deps/"*.dll "$DEPLOY_DIR/"
    echo "  Copied $(ls "$MPV_DIR/lib/deps/"*.dll 2>/dev/null | wc -l) dependency DLLs"
fi

# ── MSVC runtime DLLs ──
echo "[4/5] Copying MSVC runtime..."
# Find vcredist DLLs from MSVC install (typically in VS install dir or Windows SDK)
# dumpbin /dependents on the exe will show which vcruntime/msvcp DLLs are needed
if command -v dumpbin >/dev/null 2>&1; then
    needed=$(dumpbin /dependents "$(cygpath -w "$DEPLOY_DIR/MfPlayer.exe" 2>/dev/null || echo "$DEPLOY_DIR/MfPlayer.exe")" 2>/dev/null \
        | grep -i 'VCRUNTIME\|MSVCP\|CONCRT' | tr -d ' ' || true)
    for dll in $needed; do
        # Search common MSVC redist locations
        for search in \
            "/c/Windows/System32" \
            "/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Redist/MSVC"/*/x64 \
            "/c/Program Files/Microsoft Visual Studio/2022/Professional/VC/Redist/MSVC"/*/x64 \
            "/c/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Redist/MSVC"/*/x64; do
            if [ -f "$search/Microsoft.VC143.CRT/$dll" ]; then
                cp "$search/Microsoft.VC143.CRT/$dll" "$DEPLOY_DIR/"
                echo "  + $dll"
                break
            fi
            if [ -f "$search/$dll" ]; then
                cp "$search/$dll" "$DEPLOY_DIR/"
                echo "  + $dll"
                break
            fi
        done
    done
fi

# windeployqt6 already copies the VC runtime DLLs that Qt was built with,
# but if your Qt was built with a different MSVC version, grab them manually:
# Look for VCRUNTIME140.dll / MSVCP140.dll in the deploy dir first
if [ -f "$DEPLOY_DIR/VCRUNTIME140.dll" ] || [ -f "$DEPLOY_DIR/VCRUNTIME140_1.dll" ]; then
    echo "  MSVC runtime already deployed by windeployqt6"
fi

# ── Copy fonts ──
echo "[5/5] Copying fonts..."
FONT_SRC="$PROJECT_DIR/resources/fonts"
if [ -d "$FONT_SRC" ]; then
    cp -r "$FONT_SRC" "$DEPLOY_DIR/fonts"
    echo "  $(find "$FONT_SRC" -name '*.ttc' -o -name '*.ttf' -o -name '*.otf' 2>/dev/null | wc -l) font files"
else
    echo "  Warning: $FONT_SRC not found"
fi

# ── Create zip ──
echo ""
echo "=== Creating archive ==="
cd "$PROJECT_DIR/deploy"
ZIP_NAME="MfPlayer-$(date +%Y%m%d).zip"
sleep 2  # let file handles close

if command -v 7z >/dev/null 2>&1; then
    7z a -tzip "$ZIP_NAME" MfPlayer/ >/dev/null
elif command -v zip >/dev/null 2>&1; then
    zip -r "$ZIP_NAME" MfPlayer/ -q
elif command -v tar >/dev/null 2>&1; then
    tar -a -c -f "$ZIP_NAME" MfPlayer/
else
    powershell -Command "Compress-Archive -Path MfPlayer -DestinationPath $ZIP_NAME -Force"
fi

TOTAL_SIZE=$(du -sh "$DEPLOY_DIR" 2>/dev/null | cut -f1 || echo "? MB")
echo ""
echo "=== Done ==="
echo "Package: $PROJECT_DIR/deploy/$ZIP_NAME"
echo "Size:   $TOTAL_SIZE"
echo ""
echo "To distribute: just extract the zip and run MfPlayer.exe."
