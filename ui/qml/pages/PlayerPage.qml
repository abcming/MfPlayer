pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import mfplayer

Item {
    id: playerRoot
    focus: true

    Rectangle { anchors.fill: parent; color: "black" }
    MouseArea {
        id: cursorArea
        anchors.fill: parent
        acceptedButtons: Qt.NoButton
        cursorShape: cursorVisible ? Qt.ArrowCursor : Qt.BlankCursor
    }

    // ── Cursor auto-hide ──
    property bool cursorVisible: true

    Timer {
        id: hideCursorTimer
        interval: 3000
        running: true
        onTriggered: {
            if (topHoverHandler.hovered || bottomHoverHandler.hovered) {
                restart()
                return
            }
            cursorVisible = false
        }
    }

    // ── Input properties ──
    property string itemId: ""
    property string episodeTitle: ""
    property string episodeSubtitle: ""
    property int episodeIndex: -1
    property var playlistData: []
    property string itemType: ""
    property var itemData: null
    // MediaSources for video version selector — prefer itemData (passed from DetailPage),
    // fall back to Playback.currentItemDetail (populated from PlaybackInfo response).
    // This covers episodes played from a series page where the episode detail wasn't pre-cached.
    property var _versionSources: {
        if (itemData && itemData.MediaSources && itemData.MediaSources.length)
            return itemData.MediaSources
        let detail = Playback.currentItemDetail
        if (detail && detail.MediaSources && detail.MediaSources.length)
            return detail.MediaSources
        return []
    }
    property double startTimeTicks: 0
    property string mediaSourceId: ""
    property int audioIndex: -1
    property int subtitleIndex: -2
    property string localFile: ""

    property bool _localPlayStarted: false

    // Defer playback until after the StackView push animation (350ms) completes,
    // so mpv warmup doesn't stutter the transition.
    Timer {
        id: playStartTimer
        interval: 400
        onTriggered: {
            if (localFile) {
                itemType = "Local"
                episodeTitle = localFile.split('/').pop().split('\\').pop()
                if (!_localPlayStarted) {
                    _localPlayStarted = true
                    // Offload folder scan to I/O pool (non-blocking).
                    // localPlaylistReady signal will handle the result.
                    Playback.scanFolderForLocalPlaylistAsync(localFile)
                }
            } else if (itemId) {
                Playback.playItem(itemId, startTimeTicks, mediaSourceId, audioIndex, subtitleIndex)
            }
        }
    }

    Component.onCompleted: {
        playStartTimer.start()
        forceActiveFocus()
    }

    // ── Overlay visibility ── (hover-driven, no timers)

    property string _playError: ""
    Connections {
        target: Playback
        function onPlayError(msg) {
            _playError = msg || Str.playFailed
            Playback.stop()
            Nav.pop()
        }
    }

    // ── Derived models ──
    property var subtitleModel: {
        void Playback.mpv.tracks  // explicit dependency: re-evaluate when tracks change
        var tracks = Playback.mpv.tracks || []
        var subs = [{id: -2, title: Str.trackOff, lang: "", selected: Playback.mpv.currentSid === -2}]
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].type === "sub") {
                if (tracks[i].selected) subs[0].selected = false
                subs.push(tracks[i])
            }
        }
        return subs
    }

    property var audioModel: {
        var tracks = Playback.mpv.tracks || []
        var audios = []
        for (var i = 0; i < tracks.length; i++) {
            if (tracks[i].type === "audio") audios.push(tracks[i])
        }
        return audios
    }

    function subDisplayName(track) {
        // "Off" pseudo-track
        if (track.id === -2) return track.title
        // External subs: title is Emby DisplayTitle from sub-add
        // Built-in subs: mpv's own title or lang — same approach as Tsukimi
        return track.title || track.lang || ("Track " + track.id)
    }

    function trackLabel(track) { return subDisplayName(track) }

    function switchEpisode(idx) {
        if (idx < 0 || idx >= playlistData.length) return
        var ep = playlistData[idx]
        episodeIndex = idx

        if (ep.localFile) {
            // ── Local file ──
            itemId = ""
            startTimeTicks = 0
            localFile = ep.localFile
            episodeSubtitle = ""
            mediaSourceId = ""
            audioIndex = -1
            subtitleIndex = -1
            itemData = null
            _localPlayStarted = true  // skip playStartTimer re-init
            Playback.playLocalFile(ep.localFile)
        } else {
            // ── Emby item ──
            itemId = ep.itemId
            startTimeTicks = 0
            episodeTitle = ep.seriesName || episodeTitle
            episodeSubtitle = Str.episodeFullLabel(ep.indexNumber, ep.itemName)
            mediaSourceId = ""
            audioIndex = -1
            subtitleIndex = -1
            Detail.browseItem(ep.itemId)
            Playback.playItem(ep.itemId, 0, "", audioIndex, subtitleIndex)
        }
    }

    function toggleFullscreen() {
        Playback.toggleFullscreen()
    }

    // ── Video surface ──
    MpvRenderItem {
        id: videoSurface
        anchors.fill: parent
        player: Playback.mpv
        onMouseMoved: {
            cursorVisible = true
            hideCursorTimer.restart()
        }
    }

    // ── Top hover container (always visible) ──
    HdrPqOverlay {
        id: topOverlay
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 56 + 60
        z: 2

        HoverHandler { id: topHoverHandler }

        // ── Top bar ──
        Rectangle {
            id: topBar
            visible: topHoverHandler.hovered && cursorVisible
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: 56
            color: Qt.rgba(0, 0, 0, 0.7)

        RowLayout {
            anchors.fill: parent
            anchors { leftMargin: 12; rightMargin: 12 }

            // Back button
            Button {
                id: backBtn
                focusPolicy: Qt.NoFocus
                width: 36; height: 36
                flat: true
                onClicked: { Playback.stop(); Nav.pop() }

                contentItem: Icon {
                    name: "arrow_back"
                    color: backBtn.hovered ? Theme.primary : Theme.textSecondary
                    size: 22
                }

                background: Rectangle {
                    radius: 18
                    color: backBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
                }
            }

            // Title + subtitle
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 8
                spacing: 0

                Label {
                    text: episodeTitle || ""
                    color: Theme.textPrimary
                    font.pixelSize: 15; font.bold: true
                    elide: Text.ElideRight; maximumLineCount: 1
                    Layout.fillWidth: true
                    visible: text !== ""
                }
                Label {
                    text: episodeSubtitle || ""
                    color: Theme.textTertiary
                    font.pixelSize: 12
                    elide: Text.ElideRight; maximumLineCount: 1
                    Layout.fillWidth: true
                    visible: text !== ""
                }
            }

            // Video version button
            Button {
                id: videoVerBtn
                focusPolicy: Qt.NoFocus
                visible: _versionSources.length > 1
                flat: true
                width: 36; height: 36

                contentItem: Icon {
                    name: "movie"
                    color: videoVerBtn.hovered ? Theme.primary : Theme.textSecondary
                    size: 20
                }

                background: Rectangle {
                    radius: 18
                    color: videoVerBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
                }

                onClicked: videoVerPopup.open()
            }

            StyledPopup {
                id: videoVerPopup
                x: videoVerBtn.x + videoVerBtn.width - width
                y: videoVerBtn.y + videoVerBtn.height + 4
                width: 260; padding: 6

                ListView {
                    anchors.fill: parent
                    implicitHeight: Math.min(contentHeight, 200)
                    clip: true; spacing: 2
                    model: _versionSources

                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index

                        width: videoVerPopup.width - 12
                        hoverEnabled: true

                        contentItem: ColumnLayout {
                            spacing: 2
                            Label {
                                text: modelData.Name || (Str.trackVersionPrefix + (index + 1))
                                color: parent.parent.hovered ? Theme.primary : Theme.textSecondary
                                font.pixelSize: 13
                            }
                            Label {
                                text: {
                                    let s = modelData
                                    let parts = []
                                    let ms = s.MediaStreams || []
                                    for (let j = 0; j < ms.length; j++)
                                        if (ms[j].Type === "Video")
                                            parts.push(ms[j].DisplayTitle || ms[j].Codec || "")
                                    parts.push(s.Container || "")
                                    return parts.join(" · ")
                                }
                                color: Theme.textMuted; font.pixelSize: 10
                                visible: text !== ""
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Theme.active : "transparent"
                        }

                        onClicked: {
                            let pos = Playback.position
                            let ticks = Math.round(pos * 10000000)
                            playerRoot.audioIndex = -1
                            playerRoot.subtitleIndex = -1
                            Playback.playItem(itemId, ticks, modelData.Id, -1, -1)
                            videoVerPopup.close()
                        }
                    }
                }
            }

            // Subtitle track button
            Button {
                focusPolicy: Qt.NoFocus
                id: subtitleBtn
                visible: subtitleModel.length > 1
                flat: true
                width: 36; height: 36

                contentItem: Icon {
                    name: "subtitles"
                    color: subtitleBtn.hovered ? Theme.primary : Theme.textSecondary
                    size: 20
                }

                background: Rectangle {
                    radius: 18
                    color: subtitleBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
                }

                onClicked: subtitlePopup.open()
            }

            StyledPopup {
                id: subtitlePopup
                x: subtitleBtn.x + subtitleBtn.width - width
                y: subtitleBtn.y + subtitleBtn.height + 4
                width: 220
                padding: 6

                ListView {
                    id: subtitleList
                    anchors.fill: parent
                    implicitHeight: Math.min(contentHeight, 250)
                    model: subtitleModel
                    clip: true
                    spacing: 2

                    delegate: ItemDelegate {
                        required property var modelData

                        width: subtitleList.width
                        hoverEnabled: true

                        contentItem: RowLayout {
                            spacing: 8

                            Label {
                                text: trackLabel(modelData)
                                color: parent.parent.hovered ? Theme.primary : Theme.textSecondary
                                font.pixelSize: 13
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Icon {
                                name: "check"
                                color: Theme.primary
                                size: 16
                                visible: modelData.selected
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Theme.active : "transparent"
                        }

                        onClicked: {
                            Playback.setSid(modelData.id)
                            subtitlePopup.close()
                        }
                    }
                }
            }

            // Audio track button
            Button {
                focusPolicy: Qt.NoFocus
                id: audioBtn
                visible: audioModel.length > 1
                flat: true
                width: 36; height: 36

                contentItem: Icon {
                    name: "volume_up"
                    color: audioBtn.hovered ? Theme.primary : Theme.textSecondary
                    size: 20
                }

                background: Rectangle {
                    radius: 18
                    color: audioBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
                }

                onClicked: audioPopup.open()
            }

            StyledPopup {
                id: audioPopup
                x: audioBtn.x + audioBtn.width - width
                y: audioBtn.y + audioBtn.height + 4
                width: 220
                padding: 6

                ListView {
                    id: audioList
                    anchors.fill: parent
                    implicitHeight: Math.min(contentHeight, 250)
                    model: audioModel
                    clip: true
                    spacing: 2

                    delegate: ItemDelegate {
                        required property var modelData

                        width: audioList.width
                        hoverEnabled: true

                        contentItem: RowLayout {
                            spacing: 8

                            Label {
                                text: trackLabel(modelData)
                                color: parent.parent.hovered ? Theme.primary : Theme.textSecondary
                                font.pixelSize: 13
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }

                            Icon {
                                name: "check"
                                color: Theme.primary
                                size: 16
                                visible: modelData.selected
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: parent.hovered ? Theme.active : "transparent"
                        }

                        onClicked: {
                            Playback.setAid(modelData.id)
                            audioPopup.close()
                        }
                    }
                }
            }

            // Chapter button
            Button {
                focusPolicy: Qt.NoFocus
                id: chapterBtn
                visible: (Playback.mpv.chapters || []).length > 1
                flat: true
                width: 36; height: 36

                contentItem: Icon {
                    name: "toc"
                    color: chapterBtn.hovered ? Theme.primary : Theme.textSecondary
                    size: 20
                }

                background: Rectangle {
                    radius: 18
                    color: chapterBtn.hovered ? Qt.rgba(1,1,1,0.12) : "transparent"
                }

                onClicked: chapterPopup.open()
            }

            StyledPopup {
                id: chapterPopup
                x: chapterBtn.x + chapterBtn.width - width
                y: chapterBtn.y + chapterBtn.height + 4
                width: 260
                padding: 6

                ListView {
                    anchors.fill: parent
                    implicitHeight: Math.min(contentHeight, 300)
                    model: Playback.mpv.chapters || []
                    clip: true
                    spacing: 2

                    delegate: ItemDelegate {
                        required property var modelData
                        required property int index

                        width: chapterPopup.width - 12
                        hoverEnabled: true

                        contentItem: RowLayout {
                            spacing: 8

                            Label {
                                text: {
                                    let t = modelData.time || 0
                                    let h = Math.floor(t / 3600)
                                    let m = Math.floor((t % 3600) / 60)
                                    let s = Math.floor(t % 60)
                                    return h > 0
                                        ? (h + ":" + (m < 10 ? "0" : "") + m + ":" + (s < 10 ? "0" : "") + s)
                                        : (m + ":" + (s < 10 ? "0" : "") + s)
                                }
                                color: index === Playback.mpv.currentChapter ? Theme.primary : Theme.textMuted
                                font.pixelSize: 11
                                Layout.preferredWidth: 55
                            }

                            Label {
                                text: modelData.title || (Str.playerChapterPrefix + (index + 1))
                                color: index === Playback.mpv.currentChapter ? Theme.textPrimary
                                       : (parent.parent.hovered ? Theme.primary : Theme.textSecondary)
                                font.pixelSize: 12
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                        }

                        background: Rectangle {
                            radius: 4
                            color: index === Playback.mpv.currentChapter ? Theme.active
                                   : (parent.hovered ? Theme.activeHover : "transparent")
                        }

                        onClicked: {
                            Playback.setChapter(index)
                            chapterPopup.close()
                        }
                    }
                }
            }

            // Playlist toggle button
            Button {
                id: playlistBtn
                focusPolicy: Qt.NoFocus
                visible: playlistData.length > 1
                flat: true
                width: 36; height: 36

                contentItem: Icon {
                    name: "playlist_play"
                    color: playlistPanel.opened ? Theme.primary
                          : (playlistBtn.hovered ? Theme.primary : Theme.textSecondary)
                    size: 20
                }

                background: Rectangle {
                    radius: 18
                    color: playlistPanel.opened ? Qt.rgba(1,1,1,0.12)
                          : (playlistBtn.hovered ? Qt.rgba(1,1,1,0.08) : "transparent")
                }

                onClicked: playlistPanel.opened ? playlistPanel.close() : playlistPanel.open()
            }
        }
    }

    // ── Playlist panel ──
    StyledPopup {
        id: playlistPanel
        x: playlistBtn.x + playlistBtn.width - width
        y: playlistBtn.y + playlistBtn.height + 4
        width: 260
        height: Math.min(Math.max(playlistList.contentHeight + 48, 180),
                         playerRoot.height - y - 80)
        padding: 12

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            Label {
                text: Str.playerPlaylist
                color: Theme.primary
                font.pixelSize: 14; font.bold: true
            }

            ListView {
                id: playlistList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: playlistData
                clip: true
                spacing: 4

                delegate: Rectangle {
                    required property var modelData
                    required property int index

                    id: plDelegate
                    width: ListView.view.width
                    height: 48
                    radius: 6
                    color: index === episodeIndex ? Theme.active
                           : (plMouse.containsMouse ? Theme.activeHover : "transparent")

                    RowLayout {
                        anchors.fill: parent
                        anchors { leftMargin: 8; rightMargin: 8 }
                        spacing: 8

                        Label {
                            text: "E" + (modelData.indexNumber || "")
                            color: index === episodeIndex ? Theme.primary : Theme.textMuted
                            font.pixelSize: 13; font.bold: index === episodeIndex
                            Layout.preferredWidth: 45
                        }

                        Label {
                            text: modelData.itemName || "?"
                            color: index === episodeIndex ? Theme.textPrimary : Theme.textSecondary
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            maximumLineCount: 1
                        }

                        Icon {
                            name: "play_arrow"
                            color: Theme.primary
                            size: 14
                            visible: index === episodeIndex
                        }
                    }

                    MouseArea {
                        id: plMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: {
                            switchEpisode(index)
                            playlistPanel.close()
                        }
                    }
                }
            }
        }
        }

    }

    // ── Bottom hover container (always visible) ──
    HdrPqOverlay {
        id: bottomOverlay
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            margins: 16
        }
        height: 60 + 60
        z: 1

        HoverHandler { id: bottomHoverHandler }

        PlayerControls {
            id: controls
            visible: (bottomHoverHandler.hovered && cursorVisible) || !Playback.playing
            anchors {
                bottom: parent.bottom
                left: parent.left
                right: parent.right
            }
            episodeIndex: playerRoot.episodeIndex
            playlistData: playerRoot.playlistData
            onPrevClicked: switchEpisode(episodeIndex - 1)
            onNextClicked: switchEpisode(episodeIndex + 1)
        }

    }

    // ── Keyboard shortcuts (configurable via Settings) ──
    Keys.onPressed: (event) => {
        var k = event.key
        var s = Server.settings
        if (k === Qt.Key_Escape) {
            Playback.stop(); Nav.pop()
        } else if (k === s.keySeekBackward) {
            Playback.seek(Math.max(0, Playback.position - s.seekBackwardStep))
            event.accepted = true
        } else if (k === s.keySeekForward) {
            Playback.seek(Math.min(Playback.position + s.seekForwardStep, Playback.duration))
            event.accepted = true
        } else if (k === s.keyPlayPause) {
            Playback.playing ? Playback.pause() : Playback.resume()
            event.accepted = true
        } else if (k === s.keyFullscreen || k === Qt.Key_F11) {
            toggleFullscreen()
            event.accepted = true
        } else if (k === s.keyStats) {
            Playback.mpv.toggleStats()
            event.accepted = true
        } else if (k === s.keySpeedDown) {
            var speeds = [0.5, 1.0, 1.25, 1.5, 2.0, 3.0]
            var cur = Playback.mpv.speed || 1.0
            var idx = speeds.indexOf(cur)
            Playback.mpv.setSpeed(speeds[Math.max(0, idx - 1)])
            event.accepted = true
        } else if (k === s.keySpeedUp) {
            var speeds2 = [0.5, 1.0, 1.25, 1.5, 2.0, 3.0]
            var cur2 = Playback.mpv.speed || 1.0
            var idx2 = speeds2.indexOf(cur2)
            Playback.mpv.setSpeed(speeds2[Math.min(speeds2.length - 1, idx2 + 1)])
            event.accepted = true
        } else if (k === s.keyVolumeUp) {
            Playback.setVolume(Math.min(100, Playback.volume + 5))
            event.accepted = true
        } else if (k === s.keyVolumeDown) {
            Playback.setVolume(Math.max(0, Playback.volume - 5))
            event.accepted = true
        }
    }

    // ── Async local playlist scan result ──
    Connections {
        target: Playback
        function onLocalPlaylistReady(pl) {
            playlistData = pl
            for (var i = 0; i < pl.length; i++) {
                if (pl[i].isCurrent) {
                    episodeIndex = i
                    break
                }
            }
            if (episodeIndex < 0) episodeIndex = 0
            Playback.playLocalFile(localFile)
        }
    }

    // ── Auto-play next / loop / stop on end of file ──
    Connections {
        target: Playback
        function onEndOfFile() {
            var action = Server.settings.actionAfterEnd
            if (action === 1) {
                // Loop: restart current file
                Playback.seek(0)
                return
            }
            if (action === 2) {
                // Stop: pop back immediately
                Nav.pop()
                return
            }
            // Default (0): play next episode if available, otherwise pop
            if (episodeIndex >= 0 && episodeIndex < playlistData.length - 1) {
                switchEpisode(episodeIndex + 1)
            } else {
                Nav.pop()
            }
        }
    }

    // ── Subtitle file drag-and-drop ──
    DropArea {
        anchors.fill: parent
        keys: ["text/uri-list"]
        z: 10  // above video surface and overlay containers

        onDropped: function(drop) {
            if (!drop.hasUrls) return
            var subExts = ["srt","ass","ssa","sub","vtt","idx","sup","smi"]
            for (var i = 0; i < drop.urls.length; i++) {
                var path = drop.urls[i].toString().replace(/^file:\/{2,3}/, "")
                var ext = path.split('.').pop().toLowerCase()
                if (subExts.indexOf(ext) >= 0) {
                    var fileName = path.split('/').pop().split('\\').pop()
                    Playback.mpv.addSubtitleFile("file://" + path, fileName, "")
                }
            }
        }
    }
}
