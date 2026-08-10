pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

// 卡片封面上的播放钮 —— hover 时出现在封面正中, 点了直接起播, 不用先进详情页。
// 用在「继续观看」/「收藏的集」(resumeCardDelegate) 和媒体库的「集」Tab。
//
// 放在封面正中而不是右上角: 右上角已经有收藏/已播两个钮, 再挤第三个既难点也难认。
// 配色和那两个小钮保持一致 (同样的底色/hover 色/图标色, 一样不描边), 只是尺寸大一号。
Rectangle {
    id: root

    signal clicked()

    anchors.centerIn: parent
    width: 44
    height: 44
    radius: width / 2
    // 卡片自己的 MouseArea 铺满整张卡, 这里要盖在它上面才能先收到点击
    z: 20
    color: _hit.containsMouse ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0, 0, 0, 0.45)

    Icon {
        anchors.centerIn: parent
        name: "play_arrow"
        color: Theme.textPrimary
        size: 30
    }

    MouseArea {
        id: _hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
