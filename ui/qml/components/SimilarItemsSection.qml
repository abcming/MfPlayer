pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Column {
    id: root
    property bool isPerson: false
    property bool initialLoad: true
    property string itemType: ""
    signal itemClicked(string itemId, string itemType)

    width: parent.width
    spacing: 0

    HorizontalMediaRow {
        sectionTitle: Str.libTabMovies
        rowHeight: 280
        listModel: Detail.personMoviesModel
        delegate: similarDelegate
        extraVisibleCondition: root.isPerson
    }
    HorizontalMediaRow {
        sectionTitle: Str.libTabShows
        rowHeight: 280
        listModel: Detail.personSeriesModel
        delegate: similarDelegate
        extraVisibleCondition: root.isPerson
    }
    HorizontalMediaRow {
        sectionTitle: Str.detailSimilar
        rowHeight: 280
        listModel: Detail.similarModel
        delegate: similarDelegate
        extraVisibleCondition: !root.isPerson && !root.initialLoad && root.itemType !== Str.typeEpisode
    }

    Component {
        id: similarDelegate
        Rectangle {
            id: simCard
            required property string imageUrl
            required property string itemName
            required property string year
            required property string itemId
            required property string itemType
            required property var playbackPositionTicks   // 电影续播位置
            required property bool isFavorite
            required property bool played

            width: 150; height: 270
            radius: 6
            color: "transparent"

            HoverHandler { id: _simHover }

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6

                RoundedImage {
                    width: parent.width
                    height: 213
                    imgRadius: 6
                    lazyLoad: true
                    externalHover: _simHover.hovered
                    embyUrl: Server.emby.imageUrl(imageUrl)

                    // 封面正中的播放钮 —— 剧集会先问 NextUp 再起播
                    CardPlayButton {
                        visible: _simHover.hovered && Nav.isPlayable(simCard.itemType)
                        onClicked: Nav.playCard({
                            itemId: simCard.itemId,
                            itemName: simCard.itemName,
                            itemType: simCard.itemType,
                            startTicks: simCard.playbackPositionTicks || 0
                        })
                    }

                    CardActionButtons {
                        visible: _simHover.hovered && Nav.isPlayable(simCard.itemType)
                        itemId: simCard.itemId
                        isFavorite: simCard.isFavorite
                        played: simCard.played
                    }
                }

                Label {
                    text: itemName || "?"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    width: parent.width
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.Wrap
                }

                Label {
                    text: year || ""
                    color: Theme.textMuted
                    font.pixelSize: 11
                    visible: text !== ""
                }
            }

            MouseArea {
                // 沉到底 —— 它是最后声明的, 不压下去会盖住封面上的播放/收藏钮,
                // 点按钮直接穿透成"进详情页"。图片和文字本身不收鼠标事件,
                // 所以卡片空白处照样点得到这一层
                z: -1
                anchors.fill: parent
                onClicked: root.itemClicked(simCard.itemId, simCard.itemType)
            }
        }
    }
}
