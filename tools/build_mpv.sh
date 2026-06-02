#!/bin/bash
# Build libmpv with D3D11 render API backend for mfplayer
# Run this in MSYS2 UCRT64 environment
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
MPV_SRC="$PROJECT_DIR/third_party/mpv-source"
OUTPUT_DIR="$PROJECT_DIR/third_party/mpv"

echo "=== Checking dependencies ==="
MISSING=""
command -v meson >/dev/null 2>&1 || MISSING="$MISSING mingw-w64-ucrt-x86_64-meson"
command -v ninja >/dev/null 2>&1 || MISSING="$MISSING mingw-w64-ucrt-x86_64-ninja"

if [ -n "$MISSING" ]; then
    echo "Missing packages:$MISSING"
    echo "Install with: pacman -S$MISSING"
    exit 1
fi

# MSYS2 UCRT pkg-config should find these automatically
echo "=== Checking key libraries ==="
pkg-config --exists libavcodec 2>/dev/null || { echo "Need ffmpeg: pacman -S mingw-w64-ucrt-x86_64-ffmpeg"; exit 1; }
pkg-config --exists libplacebo 2>/dev/null || { echo "Need libplacebo: pacman -S mingw-w64-ucrt-x86_64-libplacebo"; exit 1; }

echo "=== Configuring mpv ==="
cd "$MPV_SRC"

meson setup build \
    --prefix="$OUTPUT_DIR" \
    --buildtype=release \
    -Dlibmpv=true \
    -Ddefault_library=shared \
    -Dd3d11=enabled \
    -Dgpl=false \
    -Dcplugins=disabled \
    -Dtests=false \
    -Dlibavdevice=enabled \
    -Dmanpage-build=disabled

echo "=== Building libmpv ==="
ninja -C build libmpv-2.dll

echo "=== Installing ==="
# Copy DLL and import lib
cp -v build/libmpv-2.dll "$OUTPUT_DIR/lib/"
cp -v build/libmpv.dll.a "$OUTPUT_DIR/lib/"

# Collect all DLL dependencies so exe can run outside MSYS2
echo "=== Collecting DLL dependencies ==="
UCRT_BIN="/ucrt64/bin"
DEPLOY_DIR="$OUTPUT_DIR/lib/deps"
rm -rf "$DEPLOY_DIR"
mkdir -p "$DEPLOY_DIR"

# Known runtime deps of our self-built libmpv
DEPS="
    libplacebo-338.dll
    libshaderc_shared.dll
    libass-9.dll
    liblcms2-2.dll
    libavcodec-62.dll
    libavformat-62.dll
    libavutil-60.dll
    libswscale-9.dll
    libswresample-6.dll
    libavfilter-11.dll
    libcurl-4.dll
    libnghttp2-14.dll
    libnghttp3-9.dll
    libngtcp2-16.dll
    libngtcp2_crypto_ossl-0.dll
    libssl-3-x64.dll
    libcrypto-3-x64.dll
    libbrotlidec.dll
    libbrotlicommon.dll
    libzstd.dll
    libdovi-3.dll
    libunibreak-6.dll
    libuchardet.dll
    libfreetype-6.dll
    libharfbuzz-0.dll
    libfontconfig-1.dll
    libfribidi-0.dll
    libexpat-1.dll
    libgraphite2.dll
    libpcre2-8-0.dll
    libbz2-1.dll
    libpng16-16.dll
    libjpeg-62.dll
    libwebp-7.dll
    liblzma-5.dll
    libz.dll
    libiconv-2.dll
    libintl-8.dll
    libgcc_s_seh-1.dll
    libstdc++-6.dll
    libwinpthread-1.dll
"

echo "Copying DLL dependencies from $UCRT_BIN..."
copied=0
missing=0
for dll in $DEPS; do
    dll=$(echo "$dll" | tr -d '[:space:]')
    [ -z "$dll" ] && continue
    if [ -f "$UCRT_BIN/$dll" ]; then
        cp -v "$UCRT_BIN/$dll" "$DEPLOY_DIR/"
        copied=$((copied + 1))
    else
        echo "  (not found: $dll)"
        missing=$((missing + 1))
    fi
done
echo "Copied $copied DLLs, $missing not found (may be OK if unused)"

# Copy updated headers (including new render_d3d11.h)
for h in client.h render.h render_gl.h render_d3d11.h stream_cb.h; do
    if [ -f "include/mpv/$h" ]; then
        cp -v "include/mpv/$h" "$OUTPUT_DIR/include/mpv/"
    fi
done

echo "=== Done ==="
echo "libmpv with D3D11 render API installed to $OUTPUT_DIR"
echo "API version: $(grep MPV_CLIENT_API_VERSION include/mpv/client.h | head -1)"
