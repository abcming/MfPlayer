#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sdrWhiteNits;   // Windows SDR white level, typically 80–240
} ubuf;

layout(binding = 1) uniform sampler2D source;

// ── sRGB → Linear Rec.709 ──
vec3 srgbToLinear(vec3 srgb) {
    return mix(srgb / 12.92,
               pow((srgb + 0.055) / 1.055, vec3(2.4)),
               step(vec3(0.04045), srgb));
}

// ── Rec.709 primaries → Rec.2020 primaries ──
vec3 rec709ToRec2020(vec3 c) {
    return mat3(
        0.6274040, 0.0690970, 0.0163916,
        0.3292820, 0.9195400, 0.0880132,
        0.0433136, 0.0113612, 0.8955950
    ) * c;
}

// ── Linear → ST.2084 (PQ) encode ──
vec3 linearToPQ(vec3 x) {
    const float m1 = 2610.0 / 16384.0;
    const float m2 = 2523.0 / 4096.0 * 128.0;
    const float c1 = 3424.0 / 4096.0;
    const float c2 = 2413.0 / 4096.0 * 32.0;
    const float c3 = 2392.0 / 4096.0 * 32.0;
    vec3 xp = pow(max(x, vec3(0.0)), vec3(m1));
    return pow((vec3(c1) + c2 * xp) / (vec3(1.0) + c3 * xp), vec3(m2));
}

void main() {
    vec4 c = texture(source, qt_TexCoord0);

    // Skip unpremultiply: QML renders premultiplied-alpha, and ÷c.a
    // amplifies quantization noise on ClearType edges → color fringing.
    // Converting premultiplied RGB directly makes edges slightly darker
    // instead of tinted — visually indistinguishable for UI text.
    vec3 lin709  = srgbToLinear(c.rgb);
    vec3 lin2020 = rec709ToRec2020(lin709);
    vec3 nits    = lin2020 * ubuf.sdrWhiteNits;
    vec3 pq      = linearToPQ(nits / 10000.0);

    fragColor = vec4(pq, c.a) * ubuf.qt_Opacity;
}
