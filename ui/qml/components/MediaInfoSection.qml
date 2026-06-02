import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Displays media stream info (video/audio/subtitle cards) and container/filesize.
// Extracted from DetailPage.qml.

Column {
    id: root

    // Streams for the currently selected video version (filtered by mediaStreamsForSelectedVideo)
    property var mediaStreams: []
    // All media sources (for container + filesize display)
    property var mediaSources: []
    // Currently selected video source Id
    property string selectedVideoId: ""

    width: parent ? parent.width : 0
    spacing: 8
    visible: mediaStreams.length > 0

    // ── Build card list from streams (single pass) ──
    property var allMediaCards: {
        var cards = []
        var streams = root.mediaStreams
        for (var i = 0; i < streams.length; i++) {
            var s = streams[i]
            var t = s.Type
            if (t === "Video") cards.push({cardType: "Video", stream: s})
            else if (t === "Audio") cards.push({cardType: "Audio", stream: s})
            else if (t === "Subtitle") cards.push({cardType: "Subtitle", stream: s})
        }
        return cards
    }

    // ── Key-value pair helpers ──
    function kvPairsForVideo(s) {
        var pairs = []
        if (s.DisplayTitle) pairs.push({k: Str.miTitle, v: s.DisplayTitle})
        if (s.Title) pairs.push({k: Str.miEmbeddedTitle, v: s.Title})
        if (s.Codec) pairs.push({k: Str.miCodec, v: s.Codec.toUpperCase()})
        if (s.ExtendedVideoSubTypeDescription) pairs.push({k: Str.miDolbyConfig, v: s.ExtendedVideoSubTypeDescription})
        if (s.Profile) pairs.push({k: Str.miProfile, v: s.Profile})
        if (s.Level) pairs.push({k: Str.miLevel, v: "" + s.Level})
        if (s.Width && s.Height) pairs.push({k: Str.miResolution, v: s.Width + "×" + s.Height})
        if (s.AspectRatio) pairs.push({k: Str.miAspectRatio, v: s.AspectRatio})
        pairs.push({k: Str.miInterlaced, v: s.IsInterlaced ? Str.miYes : Str.miNo})
        if (s.AverageFrameRate) pairs.push({k: Str.miFrameRate, v: Number(s.AverageFrameRate).toFixed(3)})
        if (s.BitRate) pairs.push({k: Str.miBitrate, v: (s.BitRate / 1000000).toFixed(0) + " mbps"})
        if (s.VideoRange) pairs.push({k: Str.miVideoRange, v: s.VideoRange})
        if (s.ColorPrimaries) pairs.push({k: Str.miColorPrimaries, v: s.ColorPrimaries})
        if (s.ColorSpace) pairs.push({k: Str.miColorSpace, v: s.ColorSpace})
        if (s.ColorTransfer) pairs.push({k: Str.miColorTransfer, v: s.ColorTransfer})
        if (s.BitDepth) pairs.push({k: Str.miBitDepth, v: s.BitDepth + " bit"})
        if (s.PixelFormat) pairs.push({k: Str.miPixelFormat, v: s.PixelFormat})
        if (s.RefFrames) pairs.push({k: Str.miRefFrames, v: "" + s.RefFrames})
        return pairs
    }

    function kvPairsForAudio(s) {
        var pairs = []
        if (s.Title) pairs.push({k: Str.miTitle, v: s.Title})
        else if (s.DisplayTitle) pairs.push({k: Str.miTitle, v: s.DisplayTitle})
        if (s.Language) pairs.push({k: Str.miLanguage, v: s.Language})
        if (s.Codec) pairs.push({k: Str.miCodec, v: s.Codec.toUpperCase()})
        if (s.ChannelLayout) pairs.push({k: Str.miLayout, v: s.ChannelLayout})
        if (s.Channels) pairs.push({k: Str.miChannels, v: s.Channels + " ch"})
        if (s.BitRate) pairs.push({k: Str.miBitrate, v: (s.BitRate / 1000).toFixed(0) + " kbps"})
        if (s.SampleRate) pairs.push({k: Str.miSampleRate, v: (s.SampleRate / 1000).toFixed(0) + " kHz"})
        pairs.push({k: Str.miDefault, v: s.IsDefault ? Str.miYes : Str.miNo})
        return pairs
    }

    function kvPairsForSub(s) {
        var pairs = []
        if (s.Title) pairs.push({k: Str.miTitle, v: s.Title})
        else if (s.DisplayTitle) pairs.push({k: Str.miTitle, v: s.DisplayTitle})
        if (s.Language) pairs.push({k: Str.miLanguage, v: s.Language})
        if (s.Codec) pairs.push({k: Str.miCodec, v: s.Codec.toUpperCase()})
        pairs.push({k: Str.miDefault, v: s.IsDefault ? Str.miYes : Str.miNo})
        if (s.IsForced) pairs.push({k: Str.miForced, v: Str.miYes})
        if (s.IsExternal) pairs.push({k: Str.miExternal, v: Str.miYes})
        return pairs
    }

    // ── Media info cards ──
    HorizontalMediaRow {
        sectionTitle: Str.detailMediaInfo
        rowHeight: 350
        cardSpacing: 8
        listModel: root.allMediaCards
        delegate: mediaInfoCardDelegate
    }

    // ── Container + file size ──
    Label {
        text: {
            var sources = root.mediaSources || []
            if (!sources.length) return ""
            var selId = root.selectedVideoId || sources[0].Id
            var src = sources[0]
            for (var i = 0; i < sources.length; i++)
                if (sources[i].Id === selId) { src = sources[i]; break }
            var parts = []
            if (src.Container) parts.push(src.Container.toUpperCase())
            if (src.Size) parts.push((src.Size / 1073741824).toFixed(1) + " GB")
            return parts.join(" · ")
        }
        color: Theme.textDimmer
        font.pixelSize: 11
        visible: text !== ""
    }

    // ── Card delegate component ──
    Component {
        id: mediaInfoCardDelegate
        Rectangle {
            width: 220
            height: 340
            radius: 8
            color: Qt.rgba(30/255, 30/255, 30/255, 0.7)
            border { color: Theme.active; width: 1 }

            property var stream: modelData.stream
            property string cardType: modelData.cardType

            Column {
                anchors { fill: parent; margins: 10 }
                spacing: 6

                RowLayout {
                    Label {
                        text: cardType === "Video" ? Str.tabVideo : (cardType === "Audio" ? Str.tabAudio : Str.tabSubtitle)
                        color: Theme.primary
                        font.pixelSize: 13; font.bold: true
                        Layout.fillWidth: true
                    }
                    Label {
                        text: stream.IsDefault ? Str.tabDefault : ""
                        color: Theme.primary; font.pixelSize: 10
                        visible: stream.IsDefault || false
                    }
                    Label {
                        text: stream.IsForced ? Str.tabForced : ""
                        color: Theme.star; font.pixelSize: 10
                        visible: stream.IsForced || false
                    }
                }

                Column {
                    spacing: 2

                    Repeater {
                        model: {
                            if (cardType === "Video") return root.kvPairsForVideo(stream)
                            if (cardType === "Audio") return root.kvPairsForAudio(stream)
                            return root.kvPairsForSub(stream)
                        }
                        RowLayout {
                            spacing: 6
                            Label {
                                text: modelData.k; color: Theme.textSecondary; font.pixelSize: 10
                            }
                            Label {
                                text: modelData.v; color: Theme.textMuted; font.pixelSize: 10
                                Layout.fillWidth: true
                            }
                        }
                    }
                }
            }
        }
    }
}
