pragma ComponentBehavior: Bound
import QtQuick

// 和 HomeView / LibraryGridView 手写的那套同一手感: 每格滚轮从当前位置
// 重起一段 OutCubic, 起步就是满速。别换回 SmoothedAnimation —— 它先加速后减速,
// 单独用时跑到一半改 to 也跟不上新目标, 滚起来发黏 (2026-10 设置页实测)
WheelHandler {
    id: root

    required property Flickable flickable
    property int orientation: Qt.Vertical
    property real stepSize: 100
    property real wheelTarget: 0

    property NumberAnimation scrollAnimation: NumberAnimation {
        target: root.flickable
        property: root.orientation === Qt.Vertical ? "contentY" : "contentX"
        duration: Theme.scrollAnimDuration
        easing.type: Easing.OutCubic
    }

    onWheel: (event) => {
        event.accepted = true

        const vertical = root.orientation === Qt.Vertical
        const current = vertical ? root.flickable.contentY : root.flickable.contentX
        const contentSize = vertical ? root.flickable.contentHeight : root.flickable.contentWidth
        const viewportSize = vertical ? root.flickable.height : root.flickable.width
        const maxScroll = Math.max(0, contentSize - viewportSize)

        // 连续滚轮不重置目标: 动画没在跑才从当前位置取起点, 跑着就继续累加
        if (!root.scrollAnimation.running)
            root.wheelTarget = current

        root.wheelTarget -= event.angleDelta.y / 120 * root.stepSize
        root.wheelTarget = Math.max(0, Math.min(maxScroll, root.wheelTarget))

        root.scrollAnimation.stop()
        root.scrollAnimation.from = current
        root.scrollAnimation.to = root.wheelTarget
        root.scrollAnimation.restart()
    }
}
