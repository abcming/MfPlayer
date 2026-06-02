#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radius;
    vec4 bgColor;
    vec2 imgSize;     // source image natural size (must be before sourceSize)
    vec2 sourceSize;  // view/element size in physical pixels
};

layout(binding = 1) uniform sampler2D source;

float roundedBox(vec2 p, vec2 b, float r) {
    vec2 d = abs(p) - b + vec2(r);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0)) - r;
}

void main() {
    // PreserveAspectCrop UV mapping
    vec2 uv = qt_TexCoord0;
    float imgAspect = imgSize.x / imgSize.y;
    float viewAspect = sourceSize.x / sourceSize.y;
    if (imgAspect > viewAspect) {
        float scale = viewAspect / imgAspect;
        uv.x = (uv.x - 0.5) * scale + 0.5;
    } else {
        float scale = imgAspect / viewAspect;
        uv.y = (uv.y - 0.5) * scale + 0.5;
    }

    // Rounded corners
    vec2 pixelCoord = qt_TexCoord0 * sourceSize;
    vec2 center = sourceSize * 0.5;
    float d = roundedBox(pixelCoord - center, center, radius);
    float alpha = 1.0 - smoothstep(-1.0, 1.0, d);
    vec4 tex = texture(source, uv);
    vec4 col = mix(bgColor, tex, tex.a);
    fragColor = col * alpha * qt_Opacity;
}
