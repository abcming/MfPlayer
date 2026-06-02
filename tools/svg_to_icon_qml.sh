#!/bin/bash
# Convert a Material Symbols SVG to a QML Shape component with dynamic color.
# Usage: svg_to_icon_qml.sh <input.svg> <output.qml>
set -euo pipefail

INPUT="$1"
OUTPUT="$2"

# Find svgtoqml: prefer PATH, fallback to Qt6 install prefix
SVGTOQML="svgtoqml"
if ! command -v svgtoqml &>/dev/null; then
    for candidate in \
        "${Qt6_DIR:-}/../../../bin/svgtoqml" \
        "${CMAKE_PREFIX_PATH:-}/bin/svgtoqml" \
        "/root/qt/6.11.0/gcc_64/bin/svgtoqml" \
        "/usr/lib/qt6/bin/svgtoqml" \
        "/usr/lib/x86_64-linux-gnu/qt6/bin/svgtoqml"; do
        if [ -x "$candidate" ]; then
            SVGTOQML="$candidate"
            break
        fi
    done
fi

# Convert SVG to QML Shape with GPU CurveRenderer (keep fill="white" for consistent output)
# Do NOT use -t (type name) — it causes recursive instantiation when the Shape
# element has the same type name as the enclosing QML file.
QT_QPA_PLATFORM=offscreen "$SVGTOQML" -c -p "$INPUT" "$OUTPUT"

# Replace hardcoded white or black fill with a dynamic color property
sed -i 's/fillColor: "#ffffffff"/fillColor: color/' "$OUTPUT"
sed -i 's/fillColor: "#ff000000"/fillColor: color/' "$OUTPUT"

# Add 'property color color: "white"' after the Item opening brace
sed -i '/^Item {$/a\    property color color: "white"' "$OUTPUT"
