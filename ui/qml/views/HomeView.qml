pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: homeFlick

    property Component libCardDelegate
    property Component resumeCardDelegate
    property Component latestCardDelegate

    contentHeight: homeCol.implicitHeight + 20
    clip: true
    interactive: false

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
        minimumSize: 0.08
    }

    SmoothScroller {
        target: homeFlick
    }

    Column {
        id: homeCol
        width: homeFlick.width - 34
        x: 8
        spacing: 16

        // ── Not logged in placeholder ──
        Item {
            width: parent.width
            height: visible ? 200 : 0
            visible: !Server.embyConnected

            Column {
                anchors.centerIn: parent
                spacing: 12
                width: parent.width

                Label {
                    text: Str.homeNotLoggedInPrompt
                    color: Theme.textMuted
                    font.pixelSize: 16
                    anchors.horizontalCenter: parent.horizontalCenter
                }
                Label {
                    text: Str.homeNotLoggedInHint
                    color: Theme.textDimmer
                    font.pixelSize: 12
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 160
                    height: 40

                    background: Rectangle {
                        radius: 8
                        color: parent.hovered ? Theme.primaryHover : Theme.primary
                    }

                    contentItem: Label {
                        text: Str.svrAddServer
                        color: Theme.textPrimary
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: Nav.openLogin()
                }
            }
        }

        // ── 我的媒体 ──
        HorizontalMediaRow {
            sectionTitle: Str.homeMyMedia
            rowHeight: 145
            listModel: Library.libraryModel
            delegate: homeFlick.libCardDelegate
        }

        // ── 继续观看 ──
        HorizontalMediaRow {
            sectionTitle: Str.homeContinueWatching
            rowHeight: 190
            listModel: Detail.resumeModel
            delegate: homeFlick.resumeCardDelegate
        }

        // ── 最新添加 (per library) ──
        Repeater {
            model: Library.latestSections

            HorizontalMediaRow {
                required property var modelData

                sectionTitle: Str.homeLatestPrefix + (modelData.name || "")
                rowHeight: 280
                listModel: modelData.model
                delegate: homeFlick.latestCardDelegate
                width: homeCol.width
            }
        }
    }
}
