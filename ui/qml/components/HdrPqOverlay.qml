import QtQuick

// HdrPqOverlay — sRGB → PQ (ST.2084) correction shim for HDR10 swapchain.
//
// Uses contentItem.layer.effect (same mechanism as StyledPopup): Qt renders
// the subtree to an FBO first, then the PQ shader processes the baked RGBA
// pixels.  This avoids ClearType subpixel artifacts (red fringing) that
// ShaderEffectSource.live capture would produce.
//
// Auto-detects HDR status via the _hdrActive root context property.

Item {
    id: root
    implicitWidth: _content.width
    implicitHeight: _content.height

    property real sdrWhiteNits: 203
    readonly property bool hdrActive: typeof _hdrActive !== "undefined" && _hdrActive

    default property alias data: _content.data

    Item {
        id: _content
        anchors.fill: parent

        // Qt layer renders subtree to FBO → PQ shader corrects output.
        // No ShaderEffectSource — layer.effect gets the FBO texture directly
        // from Qt's internal pipeline (source is auto-provided).
        layer.enabled: root.hdrActive
        layer.format: ShaderEffectSource.RGBA16F
        layer.effect: ShaderEffect {
            property real sdrWhiteNits: root.sdrWhiteNits
            vertexShader: "qrc:/qt/qml/mfplayer/hdr_pq.vert.qsb"
            fragmentShader: "qrc:/qt/qml/mfplayer/hdr_pq.frag.qsb"
        }
    }
}
