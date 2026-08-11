pragma ComponentBehavior: Bound
import QtQuick

WheelHandler {
    id: root

    required property Flickable flickable
    property int orientation: Qt.Vertical
    property real stepSize: 100
    property real wheelTarget: 0

    property SmoothedAnimation scrollAnimation: SmoothedAnimation {
        target: root.flickable
        property: root.orientation === Qt.Vertical ? "contentY" : "contentX"
        duration: Theme.scrollAnimDuration
        velocity: -1
    }

    onWheel: (event) => {
        event.accepted = true

        const vertical = root.orientation === Qt.Vertical
        const current = vertical ? root.flickable.contentY : root.flickable.contentX
        const contentSize = vertical ? root.flickable.contentHeight : root.flickable.contentWidth
        const viewportSize = vertical ? root.flickable.height : root.flickable.width
        const maxScroll = Math.max(0, contentSize - viewportSize)

        if (!root.scrollAnimation.running)
            root.wheelTarget = current

        root.wheelTarget -= event.angleDelta.y / 120 * root.stepSize
        root.wheelTarget = Math.max(0, Math.min(maxScroll, root.wheelTarget))

        root.scrollAnimation.to = root.wheelTarget
        if (!root.scrollAnimation.running)
            root.scrollAnimation.start()
    }
}
