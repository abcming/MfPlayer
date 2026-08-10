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
    property real _savedScrollPos: 0

    Connections {
        target: Detail
        function onSeasonsChanged(seriesId) {
            if (seriesId !== root.itemId) return
            root._seasonVersion++
        }
    }

    width: parent.width
    spacing: 12
    visible: (itemData.Type || "") === Str.typeSeries

    signal episodeClicked(string itemId, var playlistData)
    signal seasonSelected(int idx, string seasonId)

    // Restore scroll position after model reset
    Connections {
        target: Detail.episodeModel
        function onModelReset() {
            if (root._savedScrollPos > 0)
                restoreTimer.start()
        }
    }
    Timer {
        id: restoreTimer
        interval: 0
        onTriggered: episodeListView.contentX = root._savedScrollPos
    }

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
        // Single C++ call avoids O(n) QML↔C++ boundary crossings
        return Detail.episodeModel.getAllItems()
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
                            lazyLoad: true
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
                            root._savedScrollPos = 0
                            root.seasonSelected(index, itemId)
                            seasonPopup.close()
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
        // 196(卡片) + 20(留白/滚动条余量)。卡片: 6*2 边距 + 135 图 + 4 间距 + 两行 12px 文字
        Layout.preferredHeight: 216
        model: Detail.episodeModel
        orientation: ListView.Horizontal
        clip: true
        spacing: 10
        visible: count > 0

        delegate: Rectangle {
            id: epCard
            required property string imageUrl
            required property int indexNumber
            required property string itemName
            required property string itemId
            required property string seriesId             // 播放钮补同季播放列表用
            required property string seasonId
            required property var playbackPositionTicks   // 续播位置
            required property bool isFavorite
            required property bool played

            width: 240
            height: 196   // 从 180 加高: 标题现在最多两行, 原来的 29px 只够一行
            radius: 6
            color: "transparent"

            HoverHandler { id: _epHover }

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 4

                RoundedImage {
                    width: parent.width
                    height: 135
                    imgRadius: 4
                    lazyLoad: true
                    externalHover: _epHover.hovered
                    embyUrl: imageUrl
                        ? Server.emby.imageUrl(imageUrl)
                        : root._episodeFallbackUrl

                    // 封面正中的播放钮 —— 点封面本身仍是进单集详情页, 不改原行为
                    CardPlayButton {
                        visible: _epHover.hovered
                        onClicked: Nav.playCard({
                            itemId: epCard.itemId,
                            itemName: epCard.itemName,
                            itemType: Str.typeEpisode,
                            seriesName: root.itemData.Name || "",
                            indexNumber: epCard.indexNumber,
                            startTicks: epCard.playbackPositionTicks || 0,
                            seriesId: epCard.seriesId,
                            seasonId: epCard.seasonId
                        })
                    }

                    CardActionButtons {
                        visible: _epHover.hovered
                        itemId: epCard.itemId
                        isFavorite: epCard.isFavorite
                        played: epCard.played
                    }
                }

                RowLayout {
                    // Column 不是 Layout, 不给子项分配宽度 —— 不写这行, RowLayout 会被
                    // 内容撑成无限宽, 里面的 Layout.fillWidth 就填了个无限值,
                    // maximumLineCount / wrapMode 全部失效, 标题直接溢出压到隔壁卡片
                    width: parent.width
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
                        // Wrap 而不是 WordWrap: 剧集名常常是整串文件名
                        // ("....2026.S01E02.2160p.WEB-DL..."), 里面没有空格,
                        // WordWrap 找不到断点就一行到底
                        wrapMode: Text.Wrap
                        elide: Text.ElideRight   // 超过两行给个省略号, 别硬裁
                    }
                }
            }

            MouseArea {
                // 同 SimilarItemsSection: 沉到底, 否则盖住封面上的播放/收藏钮
                z: -1
                anchors.fill: parent
                onClicked: {
                    root._savedScrollPos = episodeListView.contentX
                    root.episodeClicked(epCard.itemId, root.buildPlaylistData())
                }
            }
        }
    }
}
