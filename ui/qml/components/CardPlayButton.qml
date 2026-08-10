pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

// 卡片封面上的播放钮 —— hover 时出现在封面正中, 点了直接起播, 不用先进详情页。
// 用在「继续观看」/「收藏的集」(resumeCardDelegate) 和媒体库的「集」Tab。
//
// 放在封面正中而不是右上角: 右上角已经有收藏/已播两个钮, 再挤第三个既难点也难认。
Rectangle {
    id: root

    signal clicked()

    anchors.centerIn: parent
    width: 44
    height: 44
    radius: 22
    // 卡片自己的 MouseArea 铺满整张卡, 这里要盖在它上面才能先收到点击
    z: 20
    color: _hit.containsMouse ? Qt.rgba(1, 1, 1, 0.9) : Qt.rgba(0, 0, 0, 0.55)
    border { color: Qt.rgba(1, 1, 1, 0.75); width: 1 }

    // 淡入淡出, 免得 hover 时"啪"地跳出来
    opacity: visible ? 1 : 0
    Behavior on opacity { NumberAnimation { duration: 120 } }

    Icon {
        anchors.centerIn: parent
        // 图标本身偏左, 往右挪一点才在视觉上居中
        anchors.horizontalCenterOffset: 2
        name: "play_arrow"
        color: _hit.containsMouse ? Theme.panelDeep : Theme.textPrimary
        size: 22
    }

    MouseArea {
        id: _hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
