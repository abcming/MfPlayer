pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: controlsRoot
    height: volumeOnly ? 40 : 60
    color: volumeOnly ? "transparent" : Qt.rgba(0, 0, 0, 0.7)
    radius: 8
    visible: true

    property int episodeIndex: -1
    property var playlistData: []
    property bool volumeOnly: false
    readonly property bool fullscreen: Playback.fullscreen
    signal prevClicked()
    signal nextClicked()

    // ── Volume-only strip (same position as the volume slider in the
    // full controls: right-aligned, accounting for fullscreen button 36px
    // + spacing 4px + right margin 8px = 48px from the right edge) ──
    Row {
        anchors {
            right: parent.right
            rightMargin: 48
            verticalCenter: parent.verticalCenter
        }
        spacing: 4
        visible: volumeOnly

        Icon {
            name: Playback.volume > 0 ? "volume_up" : "volume_off"
            color: Theme.textTertiary
            size: 18
            anchors.verticalCenter: parent.verticalCenter
        }

        Slider {
            id: volumeOnlySlider
            focusPolicy: Qt.NoFocus
            width: 80
            anchors.verticalCenter: parent.verticalCenter
            from: 0
            to: 100
            value: Playback.volume
            onMoved: Playback.setVolume(value)

            background: Rectangle {
                x: volumeOnlySlider.leftPadding
                y: volumeOnlySlider.topPadding + volumeOnlySlider.availableHeight / 2 - 2
                implicitHeight: 3
                width: volumeOnlySlider.availableWidth
                height: 3
                radius: 1
                color: Theme.textDisabled

                Rectangle {
                    width: volumeOnlySlider.visualPosition * parent.width
                    height: parent.height
                    color: Theme.primary
                    radius: 1
                }
            }

            handle: Rectangle {
                x: volumeOnlySlider.leftPadding + volumeOnlySlider.visualPosition
                   * (volumeOnlySlider.availableWidth - width)
                y: volumeOnlySlider.topPadding + volumeOnlySlider.availableHeight / 2 - height / 2
                implicitWidth: 8
                implicitHeight: 8
                radius: 4
                color: Theme.primary
            }
        }
    }

    // ── Full controls layout ──
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        visible: !volumeOnly

        // Progress bar
        // live:true — handle follows mouse during drag.
        // Seek directly on every value change for frame-level scrubbing.
        Slider {
            visible: !controlsRoot.volumeOnly
            id: progressSlider
            focusPolicy: Qt.NoFocus
            Layout.fillWidth: true
            from: 0
            to: Playback.duration || 1
            live: true
            property string _cachedTime: "0:00 / 0:00"
            property int _lastSecond: -1

            Timer {
                id: seekThrottle
                interval: 80
                repeat: false
                onTriggered: Playback.seek(progressSlider.value)
            }

            property int _snappedChapter: -1
            property bool _snapping: false

            function snapToChapter(val) {
                var chapters = Playback.mpv.chapters || []
                if (chapters.length === 0) return val
                var dur = Playback.duration || 1
                var snapDist = Math.max(dur * 0.005, 3)
                var unstickDist = snapDist * 1.5

                if (_snappedChapter >= 0 && _snappedChapter < chapters.length) {
                    var ct = chapters[_snappedChapter].time || 0
                    if (ct > 0 && ct < dur && Math.abs(val - ct) < unstickDist)
                        return ct
                    _snappedChapter = -1
                }

                var best = val, bestDist = snapDist
                for (var i = 0; i < chapters.length; i++) {
                    var t = chapters[i].time || 0
                    if (t <= 0 || t >= dur) continue
                    var dist = Math.abs(val - t)
                    if (dist < bestDist) {
                        bestDist = dist
                        best = t
                        _snappedChapter = i
                    }
                }
                return best
            }

            onValueChanged: {
                if (!pressed || _snapping) return
                var snapped = snapToChapter(value)
                if (snapped !== value) {
                    _snapping = true
                    value = snapped
                    _snapping = false
                }
                seekThrottle.restart()
            }

            onPressedChanged: {
                if (!pressed) {
                    var finalValue = snapToChapter(value)
                    _snappedChapter = -1
                    seekThrottle.stop()
                    Playback.seek(finalValue)
                }
            }

            Connections {
                target: Playback
                function onPositionChanged() {
                    var sec = Math.floor(Playback.position)
                    if (sec !== progressSlider._lastSecond) {
                        progressSlider._lastSecond = sec
                        progressSlider._cachedTime = formatTime(Playback.position) + " / " + formatTime(Playback.duration)
                        if (!progressSlider.pressed)
                            progressSlider.value = Playback.position
                    }
                }
                function onDurationChanged() {
                    progressSlider._cachedTime = formatTime(Playback.position) + " / " + formatTime(Playback.duration)
                    progressSlider.to = Playback.duration || 1
                }
            }

            Component.onCompleted: {
                progressSlider.value = Playback.position
                progressSlider._cachedTime = formatTime(Playback.position) + " / " + formatTime(Playback.duration)
            }

            background: Rectangle {
                x: progressSlider.leftPadding
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - 2
                implicitHeight: 4
                width: progressSlider.availableWidth
                height: 4
                radius: 2
                color: Theme.textDisabled

                Rectangle {
                    width: ((progressSlider.value - progressSlider.from) / (progressSlider.to - progressSlider.from)) * parent.width
                    height: parent.height
                    color: Theme.primary
                    radius: 2
                }

                // Chapter markers — vertical ticks on the bar
                Repeater {
                    model: Playback.mpv.chapters || []
                    Rectangle {
                        required property var modelData
                        required property int index

                        x: (modelData.time || 0) / (Playback.duration || 1) * (parent.width - progressSlider.handle.width) + progressSlider.handle.width / 2 - width / 2
                        y: -3
                        width: 2; height: parent.height + 6
                        radius: 1
                        color: (modelData.time || 0) <= Playback.position ? Theme.primary : Theme.textTertiary
                        visible: index > 0 && (modelData.time || 0) < (Playback.duration || 1)

                        MouseArea {
                            anchors { fill: parent; margins: -4 }
                            onClicked: Playback.seek(modelData.time || 0)
                        }
                    }
                }
            }

            handle: Rectangle {
                x: progressSlider.leftPadding + ((progressSlider.value - progressSlider.from) / (progressSlider.to - progressSlider.from))
                   * (progressSlider.availableWidth - width)
                y: progressSlider.topPadding + progressSlider.availableHeight / 2 - height / 2
                implicitWidth: 12
                implicitHeight: 12
                radius: 6
                color: progressSlider.pressed ? Theme.primaryHover : Theme.primary
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                visible: !controlsRoot.volumeOnly
                text: progressSlider._cachedTime
                color: Theme.textTertiary
                font.pixelSize: 12
                Layout.preferredWidth: 120
            }

            Item { visible: !controlsRoot.volumeOnly; Layout.fillWidth: true }

            // Prev episode
            Button {
                visible: !controlsRoot.volumeOnly && playlistData.length > 1 && episodeIndex > 0
                focusPolicy: Qt.NoFocus
                flat: true
                implicitWidth: 36; implicitHeight: 36
                onClicked: prevClicked()

                contentItem: Icon {
                    name: "skip_previous"
                    color: parent.hovered ? Theme.primary : Theme.textTertiary
                    size: 22
                }

                background: Rectangle {
                    color: parent.hovered ? Theme.activeHover : "transparent"
                    radius: 4
                }
            }

            Button {
                visible: !controlsRoot.volumeOnly
                focusPolicy: Qt.NoFocus
                flat: true
                implicitWidth: 36; implicitHeight: 36
                onClicked: Playback.playing ? Playback.pause() : Playback.resume()

                contentItem: Icon {
                    name: Playback.playing ? "pause" : "play_arrow"
                    color: Theme.textPrimary
                    size: 22
                }

                background: Rectangle {
                    color: parent.hovered ? Theme.active : "transparent"
                    radius: 4
                }
            }

            // Next episode
            Button {
                visible: !controlsRoot.volumeOnly && playlistData.length > 1
                         && episodeIndex >= 0
                         && episodeIndex < playlistData.length - 1
                focusPolicy: Qt.NoFocus
                flat: true
                implicitWidth: 36; implicitHeight: 36
                onClicked: nextClicked()

                contentItem: Icon {
                    name: "skip_next"
                    color: parent.hovered ? Theme.primary : Theme.textTertiary
                    size: 22
                }

                background: Rectangle {
                    color: parent.hovered ? Theme.activeHover : "transparent"
                    radius: 4
                }
            }

            Item { visible: !controlsRoot.volumeOnly; Layout.fillWidth: true }

            RowLayout {
                spacing: 4

                // Speed selector
                Button {
                    visible: !controlsRoot.volumeOnly
                    id: speedBtn
                    focusPolicy: Qt.NoFocus
                    flat: true
                    implicitWidth: 44; implicitHeight: 36

                    property double current: Playback.mpv.speed || 1.0

                    onClicked: speedPopup.open()

                    contentItem: Label {
                        text: {
                            var s = speedBtn.current
                            if (s === 1) return "1x"
                            if (s === 2) return "2x"
                            if (s === 3) return "3x"
                            return s + "x"
                        }
                        color: speedBtn.current !== 1.0
                            ? Theme.primary
                            : (speedBtn.hovered ? Theme.primary : Theme.textTertiary)
                        font.pixelSize: 12
                        font.bold: speedBtn.current !== 1.0
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        color: speedBtn.hovered ? Theme.activeHover : "transparent"
                        radius: 4
                    }
                }

                StyledPopup {
                    id: speedPopup
                    x: speedBtn.x + speedBtn.width - width
                    y: -speedPopup.height - 8
                    width: 140
                    padding: 6

                    ListView {
                        anchors.fill: parent
                        implicitHeight: contentHeight
                        model: [3.0, 2.0, 1.5, 1.25, 1.0, 0.5]
                        clip: true
                        spacing: 2

                        delegate: ItemDelegate {
                            required property var modelData

                            width: speedPopup.width - 12
                            hoverEnabled: true

                            contentItem: RowLayout {
                                spacing: 8

                                Label {
                                    text: {
                                        if (modelData === 1) return "1x"
                                        if (modelData === 2) return "2x"
                                        if (modelData === 3) return "3x"
                                        return modelData + "x"
                                    }
                                    color: parent.parent.hovered ? Theme.primary
                                           : (modelData === (Playback.mpv.speed || 1.0) ? Theme.primary : Theme.textSecondary)
                                    font.pixelSize: 13
                                    font.bold: modelData === (Playback.mpv.speed || 1.0)
                                    Layout.fillWidth: true
                                }

                                Icon {
                                    name: "check"
                                    color: Theme.primary
                                    size: 16
                                    visible: modelData === (Playback.mpv.speed || 1.0)
                                }
                            }

                            background: Rectangle {
                                radius: 4
                                color: parent.hovered ? Theme.active : "transparent"
                            }

                            onClicked: {
                                Playback.mpv.setSpeed(modelData)
                                speedPopup.close()
                            }
                        }
                    }
                }

                Icon {
                    name: Playback.volume > 0 ? "volume_up" : "volume_off"
                    color: Theme.textTertiary
                    size: 18
                }

                Slider {
                    id: volumeSlider
                    focusPolicy: Qt.NoFocus
                    Layout.preferredWidth: 80
                    from: 0
                    to: 100
                    value: Playback.volume
                    onMoved: Playback.setVolume(value)

                    background: Rectangle {
                        x: volumeSlider.leftPadding
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - 2
                        implicitHeight: 3
                        width: volumeSlider.availableWidth
                        height: 3
                        radius: 1
                        color: Theme.textDisabled

                        Rectangle {
                            width: volumeSlider.visualPosition * parent.width
                            height: parent.height
                            color: Theme.primary
                            radius: 1
                        }
                    }

                    handle: Rectangle {
                        x: volumeSlider.leftPadding + volumeSlider.visualPosition
                           * (volumeSlider.availableWidth - width)
                        y: volumeSlider.topPadding + volumeSlider.availableHeight / 2 - height / 2
                        implicitWidth: 8
                        implicitHeight: 8
                        radius: 4
                        color: Theme.primary
                    }
                }

                // Fullscreen toggle
                Button {
                    visible: !controlsRoot.volumeOnly
                    focusPolicy: Qt.NoFocus
                    flat: true
                    implicitWidth: 36; implicitHeight: 36
                    onClicked: Playback.toggleFullscreen()

                    contentItem: Icon {
                        name: fullscreen ? "fullscreen_exit" : "fullscreen"
                        color: parent.hovered ? Theme.primary : Theme.textTertiary
                        size: 20
                    }

                    background: Rectangle {
                        color: parent.hovered ? Theme.activeHover : "transparent"
                        radius: 4
                    }
                }
            }
        }
    }

    function formatTime(seconds) {
        if (!seconds || seconds <= 0) return "0:00"
        let totalSec = Math.floor(seconds)
        let h = Math.floor(totalSec / 3600)
        let m = Math.floor((totalSec % 3600) / 60)
        let s = totalSec % 60
        let mm = (h > 0 && m < 10 ? "0" : "") + m
        let ss = (s < 10 ? "0" : "") + s
        return h > 0 ? h + ":" + mm + ":" + ss : m + ":" + ss
    }
}
