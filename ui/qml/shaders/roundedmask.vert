#version 440

layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 qt_TexCoord0;
layout(location = 1) out vec2 correctedUV;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float radius;
    vec4 bgColor;
    vec2 imgSize;     // source image natural size (must be before sourceSize)
    vec2 sourceSize;  // view/element size in physical pixels
};

void main() {
    qt_TexCoord0 = qt_MultiTexCoord0;

    // PreserveAspectCrop UV mapping -- computed once per vertex (was per pixel
    // in the fragment shader).  The corrected UV is a linear (affine) function
    // of the texture coordinate, so hardware perspective-correct interpolation
    // produces identical results to the per-pixel computation.
    correctedUV = qt_MultiTexCoord0;
    float imgAspect = imgSize.x / max(imgSize.y, 1.0);
    float viewAspect = sourceSize.x / max(sourceSize.y, 1.0);
    if (imgAspect > viewAspect) {
        float scale = viewAspect / imgAspect;
        correctedUV.x = (qt_MultiTexCoord0.x - 0.5) * scale + 0.5;
    } else {
        float scale = imgAspect / viewAspect;
        correctedUV.y = (qt_MultiTexCoord0.y - 0.5) * scale + 0.5;
    }

    gl_Position = qt_Matrix * qt_Vertex;
}