pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls

Popup {
    id: root

    property int popupRadius: Theme.popupRadius
    property color bgColor: Theme.popupBg

    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property bool _hdr: typeof _hdrActive !== "undefined" && _hdrActive

    Overlay.modal: Rectangle {
        color: Theme.modalOverlay
        Behavior on opacity { NumberAnimation { duration: Theme.popupEnterDuration } }
    }

    enter: Transition {
        NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: Theme.popupEnterDuration; easing.type: Easing.OutCubic }
        NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: Theme.popupEnterDuration }
    }
    exit: Transition {
        NumberAnimation { property: "scale"; from: 1.0; to: 0.95; duration: Theme.popupExitDuration; easing.type: Easing.InCubic }
        NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: Theme.popupExitDuration }
    }

    background: Rectangle {
        radius: root.popupRadius
        color: root.bgColor
        border { color: Theme.active; width: 1 }
    }

    // Content item with HDR PQ correction via layer.effect.
    // Qt's layer system renders content to an FBO and applies the ShaderEffect
    // as a post-process.  The layer auto-provides 'source' — no property needed.
    //
    // implicitHeight walks children's implicit sizes so the Popup scales
    // correctly.  ColumnLayout/ListView compute their implicitHeight from
    // their own children independently of parent height — no cycle.
    contentItem: Item {
        layer.enabled: root._hdr
        layer.format: ShaderEffectSource.RGBA16F
        layer.effect: ShaderEffect {
            property real sdrWhiteNits: Server.settings.sdrWhiteNits
            vertexShader: "qrc:/qt/qml/mfplayer/hdr_pq.vert.qsb"
            fragmentShader: "qrc:/qt/qml/mfplayer/hdr_pq.frag.qsb"
        }

        implicitHeight: {
            var h = 0
            for (var i = 0; i < children.length; i++) {
                var c = children[i]
                if (!c.visible) continue
                var bh = c.y + (c.implicitHeight || 0)
                if (bh > h) h = bh
            }
            return h
        }
    }
}
