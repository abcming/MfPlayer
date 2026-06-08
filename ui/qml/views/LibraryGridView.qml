pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GridView {
    id: grid

    reuseItems: true
    model: Library.contentModel

    clip: true
    interactive: false
    cellWidth: landscapeMode ? 240 : 160
    // Studios (5) and episodes (6) use landscape cards, movies/series use portrait posters
    property bool landscapeMode: Library.currentTab === 5 || Library.currentTab === 6
    cellHeight: landscapeMode ? 180 : 285
    // ~28 rows off-screen (5000px). Keeps ~224 delegates alive
    // to survive extreme scroll velocity (25 rows/frame @ 0.5s full-scroll).
    displayMarginBeginning: 5000
    displayMarginEnd: 5000
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

    // Infinite scroll with position anchoring.
    // Load-more fires when within 2000px of the bottom (~7 rows of portrait cards)
    // instead of 600px. This gives the network+model round-trip enough headroom so
    // contentHeight stabilises long before the user reaches the new content boundary.
    // Without this, the scrollbar thumb jumps because contentHeight ratio changes
    // during active scrolling — making it look like the scrollbar is "chasing" cards.
    property int _anchorIdx: -1

    Timer {
        id: scrollCheckTimer
        interval: 100
        onTriggered: {
            if (Library.canLoadMore && grid.contentY + grid.height > grid.contentHeight - 2000) {
                _anchorIdx = grid.indexAt(grid.contentX + grid.cellWidth / 2,
                                          grid.contentY + grid.cellHeight / 2)
                Library.loadMoreEpisodes()
            }
        }
    }
    onContentYChanged: {
        if (Library.canLoadMore && contentY + height > contentHeight - 2000)
            scrollCheckTimer.restart()
    }

    Connections {
        target: Library.contentModel
        function onRowsInserted() {
            if (_anchorIdx >= 0) {
                grid.positionViewAtIndex(_anchorIdx, GridView.Beginning)
                _anchorIdx = -1
            }
        }
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

    // ── Delegate: single-tree dynamic layout. Differences between landscape
    // (studios/episodes) and portrait (movies/series) are just sizes/spacing,
    // handled via ternary on grid.landscapeMode. One CachedImage, one ShaderEffect,
    // one HoverHandler per delegate — half the binding re-evaluation on recycle
    // compared to the dual-visible-tree approach.
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

        HoverHandler { id: _h }

        Column {
            anchors.fill: parent
            anchors.margins: grid.landscapeMode ? 6 : 4
            spacing: grid.landscapeMode ? 2 : 4

            RoundedImage {
                width: parent.width
                height: grid.landscapeMode ? 135 : 213
                imgRadius: grid.landscapeMode ? 4 : 8
                lazyLoad: true
                externalHover: _h.hovered
                embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
            }
            Label {
                text: grid.landscapeMode ? (seriesName || itemName || "?") : (itemName || "?")
                color: Theme.textPrimary; font.pixelSize: 12
                width: parent.width; elide: Text.ElideRight
                maximumLineCount: grid.landscapeMode ? 1 : 2
                wrapMode: grid.landscapeMode ? Text.NoWrap : Text.Wrap
            }
            Label {
                text: grid.landscapeMode ? (seriesName ? itemName : (year || "")) : (year || "")
                color: Theme.textMuted; font.pixelSize: 11
                elide: Text.ElideRight; width: parent.width; maximumLineCount: 1
                visible: text !== ""
            }
        }

        Row {
            id: actionRow
            anchors {
                top: parent.top
                right: parent.right
                margins: grid.landscapeMode ? 6 : 10
            }
            spacing: 4
            visible: _h.hovered && (grid.landscapeMode
                ? Library.currentTab === 6
                : (Library.currentTab !== 4 && Library.currentTab !== 5))
            z: 10
            Rectangle {
                width: 28; height: 28; radius: 14
                color: _fm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                Icon { anchors.centerIn: parent; name: isFavorite ? "heart_filled" : "heart"; color: isFavorite ? Theme.primary : Theme.textPrimary; size: 16 }
                MouseArea { id: _fm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (isFavorite) Detail.removeFavorite(itemId); else Detail.addFavorite(itemId) } }
            }
            Rectangle {
                width: 28; height: 28; radius: 14
                color: _pm.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                Icon { anchors.centerIn: parent; name: "check"; color: played ? Theme.primary : Theme.textPrimary; size: 16 }
                MouseArea { id: _pm; anchors.fill: parent; hoverEnabled: true; onClicked: { if (played) Detail.markUnplayed(itemId); else Detail.markPlayed(itemId) } }
            }
        }
    }
}
