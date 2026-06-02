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
            required property string imageUrl
            required property string itemName
            required property string year
            required property string itemId
            required property string itemType

            width: 150; height: 270
            radius: 6
            color: "transparent"

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6

                RoundedImage {
                    width: parent.width
                    height: 213
                    imgRadius: 6
                    embyUrl: Server.emby.imageUrl(imageUrl)
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
                anchors.fill: parent
                onClicked: root.itemClicked(itemId, itemType)
            }
        }
    }
}
