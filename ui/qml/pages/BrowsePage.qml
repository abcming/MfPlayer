pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

HdrPqOverlay {
    id: browseRoot

    property string currentView: "home"
    property int contentCount: 0
    property bool showSuggestions: false
    property bool showSortFilter: currentView === "library"
        && Library.currentTab !== 1 && Library.currentTab !== 3
        && Library.currentTab !== 4 && Library.currentTab !== 5

    onVisibleChanged: {
        if (visible && currentView === "favorites") Library.fetchFavorites()
    }

    Connections {
        target: Library.contentModel
        function onModelReset() { contentCount = Library.contentModel.rowCount() }
        function onRowsInserted() { contentCount = Library.contentModel.rowCount() }
        function onRowsRemoved() { contentCount = Library.contentModel.rowCount() }
    }

    Connections {
        target: Library
        function onPersonBrowseStarted(name) {
            browseRoot.currentView = "library"
            browseRoot.showSuggestions = false
            libraryTabs.selectedIndex = 0
        }
    }

    Connections {
        target: Server
        function onPlayError(msg) {
            _lastError = msg || Str.playFailed
            errorToastTimer.restart()
        }
    }

    property string _lastError: ""

    Connections {
        target: Server
        function onLoggedOut() {
            browseRoot.currentView = "home"
            browseRoot.showSuggestions = false
            libraryTabs.selectedIndex = 0
        }
    }

    // ── Delegate Components (defined at root level so they resolve before use) ──
    property Component libCardDelegate: Component {
        Rectangle {
            required property string itemId
            required property string itemName
            required property string imageUrl
            width: 200; height: 135
            radius: 6
            color: "transparent"

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                RoundedImage {
                    width: parent.width
                    height: 113
                    imgRadius: 6
                    lazyLoad: true
                    embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
                }

                Label {
                    text: itemName || "?"
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    width: parent.width
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            MouseArea {
                anchors.fill: parent
                onClicked: {
                    browseRoot.currentView = "library"
                    browseRoot.showSuggestions = false
                    libraryTabs.selectedIndex = 0
                    mediaGrid.contentY = 0
                    Library.browseLibrary(itemId)
                }
            }
        }
    }

    property Component resumeCardDelegate: Component {
        Rectangle {
            required property string itemId
            required property string imageUrl
            required property string itemType
            required property string backdropUrl
            required property var runTimeTicks
            required property var playbackPositionTicks
            required property double playedPercentage
            required property bool isFavorite
            required property bool played
            required property string seriesName
            required property string itemName
            required property int indexNumber
            required property var year
            width: 240; height: 180
            radius: 6
            color: "transparent"

            HoverHandler { id: _resumeHover }

            MouseArea {
                anchors.fill: parent
                onClicked: Nav.pushDetail(itemId)
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 135

                    RoundedImage {
                        anchors.fill: parent
                        imgRadius: 4
                        lazyLoad: true
                        externalHover: _resumeHover.hovered
                        embyUrl: {
                            if (!Server.emby) return ""
                            if (itemType === Str.typeMovie && backdropUrl)
                                return Server.emby.imageUrl(backdropUrl + "&maxWidth=600&quality=80")
                            return Server.emby.imageUrl(imageUrl)
                        }
                    }

                    Rectangle {
                        anchors { left: parent.left; top: parent.top; margins: 4 }
                        width: remTimeLabel.implicitWidth + 10
                        height: 20
                        radius: 3
                        color: Theme.transparentOverlay
                        visible: remTimeLabel.text !== ""

                        Label {
                            id: remTimeLabel
                            anchors.centerIn: parent
                            text: Str.remainingTimeCompact(runTimeTicks, playbackPositionTicks)
                            color: Theme.textPrimary
                            font.pixelSize: 10
                        }
                    }

                    Rectangle {
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
                        height: 4
                        radius: 2
                        color: Theme.panelDeep
                        visible: (playedPercentage || 0) > 0

                        Rectangle {
                            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
                            width: parent.width * Math.min((playedPercentage || 0) / 100, 1)
                            radius: 2
                            color: Theme.primary
                        }
                    }

                    // Action buttons (top-right corner)
                    Row {
                        anchors { top: parent.top; right: parent.right; margins: 6 }
                        spacing: 4
                        visible: _resumeHover.hovered
                        z: 10

                        Rectangle {
                            width: 28; height: 28; radius: 14
                            color: _resumeFavMa.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                            Icon {
                                anchors.centerIn: parent
                                name: isFavorite ? "heart_filled" : "heart"
                                color: isFavorite ? Theme.primary : Theme.textPrimary
                                size: 16
                            }
                            MouseArea {
                                id: _resumeFavMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (isFavorite) Detail.removeFavorite(itemId)
                                    else Detail.addFavorite(itemId)
                                }
                            }
                        }

                        Rectangle {
                            width: 28; height: 28; radius: 14
                            color: _resumePlayMa.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                            Icon {
                                anchors.centerIn: parent
                                name: "check"
                                color: played ? Theme.primary : Theme.textPrimary
                                size: 16
                            }
                            MouseArea {
                                id: _resumePlayMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (played) Detail.markUnplayed(itemId)
                                    else Detail.markPlayed(itemId)
                                }
                            }
                        }
                    }

                }

                Label {
                    text: seriesName || itemName || "?"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    maximumLineCount: 1
                }

                Label {
                    text: {
                        let t = itemType || ""
                        if (t === Str.typeEpisode)
                            return Str.episodeShortLabel(indexNumber)
                        return year || ""
                    }
                    color: Theme.textMuted
                    font.pixelSize: 11
                    visible: text !== ""
                }
            }
        }
    }

    property Component latestCardDelegate: Component {
        Rectangle {
            required property string itemId
            required property string imageUrl
            required property bool isFavorite
            required property bool played
            required property string itemName
            required property string year
            width: 150; height: 270
            radius: 6
            color: "transparent"

            HoverHandler { id: _latestHover }

            // Card-level navigation (fills entire card)
            MouseArea {
                anchors.fill: parent
                onClicked: Nav.pushDetail(itemId)
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 6

                Item {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 213

                    RoundedImage {
                        anchors.fill: parent
                        imgRadius: 6
                        lazyLoad: true
                        externalHover: _latestHover.hovered
                        embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
                    }

                    // Action buttons (top-right corner)
                    Row {
                        anchors { top: parent.top; right: parent.right; margins: 6 }
                        spacing: 4
                        visible: _latestHover.hovered
                        z: 10

                        Rectangle {
                            width: 28; height: 28; radius: 14
                            color: _latestFavMa.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                            Icon {
                                anchors.centerIn: parent
                                name: isFavorite ? "heart_filled" : "heart"
                                color: isFavorite ? Theme.primary : Theme.textPrimary
                                size: 16
                            }
                            MouseArea {
                                id: _latestFavMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (isFavorite) Detail.removeFavorite(itemId)
                                    else Detail.addFavorite(itemId)
                                }
                            }
                        }

                        Rectangle {
                            width: 28; height: 28; radius: 14
                            color: _latestPlayMa.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                            Icon {
                                anchors.centerIn: parent
                                name: "check"
                                color: played ? Theme.primary : Theme.textPrimary
                                size: 16
                            }
                            MouseArea {
                                id: _latestPlayMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    if (played) Detail.markUnplayed(itemId)
                                    else Detail.markPlayed(itemId)
                                }
                            }
                        }
                    }

                }

                Label {
                    text: itemName || "?"
                    color: Theme.textPrimary
                    font.pixelSize: 12
                    Layout.fillWidth: true
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
        }
    }

    property Component personCardDelegate: Component {
        Rectangle {
            required property string itemId
            required property string imageUrl
            required property bool isFavorite
            required property string itemName
            width: 120; height: 205
            radius: 6
            color: "transparent"

            HoverHandler { id: _personHover }

            MouseArea {
                anchors.fill: parent
                onClicked: Nav.pushDetail(itemId)
            }

            Column {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4

                Item {
                    width: parent.width
                    height: 168

                    RoundedImage {
                        anchors.fill: parent
                        imgRadius: 6
                        lazyLoad: true
                        externalHover: _personHover.hovered
                        embyUrl: Server.emby ? Server.emby.imageUrl(imageUrl) : ""
                    }

                    // Favorite button (top-right corner)
                    Rectangle {
                        anchors { top: parent.top; right: parent.right; margins: 6 }
                        width: 28; height: 28; radius: 14
                        color: _personFavMa.containsMouse ? Qt.rgba(1,1,1,0.35) : Qt.rgba(0,0,0,0.45)
                        visible: _personHover.hovered
                        z: 10
                        Icon {
                            anchors.centerIn: parent
                            name: isFavorite ? "heart_filled" : "heart"
                            color: isFavorite ? Theme.primary : Theme.textPrimary
                            size: 16
                        }
                        MouseArea {
                            id: _personFavMa
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: {
                                if (isFavorite) Detail.removeFavorite(itemId)
                                else Detail.addFavorite(itemId)
                            }
                        }
                    }
                }

                Label {
                    text: itemName || "?"
                    color: Theme.textPrimary
                    font.pixelSize: 11
                    width: parent.width
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignHCenter
                    maximumLineCount: 1
                }
            }
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // Left: library tree
        Rectangle {
            Layout.preferredWidth: 200
            Layout.fillHeight: true
            color: Theme.panel
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 0
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 6

                Label {
                    text: Str.navBrowseMedia
                    font.bold: true
                    font.pixelSize: 16
                    color: Theme.primary
                }

                // Home entry — always first
                ItemDelegate {
                    id: homeEntry
                    Layout.fillWidth: true
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 6
                        Icon {
                            name: "home"
                            color: homeEntry.hovered || browseRoot.currentView === "home"
                                   ? Theme.primary : Theme.textSecondary
                            size: 20
                            Layout.rightMargin: -15
                        }
                        Label {
                            text: Str.navHome
                            color: homeEntry.hovered || browseRoot.currentView === "home"
                                   ? Theme.primary : Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }

                    background: Rectangle {
                        color: parent.hovered || browseRoot.currentView === "home"
                               ? Theme.active : "transparent"
                        radius: 4
                    }

                    onClicked: browseRoot.currentView = "home"
                }

                // Favorites entry
                ItemDelegate {
                    id: favEntry
                    Layout.fillWidth: true
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 6
                        Icon {
                            name: "heart"
                            color: favEntry.hovered || browseRoot.currentView === "favorites"
                                   ? Theme.primary : Theme.textSecondary
                            size: 20
                            Layout.rightMargin: -15
                        }
                        Label {
                            text: Str.navFavorites
                            color: favEntry.hovered || browseRoot.currentView === "favorites"
                                   ? Theme.primary : Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }

                    background: Rectangle {
                        color: parent.hovered || browseRoot.currentView === "favorites"
                               ? Theme.active : "transparent"
                        radius: 4
                    }

                    onClicked: {
                        browseRoot.currentView = "favorites"
                        Library.fetchFavorites()
                    }
                }

                // Search entry
                ItemDelegate {
                    id: searchEntry
                    Layout.fillWidth: true
                    hoverEnabled: true

                    contentItem: RowLayout {
                        spacing: 6
                        Icon {
                            name: "search"
                            color: searchEntry.hovered || browseRoot.currentView === "search"
                                   ? Theme.primary : Theme.textSecondary
                            size: 20
                            Layout.rightMargin: -15
                        }
                        Label {
                            text: Str.navSearch
                            color: searchEntry.hovered || browseRoot.currentView === "search"
                                   ? Theme.primary : Theme.textSecondary
                            font.pixelSize: 13
                        }
                    }

                    background: Rectangle {
                        color: parent.hovered || browseRoot.currentView === "search"
                               ? Theme.active : "transparent"
                        radius: 4
                    }

                    onClicked: {
                        browseRoot.currentView = "search"
                        Library.fetchRecommendations()
                    }
                }

                ListView {
                    id: libraryList
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    model: Library.libraryModel
                    clip: true
                    spacing: 2
                    interactive: false

                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        minimumSize: 0.08
                    }

                    SmoothScroller {
                        target: libraryList
                    }

                    delegate: ItemDelegate {
                        required property string itemId
                        required property string itemName
                        width: libraryList.width
                        text: itemName || "?"
                        hoverEnabled: true

                        contentItem: Text {
                            text: parent.text
                            color: parent.hovered ? Theme.primary : Theme.textSecondary
                            elide: Text.ElideRight
                        }

                        background: Rectangle {
                            color: parent.hovered || (Library.currentLibraryId === itemId)
                                   ? Theme.active : "transparent"
                            radius: 4
                        }

                        onClicked: {
                            browseRoot.currentView = "library"
                            browseRoot.showSuggestions = false
                            libraryTabs.selectedIndex = 0
                            mediaGrid.contentY = 0
                            Library.browseLibrary(itemId)
                        }
                    }
                }

                // User area — avatar + name, click to open server panel
                Button {
                    id: userBtn
                    Layout.fillWidth: true
                    Layout.topMargin: 8
                    Layout.preferredHeight: 44
                    flat: true

                    background: Rectangle {
                        radius: 6
                        color: userBtn.hovered || serverPanel.visible ? Theme.active : "transparent"
                    }

                    contentItem: RowLayout {
                        spacing: 8
                        Rectangle {
                            Layout.preferredWidth: 28
                            Layout.preferredHeight: 28
                            radius: 14
                            color: Server.embyConnected ? "transparent" : Theme.active

                            RoundedImage {
                                anchors.fill: parent
                                imgRadius: 14
                                lazyLoad: true
                                visible: Server.embyConnected
                                embyUrl: Server.emby && Server.emby.userId && Server.emby.accessToken
                                    ? Server.emby.imageUrl("/emby/Users/" + Server.emby.userId + "/Images/Primary?maxWidth=64&quality=90&api_key=" + Server.emby.accessToken)
                                    : ""
                            }

                            Icon {
                                anchors.centerIn: parent
                                name: "search"  // placeholder for person icon
                                color: Theme.textMuted
                                size: 16
                                visible: !Server.embyConnected
                            }
                        }
                        Label {
                            text: Server.embyConnected
                                ? (Server.settings.embyUsername || Str.svrDefaultUser)
                                : Str.svrNotLoggedIn
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Icon {
                            name: "chevron_right"
                            color: Theme.textMuted
                            size: 16
                        }
                    }

                    onClicked: serverPanel.open()
                }

                ServerPanel {
                    id: serverPanel
                    anchorTarget: userBtn
                    onRequestLoginDialog: Nav.openLogin()
                    onOpenFileDialog: fileDialog.open()
                }
            }
        }

        // Right: content area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.panel
            radius: 8

            ColumnLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 0
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                spacing: 8

                // Title
                RowLayout {
                    spacing: 8

                    Label {
                        text: {
                            if (browseRoot.currentView === "home") return Str.navHome
                            if (browseRoot.currentView === "favorites") return Str.navFavorites
                            if (browseRoot.currentView === "search") return Str.navSearch
                            for (var i = 0; i < Library.libraryModel.rowCount(); i++) {
                                var lib = Library.libraryModel.get(i)
                                if (lib.itemId === Library.currentLibraryId) return lib.itemName
                            }
                            return Str.navAllMedia
                        }
                        font.pixelSize: 20
                        font.bold: true
                        color: Theme.textPrimary
                    }

                    Label {
                        text: {
                            if (browseRoot.currentView !== "library") return ""
                            return browseRoot.contentCount > 0 ? browseRoot.contentCount : ""
                        }
                        font.pixelSize: 14
                        color: Theme.textMuted
                        visible: text !== ""
                    }

                    // Home page: global counts
                    Label {
                        text: {
                            if (browseRoot.currentView !== "home") return ""
                            var parts = []
                            if (Library.movieCount > 0) parts.push(Str.navMovieCount + " " + Library.movieCount)
                            if (Library.seriesCount > 0) parts.push(Str.navSeriesCount + " " + Library.seriesCount)
                            if (Library.episodeCount > 0) parts.push(Str.navEpisodeCount + " " + Library.episodeCount)
                            return parts.join("  ·  ")
                        }
                        font.pixelSize: 14
                        color: Theme.textMuted
                        visible: text !== ""
                    }

                    // Spacer — pushes sort/filter controls to the right
                    Item { Layout.fillWidth: true }

                    // ── Sort & Filter controls ──
                    Row {
                        visible: showSortFilter
                        spacing: 4

                        // Sort button (text label + icon)
                        Rectangle {
                            id: sortBtn
                            width: sortRow.implicitWidth + 16
                            height: 28
                            radius: 6
                            color: _sortMa.containsMouse ? Theme.activeHover : Theme.active

                            Row {
                                id: sortRow
                                anchors.centerIn: parent
                                spacing: 4

                                Label {
                                    text: [Str.sortByName, Str.sortByYear, Str.sortByRating, Str.sortByDateAdded, Str.sortByPlayed][Library.sortBy]
                                    font.pixelSize: 12
                                    color: Theme.textSecondary
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                                Icon {
                                    name: "sort"
                                    size: 16
                                    color: Theme.textMuted
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }

                            MouseArea {
                                id: _sortMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: sortPopup.visible ? sortPopup.close() : sortPopup.open()
                            }
                        }

                        // Sort direction toggle
                        Rectangle {
                            width: 28; height: 28; radius: 6
                            color: _dirMa.containsMouse ? Theme.activeHover : Theme.active

                            Label {
                                anchors.centerIn: parent
                                text: Library.sortAscending ? "↑" : "↓"
                                font.pixelSize: 14
                                color: Theme.textSecondary
                            }

                            MouseArea {
                                id: _dirMa
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: Library.sortAscending = !Library.sortAscending
                            }
                        }

                    }

                    // Sort dropdown popup
                    StyledPopup {
                        id: sortPopup
                        parent: sortBtn
                        x: parent.width - width
                        y: parent.height + 4
                        width: 200
                        modal: false
                        height: sortCol.implicitHeight + 16

                        Column {
                            id: sortCol
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 2

                            Repeater {
                                model: [
                                    {idx: 0, name: Str.sortByName},
                                    {idx: 1, name: Str.sortByYear},
                                    {idx: 2, name: Str.sortByRating},
                                    {idx: 3, name: Str.sortByDateAdded},
                                    {idx: 4, name: Str.sortByPlayed}
                                ]
                                delegate: Rectangle {
                                    required property var modelData
                                    required property int index
                                    width: sortCol.width
                                    height: 32
                                    radius: 4
                                    color: _sortOptMa.containsMouse ? Theme.activeHover
                                         : (modelData.idx === Library.sortBy ? Theme.active : "transparent")

                                    Label {
                                        anchors { left: parent.left; leftMargin: 8; verticalCenter: parent.verticalCenter }
                                        text: modelData.name
                                        font.pixelSize: 13
                                        color: modelData.idx === Library.sortBy ? Theme.primary : Theme.textPrimary
                                        font.bold: modelData.idx === Library.sortBy
                                    }

                                    MouseArea {
                                        id: _sortOptMa
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        onClicked: {
                                            Library.sortBy = modelData.idx
                                            sortPopup.close()
                                        }
                                    }
                                }
                            }
                        }
                    }

                }

                // ── Home view ──
                HomeView {
                    id: homeFlick
                    visible: browseRoot.currentView === "home"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    libCardDelegate: browseRoot.libCardDelegate
                    resumeCardDelegate: browseRoot.resumeCardDelegate
                    latestCardDelegate: browseRoot.latestCardDelegate
                }

                // ── Favorites view ──
                    Flickable {
                    id: favFlick
                    visible: browseRoot.currentView === "favorites"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: favCol.implicitHeight + 20
                    interactive: false

                    SmoothScroller {
                        target: favFlick
                    }

                    Column {
                        id: favCol
                        width: parent.width
                        spacing: 16

                        HorizontalMediaRow {
                            sectionTitle: Str.favSeries
                            rowHeight: 280
                            listModel: Library.favSeriesModel
                            delegate: browseRoot.latestCardDelegate
                        }

                        HorizontalMediaRow {
                            sectionTitle: Str.favEpisodes
                            rowHeight: 190
                            listModel: Library.favEpisodesModel
                            delegate: browseRoot.resumeCardDelegate
                        }

                        HorizontalMediaRow {
                            sectionTitle: Str.favMovies
                            rowHeight: 280
                            listModel: Library.favMoviesModel
                            delegate: browseRoot.latestCardDelegate
                        }

                        HorizontalMediaRow {
                            sectionTitle: Str.favPeople
                            rowHeight: 230
                            listModel: Library.favPeopleModel
                            delegate: browseRoot.personCardDelegate
                        }
                    }

                }

                // ── Search view ──
                ColumnLayout {
                    visible: browseRoot.currentView === "search"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8

                    // Search bar
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40

                        TextField {
                            id: searchField
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            width: 300
                            height: 36
                            placeholderText: Str.searchPlaceholder
                            placeholderTextColor: Theme.textMuted
                            color: Theme.textPrimary
                            font.pixelSize: 14

                            background: Rectangle {
                                radius: 8
                                color: Theme.panelDeep
                                border { color: searchField.activeFocus ? Theme.primary : Theme.active; width: 1 }
                            }

                            Timer {
                                id: searchDebounce
                                interval: 250
                                onTriggered: {
                                    if (searchField.text.length >= 2)
                                        Library.search(searchField.text)
                                    else if (searchField.text.length === 0)
                                        Library.fetchRecommendations()
                                }
                            }
                            onTextChanged: searchDebounce.restart()
                        }
                    }

                    // Search results (4 rows)
                        Flickable {
                        id: searchFlick
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentHeight: searchCol.implicitHeight + 20
                        interactive: false

                        SmoothScroller {
                            target: searchFlick
                        }

                        Column {
                            id: searchCol
                            width: parent.width
                            spacing: 16

                            // Recommendations (when no search text)
                            HorizontalMediaRow {
                                sectionTitle: Str.searchRecommend
                                rowHeight: 280
                                listModel: Library.recommendModel
                                delegate: browseRoot.latestCardDelegate
                                extraVisibleCondition: searchField.text.length < 2
                            }

                            HorizontalMediaRow {
                                sectionTitle: Str.searchSeries
                                rowHeight: 280
                                listModel: Library.searchSeriesModel
                                delegate: browseRoot.latestCardDelegate
                                extraVisibleCondition: searchField.text.length >= 2
                            }

                            HorizontalMediaRow {
                                sectionTitle: Str.searchMovies
                                rowHeight: 280
                                listModel: Library.searchMoviesModel
                                delegate: browseRoot.latestCardDelegate
                                extraVisibleCondition: searchField.text.length >= 2
                            }

                            HorizontalMediaRow {
                                sectionTitle: Str.searchPeople
                                rowHeight: 230
                                listModel: Library.searchPeopleModel
                                delegate: browseRoot.personCardDelegate
                                extraVisibleCondition: searchField.text.length >= 2
                            }
                        }

                        // Empty state
                        Label {
                            anchors.centerIn: parent
                            visible: searchCol.visibleChildren.length === 0
                            text: searchField.text.length >= 2 ? "未找到相关内容" : ""
                            color: Theme.textMuted
                            font.pixelSize: 14
                        }
                    }
                }

                // ── Library tab bar ──
                Row {
                    id: libraryTabs
                    visible: browseRoot.currentView === "library"
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? 36 : 0
                    spacing: 4
                    leftPadding: 0; rightPadding: 0

                    property var tvTabs: [
                        {name: Str.libTabShows, tab: 0},       // TabDefault
                        {name: Str.libTabSuggestions, tab: 1}, // TabSuggestions
                        {name: Str.libTabFavorites, tab: 3},   // TabFavorites
                        {name: Str.libTabGenres, tab: 4},      // TabGenres
                        {name: Str.libTabStudios, tab: 5},     // TabStudios
                        {name: Str.libTabEpisodes, tab: 6},    // TabEpisodes
                        {name: Str.libTabFolders, tab: 7}      // TabFolders
                    ]
                    property var movieTabs: [
                        {name: Str.libTabMovies, tab: 0},      // TabDefault
                        {name: Str.libTabSuggestions, tab: 1}, // TabSuggestions
                        {name: Str.libTabFavorites, tab: 3},   // TabFavorites
                        {name: Str.libTabGenres, tab: 4},      // TabGenres
                        {name: Str.libTabFolders, tab: 7}      // TabFolders
                    ]
                    property var currentTabs: Library.currentLibraryType === "movies" ? movieTabs : tvTabs
                    property int selectedIndex: 0

                    Repeater {
                        model: libraryTabs.currentTabs
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: tabLabel.implicitWidth + 20
                            height: 30
                            radius: 15
                            color: index === libraryTabs.selectedIndex ? Theme.active : "transparent"

                            Label {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: modelData.name
                                color: index === libraryTabs.selectedIndex ? Theme.textPrimary : Theme.textSecondary
                                font.pixelSize: 12
                            }

                            MouseArea {
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    libraryTabs.selectedIndex = index
                                    browseRoot.showSuggestions = (modelData.tab === 1)
                                    Library.setLibraryTab(modelData.tab)
                                }
                            }
                        }
                    }
                }

                // ── Suggestions tab view (library-level, horizontal rows) ──
                SuggestionsView {
                    id: suggestionsFlick
                    visible: browseRoot.showSuggestions && browseRoot.currentView === "library"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    resumeCardDelegate: browseRoot.resumeCardDelegate
                    latestCardDelegate: browseRoot.latestCardDelegate
                }

                // ── Favorites tab view (horizontal rows, same as sidebar favorites) ──
                Flickable {
                    id: favTabFlick
                    visible: browseRoot.currentView === "library" && Library.currentTab === 3
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    contentHeight: favTabCol.implicitHeight + 20
                    interactive: false

                    SmoothScroller {
                        target: favTabFlick
                    }

                    Column {
                        id: favTabCol
                        width: parent.width
                        spacing: 16

                        HorizontalMediaRow {
                            sectionTitle: Str.favSeries
                            rowHeight: 280
                            listModel: Library.favSeriesModel
                            delegate: browseRoot.latestCardDelegate
                        }

                        HorizontalMediaRow {
                            sectionTitle: Str.favEpisodes
                            rowHeight: 190
                            listModel: Library.favEpisodesModel
                            delegate: browseRoot.resumeCardDelegate
                        }

                        HorizontalMediaRow {
                            sectionTitle: Str.favMovies
                            rowHeight: 280
                            listModel: Library.favMoviesModel
                            delegate: browseRoot.latestCardDelegate
                        }
                    }
                }

                // ── Library grid view ──
                LibraryGridView {
                    id: mediaGrid
                    visible: browseRoot.currentView === "library" && !browseRoot.showSuggestions && Library.currentTab !== 3
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                }
            }

            // Favorites placeholder — centered in Flickable viewport
            Label {
                x: favFlick.x + (favFlick.width - width) / 2
                y: favFlick.y + (favFlick.height - height) / 2
                visible: favFlick.visible && _favCount === 0
                text: "暂无收藏内容"
                color: Theme.textMuted
                font.pixelSize: 14
            }
        }
    }

    property int _favCount: 0
    function _updateFavCount() {
        _favCount = Library.favSeriesModel.rowCount()
                 + Library.favEpisodesModel.rowCount()
                 + Library.favMoviesModel.rowCount()
                 + Library.favPeopleModel.rowCount()
    }
    Connections { target: Library.favSeriesModel;   function onModelReset() { _updateFavCount() } }
    Connections { target: Library.favEpisodesModel; function onModelReset() { _updateFavCount() } }
    Connections { target: Library.favMoviesModel;   function onModelReset() { _updateFavCount() } }
    Connections { target: Library.favPeopleModel;   function onModelReset() { _updateFavCount() } }

    // Back button (when browsing into a library)
    Button {
        text: Str.navBack
        visible: Nav.depth > 1
        anchors { left: parent.left; top: parent.top; margins: 16 }
        onClicked: Nav.pop()
    }

    FileDialog {
        id: fileDialog
        title: Str.fileDialogTitle
        nameFilters: [Str.fileFilterVideo, Str.fileFilterAll]
        onAccepted: {
            var path = selectedFile.toString().replace(/^file:\/{2,3}/, "")
            Nav.pushPlayer({
                localFile: path,
                episodeTitle: path.split('/').pop().split('\\').pop(),
                episodeIndex: -1,
                playlistData: [],
                itemType: "Local"
            }, true)
        }
    }

    // ── Error toast ──
    Timer { id: errorToastTimer; interval: 4000; onTriggered: _lastError = "" }
    Rectangle {
        anchors { bottom: parent.bottom; horizontalCenter: parent.horizontalCenter; bottomMargin: 20 }
        width: Math.min(errorText.implicitWidth + 32, parent.width - 40)
        height: 36
        radius: 8
        color: "#E0333333"
        visible: _lastError !== ""
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 200 } }
        Text {
            id: errorText
            anchors.centerIn: parent
            text: _lastError
            color: "#FFF28B82"
            font.pixelSize: 13
        }
    }

    // ── Video file drag-and-drop ──
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        z: 100  // above everything

        onDropped: function(drop) {
            if (!drop.hasUrls) return
            var videoExts = ["mp4","mkv","avi","mov","wmv","flv","webm",
                             "mpg","mpeg","m2ts","ts","m4v","3gp","ogv"]
            for (var i = 0; i < drop.urls.length; i++) {
                var path = drop.urls[i].toString().replace(/^file:\/{2,3}/, "")
                var ext = path.split('.').pop().toLowerCase()
                if (videoExts.indexOf(ext) >= 0) {
                    Nav.pushPlayer({
                        localFile: path,
                        episodeTitle: path.split('/').pop().split('\\').pop(),
                        episodeIndex: -1,
                        playlistData: [],
                        itemType: "Local"
                    }, true)
                    break  // only play the first valid video
                }
            }
        }
    }
}
