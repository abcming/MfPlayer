import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var itemData: ({})
    property string itemId: ""
    property int dataVersion: 0
    property int selectedAudioIdx: -1
    property int selectedSubIdx: -2
    property int kResumeThresholdTicks: 300000000

    signal playRequested(var resumeTicks)
    signal restartRequested()
    signal togglePlayed()
    signal toggleFavorite()
    signal nextEpisodePlayRequested(var nextEp, var resumeTicks)

    spacing: 4

    // Play button + Status
    RowLayout {
        id: playBtnRow
        Layout.topMargin: 4
        spacing: 10
        visible: !root._isPerson

        property bool isSeries: (root.itemData.Type || "") === Str.typeSeries
        property var ne: isSeries ? Detail.nextEpisode : null
        property bool neResumable: !!(isSeries && ne && ne.PlaybackPositionTicks >= root.kResumeThresholdTicks)
        property bool neAvailable: !!(isSeries && ne && ne.Id)
        readonly property bool _isPerson: (root.itemData.Type || "") === Str.typePerson

        // Smart play button
        Button {
            id: playBtn
            property bool resumable: {
                void root.dataVersion
                if (parent.neResumable) return true
                let ud = root.itemData.UserData || {}
                return ud.PlaybackPositionTicks >= root.kResumeThresholdTicks && !ud.Played
            }
            Layout.preferredWidth: resumable ? 140 : 120
            Layout.preferredHeight: 40
            enabled: root.itemId !== ""

            background: Rectangle {
                radius: 8
                color: playBtn.hovered ? Theme.primaryHover : Theme.primary
            }

            contentItem: RowLayout {
                spacing: 6
                Icon {
                    name: "play_arrow"
                    color: Theme.textPrimary
                    size: 20
                }
                Label {
                    text: playBtn.resumable ? Str.detailResume : Str.detailPlay
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                }
            }

            onClicked: {
                if (parent.neAvailable) {
                    let ne = parent.ne
                    let resumeTicks = parent.neResumable
                        ? ne.PlaybackPositionTicks : 0
                    root.nextEpisodePlayRequested(ne, resumeTicks)
                } else {
                    let ud = root.itemData.UserData || {}
                    let resumeTicks = (ud.PlaybackPositionTicks >= root.kResumeThresholdTicks && !ud.Played)
                        ? ud.PlaybackPositionTicks : 0
                    root.playRequested(resumeTicks)
                }
            }
        }

        // Restart from beginning
        Button {
            Layout.preferredWidth: 120; Layout.preferredHeight: 40
            visible: {
                let ud = root.itemData.UserData || {}
                return ud.PlaybackPositionTicks >= root.kResumeThresholdTicks && !ud.Played
            }

            background: Rectangle {
                radius: 8
                color: parent.hovered ? Theme.activeHover : Theme.active
                border { color: Theme.primary; width: 1 }
            }

            contentItem: RowLayout {
                spacing: 4
                Icon {
                    name: "replay"
                    color: Theme.primary
                    size: 16
                }
                Label {
                    text: Str.detailRestart
                    color: Theme.primary
                    font.pixelSize: 13
                }
            }

            onClicked: root.restartRequested()
        }

        // Mark played / unplayed toggle
        Button {
            Layout.preferredWidth: 30; Layout.preferredHeight: 30
            visible: root.itemData.UserData !== undefined

            property bool isPlayed: {
                void root.dataVersion
                let ud = root.itemData.UserData || {}
                return ud.Played === true
            }

            background: Rectangle {
                radius: 15
                color: parent.hovered ? (parent.isPlayed ? Qt.rgba(66/255, 133/255, 244/255, 0.15) : Qt.rgba(1, 1, 1, 0.1))
                                      : "transparent"
                border { color: parent.isPlayed ? Theme.primary : Theme.textSecondary; width: 1.5 }
            }

            contentItem: Icon {
                name: "check"
                color: parent.isPlayed ? Theme.primary : Theme.textSecondary
                size: 16
            }

            onClicked: root.togglePlayed()
        }

        // Favorite toggle
        Button {
            Layout.preferredWidth: 30; Layout.preferredHeight: 30
            visible: root.itemData.UserData !== undefined

            property bool isFavorite: {
                void root.dataVersion
                let ud = root.itemData.UserData || {}
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

            onClicked: root.toggleFavorite()
        }

        Label {
            text: {
                let t = root.itemData.Type || ""
                if (t !== Str.typeSeries) return ""
                let st = root.itemData.Status || ""
                return st === "Continuing" ? Str.detailStatusContinuing : (st === "Ended" ? Str.detailStatusEnded : "")
            }
            color: Theme.green
            font.pixelSize: 13
            visible: text !== ""
        }
    }

    // Series next-up info
    ColumnLayout {
        id: nextUpInfo
        visible: playBtnRow.neAvailable
        spacing: 4

        property var ne: playBtnRow.ne
        property double pct: ne && root.itemData.RunTimeTicks > 0
            ? Math.min((ne.PlaybackPositionTicks || 0) / root.itemData.RunTimeTicks * 100, 100)
            : 0

        Label {
            text: {
                let ne = playBtnRow.ne
                if (!ne) return ""
                return "S" + (ne.ParentIndexNumber || "?")
                    + ":E" + (ne.IndexNumber || "")
                    + " - " + (ne.Name || "")
            }
            color: Theme.textSecondary
            font.pixelSize: 13
        }

        RowLayout {
            visible: playBtnRow.neResumable
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.maximumWidth: 250
                Layout.preferredHeight: 3
                radius: 1
                color: Theme.trackBg

                Rectangle {
                    height: parent.height
                    radius: 1
                    color: Theme.green
                    width: parent.width * nextUpInfo.pct / 100
                }
            }

            Label {
                text: {
                    let ne = playBtnRow.ne
                    if (!ne) return ""
                    let ticks = ne.PlaybackPositionTicks || 0
                    let total = root.itemData.RunTimeTicks || 0
                    if (ticks <= 0 || !total) return ""
                    return Str.remainingTimeCompact(total, ticks)
                }
                color: Theme.textMuted
                font.pixelSize: 11
            }
        }
    }

    // Progress bar + remaining time
    RowLayout {
        spacing: 8
        visible: {
            if (playBtnRow._isPerson) return false
            void root.dataVersion
            let ud = root.itemData.UserData || {}
            return ud.PlaybackPositionTicks >= root.kResumeThresholdTicks && !ud.Played
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.maximumWidth: 250
            Layout.preferredHeight: 3
            radius: 1
            color: Theme.trackBg

            Rectangle {
                height: parent.height
                radius: 1
                color: Theme.green
                width: {
                    let ud = root.itemData.UserData || {}
                    let ticks = ud.PlaybackPositionTicks || 0
                    let total = root.itemData.RunTimeTicks || 1
                    return Math.min(ticks / total, 1.0) * parent.width
                }
            }
        }

        Label {
            color: Theme.textMuted
            font.pixelSize: 11
            visible: text !== ""
            text: {
                let ud = root.itemData.UserData || {}
                let ticks = ud.PlaybackPositionTicks || 0
                let total = root.itemData.RunTimeTicks || 0
                if (ticks <= 0 || !total) return ""
                return Str.remainingTimeLong(total, ticks)
            }
        }
    }
}
