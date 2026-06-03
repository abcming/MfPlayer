pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls

Column {
    id: root
    property var studios: []
    signal studioClicked(string studioId, string studioName)

    width: parent.width
    spacing: 6
    visible: (root.studios || []).length > 0

    Label {
        text: Str.detailStudios
        color: Theme.primary
        font.pixelSize: 16
        font.bold: true
    }

    Row {
        spacing: 0
        Repeater {
            model: root.studios || []
            Row {
                required property int index
                required property var modelData
                Label {
                    visible: index > 0
                    text: " · "
                    color: Theme.textMuted
                    font.pixelSize: 12
                }
                Label {
                    id: nameLabel
                    text: modelData.Name || ""
                    color: hover.containsMouse ? Theme.primary : Theme.textMuted
                    font.pixelSize: 12
                    font.underline: hover.containsMouse
                    Behavior on color { ColorAnimation { duration: 150 } }

                    MouseArea {
                        id: hover
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.studioClicked(String(modelData.Id || ""), modelData.Name || "")
                    }
                }
            }
        }
    }
}
