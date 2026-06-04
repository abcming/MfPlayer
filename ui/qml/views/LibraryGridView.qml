pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: grid

    model: Library.contentModel

    clip: true
    interactive: false
    cellWidth: landscapeMode ? 240 : 160
    // Studios (5) and episodes (6) use landscape cards, movies/series use portrait posters
    property bool landscapeMode: Library.currentTab === 5 || Library.currentTab === 6
    cellHeight: landscapeMode ? 180 : 285
    displayMarginBeginning: 350
    displayMarginEnd: 350
    leftMargin: 0
    rightMargin: 42

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
        minimumSize: 0.08
    }

    WheelHandler {
        onWheel: (event) => {
            var target = grid.contentY - event.angleDelta.y * 1.5
            target = Math.max(0, Math.min(
                grid.contentHeight - grid.height, target))
            gridAnim.from = grid.contentY
            gridAnim.to = target
            gridAnim.restart()
        }
    }

    NumberAnimation {
        id: gridAnim
        target: grid
        property: "contentY"
        duration: Theme.scrollAnimDuration
        easing.type: Easing.OutCubic
    }

    // Infinite scroll: load more episodes when near bottom (debounced)
    Timer {
        id: scrollCheckTimer
        interval: 100
        onTriggered: {
            if (Library.canLoadMore && grid.contentY + grid.height > grid.contentHeight - 600)
                Library.loadMoreEpisodes()
        }
    }
    onContentYChanged: {
        if (Library.canLoadMore && contentY + height > contentHeight - 600)
            scrollCheckTimer.restart()
    }

    function resetScroll() {
        grid.contentY = 0
    }

    // ── A-Z letter index (inside right margin, left of scrollbar) ──
    Item {
        id: letterIndex
        visible: Library.currentTab !== 6 && Library.currentTab !== 7
        anchors {
            right: parent.right
            rightMargin: 12
            top: parent.top
            bottom: parent.bottom
        }
        width: 30
        z: 10

        property var alphaMap: ({})
        property string activeLetter: ""
        property int _lastRowCount: 0

        function buildAlphaMap() {
            // Incrementally maintained on the C++ side — just read the property.
            alphaMap = Library.contentModel.alphaIndex
            _lastRowCount = Library.contentModel.rowCount()
        }

        function extendAlphaMap() {
            // C++ MediaModel maintains alphaIndex incrementally in appendItems().
            // Just sync the latest value.
            alphaMap = Library.contentModel.alphaIndex
            _lastRowCount = Library.contentModel.rowCount()
        }

        Connections {
            target: Library.contentModel
            function onAlphaIndexChanged() { letterIndex.extendAlphaMap() }
            function onModelReset() { letterIndex.buildAlphaMap() }
        }

        Component.onCompleted: buildAlphaMap()

        property var letters: ["#","A","B","C","D","E","F","G","H","I","J","K","L","M","N","O","P","Q","R","S","T","U","V","W","X","Y","Z"]

        Flickable {
            anchors.fill: parent
            contentHeight: letterCol.implicitHeight
            clip: true
            interactive: false

            Column {
                id: letterCol
                width: parent.width
                spacing: 0

                Label {
                    width: parent.width
                    text: Str.homeLetterIndexAll
                    color: letterIndex.activeLetter === "" ? Theme.primary : Theme.textMuted
                    font.pixelSize: 11
                    font.bold: letterIndex.activeLetter === ""
                    horizontalAlignment: Text.AlignHCenter
                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            letterIndex.activeLetter = ""
                            grid.contentY = 0
                        }
                    }
                }

                Repeater {
                    model: letterIndex.letters
                    Label {
                        required property string modelData
                        width: letterIndex.width
                        text: modelData
                        color: {
                            let idx = letterIndex.alphaMap[modelData]
                            return idx !== undefined
                                ? (letterIndex.activeLetter === modelData ? Theme.primary : Theme.textSecondary)
                                : Theme.textDisabled
                        }
                        font.pixelSize: 11
                        font.bold: letterIndex.activeLetter === modelData
                        horizontalAlignment: Text.AlignHCenter
                        MouseArea {
                            anchors.fill: parent
                            onClicked: {
                                let idx = letterIndex.alphaMap[modelData]
                                if (idx !== undefined) {
                                    letterIndex.activeLetter = modelData
                                    grid.positionViewAtIndex(idx, GridView.Beginning)
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // ── Card components (defined once, reused by all delegates via Loader) ──

    Component {
        id: landscapeCard
        Rectangle {
            radius: 6
            color: "transparent"
            property string itemName: ""
            property string year: ""
            property string itemId: ""
            property string imageUrl: ""
            property bool isFavorite: false
            property bool played: false
            property string seriesName: ""

            HoverHandler { id: _lh }

            Column {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2

                RoundedImage {
                    width: parent.width
                    height: 135
                    imgRadius: 4
                    lazyLoad: true
                    externalHover: _lh.hovered
                    embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
                }
                Label {
                    text: seriesName || itemName || "?"
                    color: Theme.textPrimary; font.pixelSize: 12
                    width: parent.width; elide: Text.ElideRight; maximumLineCount: 1
                }
                Label {
                    text: seriesName ? itemName : (year || "")
                    color: Theme.textMuted; font.pixelSize: 11
                    width: parent.width; elide: Text.ElideRight; maximumLineCount: 1
                    visible: text !== ""
                }
            }

            Row {
                anchors { top: parent.top; right: parent.right; margins: 6 }
                spacing: 4
                visible: _lh.hovered && Library.currentTab === 6
                z: 10
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: _lfm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                    Icon { anchors.centerIn: parent; name: isFavorite ? "heart_filled" : "heart"; color: isFavorite ? Theme.primary : Theme.textPrimary; size: 16 }
                    MouseArea { id: _lfm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (isFavorite) Detail.removeFavorite(itemId); else Detail.addFavorite(itemId) } }
                }
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: _lpm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                    Icon { anchors.centerIn: parent; name: "check"; color: played ? Theme.primary : Theme.textPrimary; size: 16 }
                    MouseArea { id: _lpm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (played) Detail.markUnplayed(itemId); else Detail.markPlayed(itemId) } }
                }
            }
        }
    }

    Component {
        id: portraitCard
        Rectangle {
            radius: 6
            color: "transparent"
            property string itemName: ""
            property string year: ""
            property string itemId: ""
            property string imageUrl: ""
            property bool isFavorite: false
            property bool played: false
            property string seriesName: ""

            HoverHandler { id: _ph }

            Row {
                anchors { top: parent.top; right: parent.right; margins: 10 }
                spacing: 4
                visible: _ph.hovered && Library.currentTab !== 4 && Library.currentTab !== 5
                z: 10
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: _pfm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                    Icon { anchors.centerIn: parent; name: isFavorite ? "heart_filled" : "heart"; color: isFavorite ? Theme.primary : Theme.textPrimary; size: 16 }
                    MouseArea { id: _pfm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (isFavorite) Detail.removeFavorite(itemId); else Detail.addFavorite(itemId) } }
                }
                Rectangle {
                    width: 28; height: 28; radius: 14
                    color: _ppm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                    Icon { anchors.centerIn: parent; name: "check"; color: played ? Theme.primary : Theme.textPrimary; size: 16 }
                    MouseArea { id: _ppm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (played) Detail.markUnplayed(itemId); else Detail.markPlayed(itemId) } }
                }
            }

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                RoundedImage {
                    width: parent.width; height: 213
                    imgRadius: 8
                    lazyLoad: true; externalHover: _ph.hovered
                    embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
                }
                Label {
                    text: itemName || "?"
                    color: Theme.textPrimary; font.pixelSize: 12
                    elide: Text.ElideRight; width: parent.width
                    maximumLineCount: 2; wrapMode: Text.Wrap
                }
                Label {
                    text: year || ""
                    color: Theme.textMuted; font.pixelSize: 11
                    visible: text !== ""
                }
            }
        }
    }

    // ── Delegate: single Loader instead of dual branches ──
    delegate: Item {
        required property string imageUrl
        required property string itemName
        required property string year
        required property string itemType
        required property string itemId
        required property int index
        required property bool isFavorite
        required property bool played
        required property string seriesName

        width: grid.cellWidth - 10
        height: grid.cellHeight - 10

        MouseArea {
            anchors.fill: parent
            onClicked: {
                if (Library.currentTab === 5) { Library.browseStudio(itemId, itemName); return }
                if (Library.currentTab === 4) { Library.browseGenre(itemId, itemName); return }
                let type = itemType || ""
                if (type === Str.typeMovie || type === Str.typeSeries || type === Str.typeEpisode)
                    Nav.pushDetail(itemId)
                else { grid.contentY = 0; Detail.browseItem(itemId) }
            }
        }

        Loader {
            id: cardLoader
            anchors.fill: parent
            sourceComponent: grid.landscapeMode ? landscapeCard : portraitCard
            onItemChanged: {
                if (!cardLoader.item) return
                // Direct assignment for static properties (set once, never change).
                // Only isFavorite + played use Binding for live model-update reactivity.
                cardLoader.item.imageUrl = imageUrl
                cardLoader.item.itemName = itemName
                cardLoader.item.year = year
                cardLoader.item.itemId = itemId
                cardLoader.item.seriesName = seriesName
            }
            Binding { target: cardLoader.item; property: "isFavorite"; value: isFavorite }
            Binding { target: cardLoader.item; property: "played";     value: played     }
        }
    }
}
