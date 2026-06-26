#version 440
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float sdrWhiteNits;   // Windows SDR white level, typically 80-240
} ubuf;

layout(binding = 1) uniform sampler2D source;

// -- sRGB -> Linear Rec.709 --
float srgbToLinear(float c) {
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

// -- Rec.709 primaries -> Rec.2020 primaries --
vec3 rec709ToRec2020(vec3 c) {
    return mat3(
        0.6274040, 0.0690970, 0.0163916,
        0.3292820, 0.9195400, 0.0880132,
        0.0433136, 0.0113612, 0.8955950
    ) * c;
}

// -- Linear -> ST.2084 (PQ) encode --
float linearToPq(float c) {
    float m1 = 2610.0 / 16384.0;
    float m2 = 2523.0 / 4096.0 * 128.0;
    float c1 = 3424.0 / 4096.0;
    float c2 = 2413.0 / 4096.0 * 32.0;
    float c3 = 2392.0 / 4096.0 * 32.0;
    float xp = pow(c, m1);
    return pow((c1 + c2 * xp) / (1.0 + c3 * xp), m2);
}

void main() {
    vec4 c = texture(source, qt_TexCoord0);

    // Skip unpremultiply: QML renders premultiplied-alpha, and dividing by
    // c.a amplifies quantization noise on ClearType edges -> color fringing.
    // Converting premultiplied RGB directly makes edges slightly darker
    // instead of tinted -- visually indistinguishable for UI text.
    vec3 lin709  = vec3(srgbToLinear(c.r),
                        srgbToLinear(c.g),
                        srgbToLinear(c.b));
    vec3 lin2020 = rec709ToRec2020(lin709);
    vec3 nits    = lin2020 * ubuf.sdrWhiteNits;
    vec3 pq      = vec3(linearToPq(nits.r / 10000.0),
                        linearToPq(nits.g / 10000.0),
                        linearToPq(nits.b / 10000.0));

    fragColor = vec4(pq, c.a) * ubuf.qt_Opacity;
}