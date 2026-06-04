pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: root
    property var people: []
    signal personClicked(string personId)

    property var filteredModel: {
        var p = root.people || []
        var filtered = []
        for (var i = 0; i < p.length; i++) {
            var t = p[i].Type || ""
            if (t === "Actor" || t === "Director") {
                filtered.push(p[i])
                if (filtered.length >= 20) break
            }
        }
        return filtered
    }

    width: parent.width

    HorizontalMediaRow {
        sectionTitle: Str.detailCastAndCrew
        rowHeight: 230
        listModel: root.filteredModel
        delegate: castDelegate
        visible: root.people && root.people.length > 0
    }

    Component {
        id: castDelegate
        Rectangle {
            required property var modelData
            width: 120; height: 205
            radius: 6
            color: "transparent"

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                RoundedImage {
                    width: parent.width
                    height: 168
                    imgRadius: 6
                    embyUrl: {
                        var pid = modelData.Id || ""
                        var tag = modelData.PrimaryImageTag || ""
                        if (pid && tag)
                            return Server.emby.imageUrl("/emby/Items/" + pid + "/Images/Primary?maxWidth=200&quality=90&tag=" + tag)
                        return ""
                    }
                }

                Label {
                    text: modelData.Name || ""
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    width: parent.width
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    text: {
                        var t = modelData.Type || ""
                        if (t === "Actor" && modelData.Role)
                            return modelData.Role
                        return t || ""
                    }
                    color: Theme.textMuted
                    font.pixelSize: 10
                    width: parent.width
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    visible: text !== ""
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: root.personClicked(modelData.Id)
            }
        }
    }
}
