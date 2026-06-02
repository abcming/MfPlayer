import QtQuick
import QtQuick.Controls

Popup {
    id: root

    property int popupRadius: Theme.popupRadius
    property color bgColor: Theme.popupBg

    modal: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

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
}
