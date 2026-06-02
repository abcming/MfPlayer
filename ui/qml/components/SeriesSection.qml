pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root

    property string itemId: ""
    property var itemData: ({})
    property int currentSeasonIdx: 0
    property int _seasonVersion: 0

    Connections {
        target: Detail
        function onSeasonsChanged() {
            root._seasonVersion++
        }
    }

    width: parent.width
    spacing: 12
    visible: (itemData.Type || "") === Str.typeSeries

    signal episodeClicked(string itemId, var playlistData)

    readonly property string _seriesPosterUrl: {
        if (!Server.emby) return ""
        let d = itemData
        let id = d.Id || ""
        if (!id) return ""
        let tag = (d.ImageTags || {}).Primary || ""
        if (tag) return Server.emby.imageUrl("/emby/Items/" + id + "/Images/Primary?maxWidth=360&quality=80&tag=" + tag)
        let bdTags = d.BackdropImageTags || []
        if (bdTags.length > 0) return Server.emby.imageUrl("/emby/Items/" + id + "/Images/Backdrop/0?tag=" + bdTags[0] + "&quality=80")
        return ""
    }

    property string _episodeFallbackUrl: {
        if (!Server.emby) return ""
        let d = itemData
        let parentId = d.Id || ""
        let bdTags = d.BackdropImageTags || []
        if (parentId && bdTags.length > 0)
            return Server.emby.imageUrl("/emby/Items/" + parentId + "/Images/Backdrop/0?tag=" + bdTags[0] + "&quality=80")
        let tag = (d.ImageTags || {}).Primary || ""
        if (parentId && tag)
            return Server.emby.imageUrl("/emby/Items/" + parentId + "/Images/Primary?maxWidth=320&quality=80&tag=" + tag)
        return ""
    }

    function buildPlaylistData() {
        var arr = []
        for (var i = 0; i < Detail.episodeModel.rowCount(); i++)
            arr.push(Detail.episodeModel.get(i))
        return arr
    }

    // Season selector
    Button {
        id: seasonBtn
        width: 220; height: 40

        background: Rectangle {
            radius: 6
            color: seasonBtn.hovered ? Theme.activeHover : Theme.active
            border { color: Theme.primary; width: 1 }
        }

        contentItem: RowLayout {
            spacing: 8
            Label {
                text: {
                    let _ = root._seasonVersion
                    let s = Detail.seasonModel.get(root.currentSeasonIdx)
                    return s && s.itemName ? s.itemName : Str.detailSelectSeason
                }
                color: Theme.textPrimary
                font.pixelSize: 14
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Icon {
                name: "chevron_right"
                color: Theme.primary
                size: 14
            }
        }

        onClicked: seasonPopup.open()
    }

    StyledPopup {
        id: seasonPopup
        x: seasonBtn.x + seasonBtn.width + 8
        y: seasonBtn.y
        width: Math.max(200, Math.min(seasonListView.count * 170 + 16, root.width - seasonBtn.width - 16))
        height: 270
        padding: 8
        bgColor: Theme.panel

        ListView {
            id: seasonListView
            anchors.fill: parent
            model: Detail.seasonModel
            orientation: ListView.Horizontal
            spacing: 10
            clip: true

            delegate: Item {
                required property string imageUrl
                required property string itemName
                required property int indexNumber
                required property string itemId
                required property int index

                width: 150
                height: 250

                Rectangle {
                    anchors.fill: parent
                    color: "transparent"
                    radius: 6

                    Column {
                        anchors.fill: parent
                        anchors.margins: 4
                        spacing: 4

                        RoundedImage {
                            width: parent.width
                            height: 210
                            embyUrl: imageUrl ? Server.emby.imageUrl(imageUrl) : root._seriesPosterUrl
                        }

                        Label {
                            text: itemName || Str.seasonLabel(indexNumber)
                            color: Theme.textPrimary
                            font.pixelSize: 12
                            width: parent.width
                            elide: Text.ElideRight
                            maximumLineCount: 2
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignHCenter
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            root.currentSeasonIdx = index
                            seasonPopup.close()
                            Detail.fetchEpisodes(root.itemId, itemId)
                        }
                    }
                }
            }
        }
    }

    // Episode list — horizontal scroll
    ListView {
        id: episodeListView
        Layout.fillWidth: true
        Layout.preferredHeight: 200
        model: Detail.episodeModel
        orientation: ListView.Horizontal
        clip: true
        spacing: 10
        visible: count > 0

        delegate: Rectangle {
            required property string imageUrl
            required property int indexNumber
            required property string itemName
            required property string itemId

            width: 240
            height: 180
            radius: 6
            color: "transparent"
            clip: true

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                RoundedImage {
                    width: parent.width
                    height: 135
                    imgRadius: 4
                    embyUrl: imageUrl
                        ? Server.emby.imageUrl(imageUrl)
                        : root._episodeFallbackUrl
                }

                RowLayout {
                    spacing: 6
                    Label {
                        text: "E" + (indexNumber || "")
                        color: Theme.primary
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Label {
                        text: itemName || "?"
                        color: Theme.textPrimary
                        font.pixelSize: 12
                        Layout.fillWidth: true
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    root.episodeClicked(itemId, root.buildPlaylistData())
                }
            }
        }
    }
}
