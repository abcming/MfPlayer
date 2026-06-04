pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

HdrPqOverlay {
    id: detailRoot
    readonly property int kResumeThresholdTicks: 300000000

    // ── Solid base layer — always opaque, prevents old page bleeding through during push transition ──
    Rectangle {
        anchors.fill: parent
        color: Theme.panelDeep
    }

    // ── Backdrop wallpaper (CachedImage for auth header support) ──
    CachedImage {
        id: backdrop
        anchors.fill: parent
        fillMode: Image.PreserveAspectCrop
        opacity: status === Image.Ready ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: 1000; easing.type: Easing.OutCubic } }
        embyUrl: {
            let data = detailRoot.itemData
            if (!data || !data.Id) return ""
            let tags = data.BackdropImageTags || []
            let imgId = data.Id
            if (tags.length === 0 && data.Type === Str.typeEpisode) {
                tags = data.ParentBackdropImageTags || []
                imgId = data.ParentLogoItemId || data.SeriesId || ""
            }
            if (tags.length > 0 && imgId)
                return Server.emby.imageUrl("/emby/Items/" + imgId + "/Images/Backdrop/0?tag=" + tags[0] + "&quality=90")
            return ""
        }
    }

    // Dark scrim over backdrop for text readability
    Rectangle {
        anchors.fill: parent
        color: "#CC000000"
        visible: backdrop.embyUrl != ""
    }

    property string itemId: ""
    property string parentItemId: ""
    property var itemData: ({})
    property var episodePlaylist: []
    property bool _initialLoad: true
    property bool _isPerson: (itemData.Type || "") === Str.typePerson
    property bool _restoring: false
    property int currentSeasonIdx: 0
    property string selectedVideoId: ""
    property int selectedAudioIdx: -1
    property int selectedSubIdx: -2
    property int _dataVersion: 0

    readonly property string _logoUrl: {
        let _ = _initialLoad
        let d = itemData
        let ak = Server.emby ? Server.emby.accessToken : ""
        if (_initialLoad || !d || !d.Id || !ak) return ""
        let base = ""
        if (d.Type === Str.typeEpisode) {
            let tag = d.ParentLogoImageTag || ""
            let imgId = d.ParentLogoItemId || ""
            if (!tag || !imgId) return ""
            base = "/emby/Items/" + imgId + "/Images/Logo?tag=" + tag + "&quality=90"
        } else {
            if (!(d.ImageTags && d.ImageTags.Logo)) return ""
            base = "/emby/Items/" + d.Id + "/Images/Logo?quality=90"
        }
        return Server.emby.imageUrl(base + "&api_key=" + ak)
    }

    // Defer data fetch until after the 350ms StackView push animation completes,
    // avoiding texture upload / binding evaluation competing with the slide transition.
    Timer {
        id: fetchTimer
        interval: 400
        onTriggered: if (itemId) Detail.browseItem(itemId)
    }
    Component.onCompleted: fetchTimer.start()

    // When popped back to (waking up), refresh to pick up any playback progress.
    onVisibleChanged: {
        if (!visible) {
            _restoring = false
            return
        }
        if (!_initialLoad && itemId && !_restoring) {
            _restoring = true
            Detail.browseItem(itemId)
        }
    }

    Connections {
        target: Detail
        function onItemDetailReady(fetchedId, data) {
            if (fetchedId !== itemId) return
            detailRoot._restoring = false
            detailRoot.itemData = data
            detailRoot._initialLoad = false
            let t = itemData.Type || ""
            if (t === Str.typeSeries) {
                detailRoot.currentSeasonIdx = 0
                Detail.fetchSeasons(itemId)
            } else if (t === Str.typeEpisode) {
                let sid = itemData.SeriesId || ""
                let seId = itemData.SeasonId || itemData.ParentId || ""
                if (sid && seId) Detail.fetchEpisodes(sid, seId)
            }
            detailRoot.resetAudioSubDefaults()
        }
    }

    Connections {
        target: Detail
        function onSeasonsChanged() {
            if (!detailRoot.itemData || detailRoot.itemData.Id !== detailRoot.itemId) return
            detailRoot.currentSeasonIdx = 0
        }
    }

    Connections {
        target: Detail
        function onPlayedStatusChanged(itemId, played) {
            if (itemId !== detailRoot.itemId) return
            if (detailRoot.itemData.UserData) {
                detailRoot.itemData.UserData.Played = played
                detailRoot._dataVersion++
            }
        }
        function onFavoriteChanged(itemId, isFavorite) {
            if (itemId !== detailRoot.itemId) return
            if (detailRoot.itemData.UserData) {
                detailRoot.itemData.UserData.IsFavorite = isFavorite
                detailRoot._dataVersion++
            }
        }
    }

    function buildPlaylistData() {
        // Single C++ call avoids O(n) QML↔C++ boundary crossings
        return Detail.episodeModel.getAllItems()
    }

    // ── Shared push-to-player helper ──
    function pushPlayerPage(resumeTicks) {
        let t = itemData.Type || ""
        let playlist = episodePlaylist.length > 0 ? episodePlaylist
            : (Detail.episodeModel.rowCount() > 0 ? buildPlaylistData() : [])
        if ((t === Str.typeSeries || t === Str.typeEpisode) && playlist.length > 0) {
            let startIdx = 0
            if (t === Str.typeEpisode) {
                for (let i = 0; i < playlist.length; i++) {
                    if (playlist[i].itemId === itemId) { startIdx = i; break }
                }
            }
            let ep = playlist[startIdx]
            Nav.pushPlayer({
                itemId: ep.itemId,
                episodeTitle: itemData.SeriesName || itemData.Name || "",
                episodeSubtitle: Str.episodeFullLabel(ep.indexNumber, ep.itemName),
                episodeIndex: startIdx,
                playlistData: playlist,
                itemType: Str.typeEpisode,
                itemData: detailRoot.itemData,
                startTimeTicks: resumeTicks,
                mediaSourceId: detailRoot.selectedVideoId || "",
                audioIndex: detailRoot.selectedAudioIdx,
                subtitleIndex: detailRoot.selectedSubIdx
            })
        } else {
            Nav.pushPlayer({
                itemId: itemId,
                episodeTitle: itemData.Name || "",
                episodeSubtitle: "",
                episodeIndex: -1,
                playlistData: [{
                    itemId: itemId,
                    itemName: itemData.Name || "",
                    indexNumber: 0
                }],
                itemType: t,
                itemData: detailRoot.itemData,
                startTimeTicks: resumeTicks,
                mediaSourceId: detailRoot.selectedVideoId || "",
                audioIndex: detailRoot.selectedAudioIdx,
                subtitleIndex: detailRoot.selectedSubIdx
            })
        }
    }

    // Return the MediaStreams belonging to the currently selected video version.
    function mediaStreamsForSelectedVideo() {
        let sources = itemData.MediaSources || []
        let topStreams = itemData.MediaStreams || []
        if (sources.length <= 1) return topStreams
        let selId = selectedVideoId || sources[0].Id
        for (let i = 0; i < sources.length; i++) {
            if (sources[i].Id === selId) {
                let ms = sources[i].MediaStreams
                if (ms && ms.length > 0) return ms
            }
        }
        return topStreams
    }

    // Recompute default audio / subtitle from the selected video version.
    function resetAudioSubDefaults() {
        let streams = mediaStreamsForSelectedVideo()

        selectedAudioIdx = -1
        for (let i = 0; i < streams.length; i++) {
            if (streams[i].Type === "Audio" && streams[i].IsDefault) {
                selectedAudioIdx = streams[i].Index
                break
            }
        }
        if (selectedAudioIdx < 0) {
            for (let i = 0; i < streams.length; i++) {
                if (streams[i].Type === "Audio") {
                    selectedAudioIdx = streams[i].Index
                    break
                }
            }
        }

        selectedSubIdx = -2
        for (let i = 0; i < streams.length; i++) {
            if (streams[i].Type === "Subtitle" && streams[i].IsDefault) {
                selectedSubIdx = streams[i].Index
                break
            }
        }
        if (selectedSubIdx === -2) {
            for (let i = 0; i < streams.length; i++) {
                if (streams[i].Type === "Subtitle") {
                    selectedSubIdx = streams[i].Index
                    break
                }
            }
        }
    }

    Flickable {
        id: detailFlick
        anchors.fill: parent
        anchors.topMargin: 60
        contentHeight: detailCol.implicitHeight + 60
        clip: true
        interactive: false

        WheelHandler {
            onWheel: (event) => {
                var target = detailFlick.contentY - event.angleDelta.y * 1.5
                target = Math.max(0, Math.min(
                    detailFlick.contentHeight - detailFlick.height, target))
                detailAnim.from = detailFlick.contentY
                detailAnim.to = target
                detailAnim.restart()
            }
        }

        NumberAnimation {
            id: detailAnim
            target: detailFlick
            property: "contentY"
            duration: Theme.scrollAnimDuration
            easing.type: Easing.OutCubic
        }

        Column {
            id: detailCol
            width: parent.width - 40
            x: 20
            spacing: 16

            // ── Poster + Meta row ──
            RowLayout {
                width: parent.width
                spacing: 20
                visible: !detailRoot._initialLoad

                // Poster
                RoundedImage {
                    Layout.preferredWidth: (itemData.Type || "") === Str.typeEpisode ? 360 : 180
                    Layout.preferredHeight: (itemData.Type || "") === Str.typeEpisode ? 202 : 270
                    imgRadius: 8
                    embyUrl: {
                        let img = itemData.Id || ""
                        let tag = ""
                        let tags = itemData.ImageTags
                        if (tags) tag = tags.Primary || ""
                        let sizeParam = (itemData.Type || "") === Str.typeEpisode
                            ? "maxWidth=500&quality=90"
                            : "maxHeight=400&quality=90"
                        if (tag)
                            return Server.emby.imageUrl("/emby/Items/" + img + "/Images/Primary?" + sizeParam + "&tag=" + tag)
                        // Fallback: backdrop
                        let bdTags = itemData.BackdropImageTags
                        if (!bdTags || !bdTags.length) {
                            bdTags = itemData.ParentBackdropImageTags
                            img = itemData.ParentBackdropItemId || itemData.SeriesId || ""
                        }
                        if (img && bdTags && bdTags.length > 0)
                            return Server.emby.imageUrl("/emby/Items/" + img + "/Images/Backdrop/0?tag=" + bdTags[0] + "&quality=90")
                        // Last resort: series primary (episodes only)
                        if ((itemData.Type || "") === Str.typeEpisode) {
                            let seriesId = itemData.SeriesId || ""
                            let seriesTag = itemData.SeriesPrimaryImageTag || ""
                            if (seriesId && seriesTag)
                                return Server.emby.imageUrl("/emby/Items/" + seriesId + "/Images/Primary?" + sizeParam + "&tag=" + seriesTag)
                        }
                        return ""
                    }
                }

                // Meta
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: 8

                    // Title
                    Label {
                        text: itemData.Name || ""
                        color: Theme.textPrimary
                        font.pixelSize: 22
                        font.bold: true
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        maximumLineCount: 2
                        wrapMode: Text.WordWrap
                    }

                    // Rating · Year · Runtime · Genres
                    Row {
                        spacing: 12
                        RowLayout {
                            spacing: 2
                            visible: itemData.CommunityRating > 0
                            Icon {
                                name: "star"
                                color: Theme.star
                                size: 16
                            }
                            Label {
                                text: Number(itemData.CommunityRating).toFixed(1)
                                color: Theme.star
                                font.pixelSize: 14
                            }
                        }
                        Label {
                            text: {
                                let py = itemData.ProductionYear || 0
                                if (!py) return ""
                                let ed = itemData.EndDate || ""
                                if (ed) {
                                    let ey = parseInt(ed.substring(0, 4))
                                    return ey > py ? py + " - " + ey : String(py)
                                }
                                return (itemData.Status || "") === "Continuing" ? py + Str.detailYearOngoing : String(py)
                            }
                            color: Theme.textTertiary
                            font.pixelSize: 14
                            visible: !detailRoot._isPerson
                        }
                        Label {
                            property int mins: Math.round(((itemData.RunTimeTicks || 0) / 600000000))
                            text: Str.runtimeLabel(mins)
                            color: Theme.textTertiary
                            font.pixelSize: 14
                            visible: text !== ""
                        }
                        Label {
                            text: {
                                let g = itemData.Genres || []
                                let names = []
                                for (let i = 0; i < g.length; i++) names.push(g[i])
                                return names.join(" · ")
                            }
                            color: Theme.textMuted
                            font.pixelSize: 14
                            visible: text !== ""
                        }
                    }

                    // Video summary + track selectors on one row
                    RowLayout {
                        spacing: 8
                        Layout.fillWidth: true
                        visible: !detailRoot._isPerson
                              && ((itemData.MediaSources || []).length > 0
                              || (itemData.MediaStreams || []).length > 0)

                        Label {
                            text: {
                                let streams = detailRoot.mediaStreamsForSelectedVideo()
                                for (let i = 0; i < streams.length; i++) {
                                    if (streams[i].Type === "Video") {
                                        let dt = streams[i].DisplayTitle || ""
                                        return Str.videoDisplayLabel(dt)
                                    }
                                }
                                return ""
                            }
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            visible: text !== ""
                        }

                        TrackSelector {
                            id: videoSel
                            label: Str.trackLabelVideo
                            streamType: "Video"
                            mediaStreams: itemData.MediaStreams || []
                            mediaSources: itemData.MediaSources || []
                            selectedVideoId: detailRoot.selectedVideoId
                            onVideoSelected: function(id) {
                                detailRoot.selectedVideoId = id
                                detailRoot.resetAudioSubDefaults()
                            }
                        }

                        TrackSelector {
                            id: audioSel
                            label: Str.trackLabelAudio
                            streamType: "Audio"
                            mediaStreams: detailRoot.mediaStreamsForSelectedVideo()
                            mediaSources: []
                            selectedAudioIdx: detailRoot.selectedAudioIdx
                            onAudioSelected: function(idx) { detailRoot.selectedAudioIdx = idx }
                        }

                        TrackSelector {
                            id: subSel
                            label: Str.trackLabelSubtitle
                            streamType: "Subtitle"
                            mediaStreams: detailRoot.mediaStreamsForSelectedVideo()
                            mediaSources: []
                            selectedSubIdx: detailRoot.selectedSubIdx
                            onSubtitleSelected: function(idx) { detailRoot.selectedSubIdx = idx }
                        }
                    }

                    // Play controls: play button, next-up, progress bar
                    PlayControlsRow {
                        visible: !detailRoot._isPerson
                        itemData: detailRoot.itemData
                        itemId: detailRoot.itemId
                        dataVersion: detailRoot._dataVersion
                        selectedAudioIdx: detailRoot.selectedAudioIdx
                        selectedSubIdx: detailRoot.selectedSubIdx
                        onPlayRequested: function(resumeTicks) {
                            pushPlayerPage(resumeTicks)
                        }
                        onRestartRequested: pushPlayerPage(0)
                        onTogglePlayed: {
                            let ud = itemData.UserData || {}
                            let id = itemData.Id || itemId
                            if (!id) return
                            if (ud.Played) {
                                Detail.markUnplayed(id)
                                itemData.UserData.Played = false
                            } else {
                                Detail.markPlayed(id)
                                itemData.UserData.Played = true
                            }
                            _dataVersion++
                        }
                        onToggleFavorite: {
                            let ud = itemData.UserData || {}
                            let id = itemData.Id || itemId
                            if (!id) return
                            if (ud.IsFavorite) {
                                Detail.removeFavorite(id)
                                itemData.UserData.IsFavorite = false
                            } else {
                                Detail.addFavorite(id)
                                itemData.UserData.IsFavorite = true
                            }
                            _dataVersion++
                        }
                        onNextEpisodePlayRequested: function(ne, resumeTicks) {
                            Nav.pushPlayer({
                                itemId: ne.Id,
                                episodeTitle: ne.SeriesName || itemData.Name || "",
                                episodeSubtitle: "S" + (ne.ParentIndexNumber || "?") + "E" + (ne.IndexNumber || ""),
                                episodeIndex: -1,
                                playlistData: [{ itemId: ne.Id, itemName: ne.Name || "", indexNumber: ne.IndexNumber || 0 }],
                                itemType: Str.typeEpisode,
                                startTimeTicks: resumeTicks,
                                audioIndex: detailRoot.selectedAudioIdx,
                                subtitleIndex: detailRoot.selectedSubIdx
                            })
                        }
                    }

                    // Person favorite toggle
                    Button {
                        visible: detailRoot._isPerson && itemData.UserData !== undefined
                        Layout.preferredWidth: 30; Layout.preferredHeight: 30
                        Layout.topMargin: 4

                        property bool isFavorite: {
                            void detailRoot._dataVersion
                            let ud = itemData.UserData || {}
                            return ud.IsFavorite === true
                        }

                        background: Rectangle {
                            radius: 15
                            color: parent.hovered ? (parent.isFavorite ? Qt.rgba(66/255, 133/255, 244/255, 0.15) : Qt.rgba(1, 1, 1, 0.1))
                                                  : "transparent"
                            border { color: parent.isFavorite ? Theme.primary : Theme.textSecondary; width: 1.5 }
                        }

                        contentItem: Icon {
                            name: parent.isFavorite ? "heart_filled" : "heart"
                            color: parent.isFavorite ? Theme.primary : Theme.textSecondary
                            size: 16
                        }

                        onClicked: {
                            let ud = itemData.UserData || {}
                            let id = itemData.Id || detailRoot.itemId
                            if (!id) return
                            if (ud.IsFavorite) {
                                Detail.removeFavorite(id)
                                itemData.UserData.IsFavorite = false
                            } else {
                                Detail.addFavorite(id)
                                itemData.UserData.IsFavorite = true
                            }
                            detailRoot._dataVersion++
                        }
                    }

                    // Tagline
                    Label {
                        text: {
                            let lines = itemData.Taglines || []
                            return lines.length ? lines[0] : ""
                        }
                        color: Theme.textSecondary
                        font.pixelSize: 14
                        font.bold: true
                        font.italic: true
                        Layout.fillWidth: true
                        visible: text !== ""
                        wrapMode: Text.WordWrap
                    }

                    // Person bio info
                    PersonBioSection {
                        isPerson: detailRoot._isPerson
                        premiereDate: itemData.PremiereDate || ""
                        endDate: itemData.EndDate || ""
                        productionLocations: itemData.ProductionLocations || []
                    }

                    // Overview
                    Label {
                        text: itemData.Overview || ""
                        color: Theme.textSecondary
                        font.pixelSize: 13
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        maximumLineCount: 6
                        elide: Text.ElideRight
                        visible: text !== ""
                    }
                }
            }

            // ── Series: season picker & episodes ──
            SeriesSection {
                id: seriesSection
                itemId: detailRoot.itemId
                itemData: detailRoot.itemData
                currentSeasonIdx: detailRoot.currentSeasonIdx
                onEpisodeClicked: function(epId, playlist) {
                    Nav.pushDetail(epId)
                }
            }

            // ── Cast & Crew ──
            CastAndCrewRow {
                people: itemData.People || []
                visible: !detailRoot._isPerson
                onPersonClicked: function(personId) {
                    Nav.pushDetail(personId)
                }
            }

            // ── Studios ──
            StudiosSection {
                studios: itemData.Studios || []
                onStudioClicked: function(studioId, studioName) {
                    Library.browseStudio(studioId, studioName)
                    Nav.popToRoot()
                }
            }

            // ── Similar items / Person filmography ──
            SimilarItemsSection {
                isPerson: detailRoot._isPerson
                initialLoad: detailRoot._initialLoad
                itemType: itemData.Type || ""
                onItemClicked: function(clickedId, clickedType) {
                    Nav.pushDetail(clickedId)
                }
            }

            // ── Media Info ──
            MediaInfoSection {
                mediaStreams: detailRoot.mediaStreamsForSelectedVideo()
                mediaSources: itemData.MediaSources || []
                selectedVideoId: detailRoot.selectedVideoId
            }
        }
    }

    // ── Loading indicator ──
    Item {
        anchors.centerIn: parent
        width: 32; height: 32
        visible: detailRoot._initialLoad

        RotationAnimator on rotation {
            from: 0; to: 360
            duration: 1000
            loops: Animation.Infinite
            running: detailRoot._initialLoad
        }

        Canvas {
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                ctx.strokeStyle = "white"
                ctx.lineWidth = 2.5
                ctx.lineCap = "round"
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, width / 2 - 3, 0, Math.PI * 1.5)
                ctx.stroke()
            }
        }
    }

    Button {
        id: backBtn
        width: 36; height: 36
        anchors { left: parent.left; top: parent.top; margins: 12 }
        flat: true
        onClicked: Nav.pop()

        contentItem: Icon {
            name: "arrow_back"
            color: Theme.textSecondary
            size: 24
        }

        background: Rectangle {
            radius: 18
            color: backBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
        }
    }

    // ── Item logo (to the right of back button) ──
    // CachedImage: curl download (with MfPlayer/1.0 UA) → disk cache → ImageProvider.
    // fillMode overridden to PreserveAspectFit (Crop is CachedImage default but wrong for logos).
    // sourceSize explicitly set so ImageProvider decodes at display size, not full resolution.
    CachedImage {
        id: itemLogo
        anchors { left: backBtn.right; leftMargin: 8; verticalCenter: backBtn.verticalCenter }
        width: 300
        height: 30
        fillMode: Image.PreserveAspectFit
        sourceSize: Qt.size(300 * Screen.devicePixelRatio, 30 * Screen.devicePixelRatio)
        embyUrl: detailRoot._logoUrl
        visible: status === Image.Ready
        onStatusChanged: if (status === Image.Ready) width = Math.min(implicitWidth, 300)
    }
}
