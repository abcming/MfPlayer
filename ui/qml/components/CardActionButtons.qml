pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick

// 卡片右上角的收藏 / 已看两个小钮。
// 位置和配色跟主页、媒体库那几处卡片保持一致 —— 那边是各写各的一份,
// 这里只给新加的地方用, 没去改动已经好好工作的那四处。
//
// showPlayed: 人物之类没有「看过」概念的条目关掉它。
Row {
    id: root

    property string itemId: ""
    property bool isFavorite: false
    property bool played: false
    property bool showPlayed: true

    anchors { top: parent.top; right: parent.right; margins: 6 }
    spacing: 4
    // 卡片自己的 MouseArea 铺满整张卡, 这里要盖在它上面才能先收到点击
    z: 10

    Rectangle {
        width: 28; height: 28; radius: 14
        color: _favMa.containsMouse ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0, 0, 0, 0.45)

        Icon {
            anchors.centerIn: parent
            name: root.isFavorite ? "heart_filled" : "heart"
            color: root.isFavorite ? Theme.primary : Theme.textPrimary
            size: 16
        }

        MouseArea {
            id: _favMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (root.isFavorite) Detail.removeFavorite(root.itemId)
                else Detail.addFavorite(root.itemId)
            }
        }
    }

    Rectangle {
        width: 28; height: 28; radius: 14
        visible: root.showPlayed
        color: _playedMa.containsMouse ? Qt.rgba(1, 1, 1, 0.35) : Qt.rgba(0, 0, 0, 0.45)

        Icon {
            anchors.centerIn: parent
            name: "check"
            color: root.played ? Theme.primary : Theme.textPrimary
            size: 16
        }

        MouseArea {
            id: _playedMa
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (root.played) Detail.markUnplayed(root.itemId)
                else Detail.markPlayed(root.itemId)
            }
        }
    }
}
