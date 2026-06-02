import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Unified media stream selector (video version / audio track / subtitle track).
// Used in DetailPage to pick from Emby MediaStreams/MediaSources.

Item {
    id: root

    // ── API ──
    property string label: ""            // "视频:", "音频:", "字幕:"
    property string streamType: ""       // "Video", "Audio", "Subtitle"
    property var mediaStreams: []        // Player.currentItem.MediaStreams
    property var mediaSources: []        // Player.currentItem.MediaSources (video only)
    property string selectedVideoId: ""
    property int selectedAudioIdx: -1
    property int selectedSubIdx: -2

    signal videoSelected(string sourceId)
    signal audioSelected(int streamIndex)
    signal subtitleSelected(int streamIndex)

    implicitHeight: labelRow.implicitHeight
    implicitWidth: labelRow.implicitWidth

    // ── Filtered model ──
    property var filteredModel: {
        if (streamType === "Video") {
            return mediaSources
        } else if (streamType === "Subtitle") {
            var arr = [{DisplayTitle: Str.trackOff, Index: -2, IsDefault: false, Type: "Subtitle"}]
            for (var i = 0; i < mediaStreams.length; i++)
                if (mediaStreams[i].Type === "Subtitle") arr.push(mediaStreams[i])
            return arr
        } else {
            var arr2 = []
            for (var j = 0; j < mediaStreams.length; j++)
                if (mediaStreams[j].Type === streamType) arr2.push(mediaStreams[j])
            return arr2
        }
    }

    // ── Button text (value only, label is separate) ──
    property string buttonText: {
        if (streamType === "Video") {
            if (!mediaSources || mediaSources.length === 0) return ""
            var sel = selectedVideoId
            for (var i = 0; i < mediaSources.length; i++) {
                if (mediaSources[i].Id === (sel || mediaSources[0].Id)) {
                    var nm = mediaSources[i].Name || ""
                    return nm.length > 20 ? nm.substring(0, 20) + "..." : nm
                }
            }
            return Str.trackDefault
        } else if (streamType === "Audio") {
            var sel2 = selectedAudioIdx
            for (var j = 0; j < mediaStreams.length; j++) {
                if (mediaStreams[j].Type === "Audio") {
                    if (sel2 < 0 && mediaStreams[j].IsDefault)
                        return (mediaStreams[j].DisplayTitle || mediaStreams[j].Language || "Track " + j)
                    if (mediaStreams[j].Index === sel2)
                        return (mediaStreams[j].DisplayTitle || mediaStreams[j].Language || "Track " + j)
                }
            }
            return Str.trackDefault
        } else {
            var sel3 = selectedSubIdx
            if (sel3 === -2) return Str.trackOff
            for (var k = 0; k < mediaStreams.length; k++) {
                if (mediaStreams[k].Type === "Subtitle") {
                    if (sel3 < 0 && mediaStreams[k].IsDefault)
                        return (mediaStreams[k].DisplayTitle || mediaStreams[k].Language || "Track " + k)
                    if (mediaStreams[k].Index === sel3)
                        return (mediaStreams[k].DisplayTitle || mediaStreams[k].Language || "Track " + k)
                }
            }
            return Str.trackDefault
        }
    }

    // ── Checkmark helper ──
    function isSelected(modelData) {
        if (streamType === "Video") return false
        if (streamType === "Subtitle") {
            var idx = modelData.Index
            if (idx === -2 && selectedSubIdx === -2) return true
            if (idx === selectedSubIdx) return true
            return modelData.IsDefault && selectedSubIdx < 0 && idx !== -2
        }
        if (streamType === "Audio") {
            var def = modelData.IsDefault
            var sel = selectedAudioIdx
            if (def && sel < 0) return true
            return modelData.Index === sel
        }
        return false
    }

    // ── Button visibility ──
    property bool hasContent: {
        if (streamType === "Video") return mediaSources.length > 1
        for (var i = 0; i < mediaStreams.length; i++)
            if (mediaStreams[i].Type === streamType) return true
        return false
    }

    visible: hasContent

    RowLayout {
        id: labelRow
        spacing: 4

        Label {
            text: root.label
            color: Theme.textMuted
            font.pixelSize: 12
            visible: root.label !== ""
        }

        Button {
            id: selectorBtn
            flat: true

            contentItem: Label {
                text: root.buttonText
                color: Theme.textTertiary
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            background: Rectangle {
                radius: 4
                color: selectorBtn.hovered ? Theme.activeHover : Theme.active
                border { color: Theme.activeHover; width: 1 }
            }

            onClicked: trackPopup.open()
        }
    }

    StyledPopup {
        id: trackPopup
        y: selectorBtn.height + 2
        width: streamType === "Video" ? 280 : 240
        padding: 6
        popupRadius: Theme.radiusMedium
        bgColor: Theme.panel

        ListView {
            anchors.fill: parent
            implicitHeight: Math.min(contentHeight, 200)
            clip: true
            spacing: 2
            model: root.filteredModel

            delegate: ItemDelegate {
                id: trackItem
                width: trackPopup.width - 12
                hoverEnabled: true

                contentItem: Loader {
                    sourceComponent: streamType === "Video" ? videoDelContent : trackDelContent
                }

                Component {
                    id: videoDelContent
                    ColumnLayout {
                        spacing: 2
                        Label {
                            text: modelData.Name || (Str.trackVersionPrefix + (index + 1))
                            color: trackItem.hovered ? Theme.primary : Theme.textSecondary
                            font.pixelSize: 13
                        }
                        Label {
                            text: {
                                var parts = []
                                var ms = modelData.MediaStreams || []
                                for (var j = 0; j < ms.length; j++)
                                    if (ms[j].Type === "Video")
                                        parts.push(ms[j].DisplayTitle || ms[j].Codec || "")
                                parts.push(modelData.Container || "")
                                return parts.join(" · ")
                            }
                            color: Theme.textMuted; font.pixelSize: 10
                            visible: text !== ""
                        }
                    }
                }

                Component {
                    id: trackDelContent
                    RowLayout {
                        spacing: 8
                        Label {
                            text: modelData.DisplayTitle || modelData.Language || ("Track " + index)
                            color: trackItem.hovered ? Theme.primary : Theme.textSecondary
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        Icon {
                            name: "check"
                            color: Theme.primary
                            size: 14
                            visible: root.isSelected(modelData)
                        }
                    }
                }

                background: Rectangle {
                    radius: 4
                    color: parent.hovered ? Theme.active : "transparent"
                }

                onClicked: {
                    if (streamType === "Video") {
                        root.videoSelected(modelData.Id)
                    } else if (streamType === "Audio") {
                        root.audioSelected(modelData.Index)
                    } else {
                        root.subtitleSelected(modelData.Index)
                    }
                    trackPopup.close()
                }
            }
        }
    }
}
