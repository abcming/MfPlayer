pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

// HdrPqLut — Generates the 256×2 LUT texture used by hdr_pq.frag.
//
// Row 0 (y≈0.25): sRGB → linear Rec.709 transfer  (was pow(…, 2.4))
// Row 1 (y≈0.75): linear → ST.2084 PQ encode       (was two pow() calls)
//
// Replaces 9 pow() calls per pixel with 6 hardware texture lookups.
// The texture is generated once at startup (live: false) and never changes.
//
// Usage: instantiate as a child of an Item that is NOT inside a layer FBO,
// then bind its texture to the ShaderEffect's lutTexture property:
//
//   HdrPqLut { id: lut; parent: root }   // parent outside layer FBO
//   ShaderEffect { property var lutTexture: lut; … }

ShaderEffectSource {
    id: root

    // Off-screen so the ShaderEffectSource is never visible in the scene.
    x: -10000; y: -10000
    width: 256; height: 2
    live: false
    hideSource: true

    sourceItem: Canvas {
        id: lutCanvas
        width: 256; height: 2
        Component.onCompleted: requestPaint()
        onPaint: {
            var ctx = getContext("2d")
            // Row 0 — sRGB → linear Rec.709
            for (var i = 0; i < 256; i++) {
                var srgb = i / 255.0
                var lin = srgb <= 0.04045
                    ? srgb / 12.92
                    : Math.pow((srgb + 0.055) / 1.055, 2.4)
                ctx.fillStyle = Qt.rgba(lin, lin, lin, 1.0)
                ctx.fillRect(i, 0, 1, 1)
            }
            // Row 1 — linear → ST.2084 (PQ)
            var m1 = 2610.0 / 16384.0
            var m2 = 2523.0 / 4096.0 * 128.0
            var c1 = 3424.0 / 4096.0
            var c2 = 2413.0 / 4096.0 * 32.0
            var c3 = 2392.0 / 4096.0 * 32.0
            for (var j = 0; j < 256; j++) {
                var x  = j / 255.0
                var xp = Math.pow(x, m1)
                var pq = Math.pow((c1 + c2 * xp) / (1.0 + c3 * xp), m2)
                ctx.fillStyle = Qt.rgba(pq, pq, pq, 1.0)
                ctx.fillRect(j, 1, 1, 1)
            }
        }
        onPainted: root.scheduleUpdate()
    }
}
