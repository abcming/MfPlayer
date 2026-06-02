import QtQuick

Item {
    id: root

    property string embyUrl: ""
    property int imgRadius: 6
    property int fillMode: Image.PreserveAspectCrop
    property bool hovered: hoverArea.containsMouse || externalHover
    property bool externalHover: false
    property bool lazyLoad: false

    // Hidden texture source for ShaderEffect — Stretch mode, aspect ratio handled in shader
    CachedImage {
        id: sourceImage
        visible: false
        width: root.width
        height: root.height
        fillMode: Image.Stretch
        embyUrl: root.embyUrl
        asynchronous: true
        lazyLoad: root.lazyLoad
    }

    // Rounded-corner mask via shader — no per-item FBO (unlike OpacityMask)
    ShaderEffect {
        id: effect
        anchors.fill: parent
        property var source: sourceImage
        property real radius: root.imgRadius * Screen.devicePixelRatio
        property color bgColor: Theme.panelDeep
        // imgSize BEFORE sourceSize — order must match GLSL std140 layout
        property vector2d imgSize: Qt.vector2d(sourceImage.implicitWidth || 1,
                                                sourceImage.implicitHeight || 1)
        property vector2d sourceSize: Qt.vector2d(root.width * Screen.devicePixelRatio,
                                                   root.height * Screen.devicePixelRatio)
        fragmentShader: "qrc:/qt/qml/mfplayer/roundedmask.frag.qsb"
    }

    // Hover overlay (sits above shader output in scene graph)
    Rectangle {
        anchors.fill: parent
        radius: root.imgRadius
        color: "black"
        opacity: root.hovered ? 0.35 : 0.0
        Behavior on opacity { NumberAnimation { duration: 200 } }
    }

    // Hover tracking only
    MouseArea {
        id: hoverArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }
}
