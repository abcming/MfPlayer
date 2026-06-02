import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

StyledPopup {
    id: root

    property Item anchorTarget: null
    property var serverListModel: []

    signal requestLoginDialog()
    signal openFileDialog()

    x: anchorTarget ? anchorTarget.x + anchorTarget.width + 8 : 0
    y: Math.max(0, (anchorTarget ? anchorTarget.y + anchorTarget.height : 0) - root.height)
    width: 280
    padding: 12
    popupRadius: 10
    bgColor: Theme.panel

    onOpened: root.serverListModel = Server.serverList

    ColumnLayout {
        anchors.fill: parent
        spacing: 6

        Label {
            text: Str.svrTitle
            color: Theme.primary
            font.pixelSize: 14
            font.bold: true
        }

        // Server list
        ListView {
            id: serverListView
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(count * 56, 224)
            model: root.serverListModel
            clip: true
            spacing: 4
            interactive: count > 4

            delegate: Rectangle {
                width: serverListView.width
                height: 52
                radius: 6
                color: modelData.isActive ? Theme.active
                       : (serverMouse.containsMouse ? Theme.activeHover : "transparent")

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 10

                    Rectangle {
                        Layout.preferredWidth: 36
                        Layout.preferredHeight: 36
                        radius: 18
                        color: Theme.panelDeep

                        Label {
                            anchors.centerIn: parent
                            text: (modelData.username || "?")[0].toUpperCase()
                            color: Theme.primary
                            font.pixelSize: 16
                            font.bold: true
                        }

                        RoundedImage {
                            anchors.fill: parent
                            imgRadius: 18
                            embyUrl: modelData.serverUrl && modelData.userId && modelData.token
                                ? modelData.serverUrl + "/emby/Users/" + modelData.userId + "/Images/Primary?maxWidth=64&quality=90&api_key=" + modelData.token
                                : ""
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1

                        Label {
                            text: modelData.username || ""
                            color: modelData.isActive ? Theme.primary : Theme.textPrimary
                            font.pixelSize: 13
                            font.bold: modelData.isActive
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: modelData.serverUrl || ""
                            color: Theme.textMuted
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Icon {
                        name: "check"
                        color: Theme.primary
                        size: 14
                        visible: modelData.isActive
                    }
                }

                MouseArea {
                    id: serverMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        if (!modelData.isActive) {
                            root.close()
                            Server.switchToServer(modelData.id)
                        }
                    }
                }
            }
        }

        // Empty state
        Label {
            text: Str.svrNoServers
            color: Theme.textMuted
            font.pixelSize: 12
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            visible: serverListView.count === 0
            Layout.topMargin: 4
            Layout.bottomMargin: 4
        }

        // Add server button
        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            Layout.topMargin: 4
            flat: true

            background: Rectangle {
                radius: 6
                color: parent.hovered ? Theme.activeHover : Theme.active
            }

            contentItem: RowLayout {
                spacing: 6
                Label {
                    text: "+"
                    color: Theme.primary
                    font.pixelSize: 18
                    font.bold: true
                }
                Label {
                    text: Str.svrAddServer
                    color: Theme.primary
                    font.pixelSize: 13
                }
            }

            onClicked: {
                root.close()
                root.requestLoginDialog()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.active
            visible: Server.embyConnected
        }

        // Settings
        ItemDelegate {
            Layout.fillWidth: true
            background: Rectangle {
                radius: 4
                color: parent.hovered ? Theme.active : "transparent"
            }
            contentItem: Text {
                text: Str.svrSettings
                color: parent.hovered ? Theme.primary : Theme.textSecondary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {
                root.close()
                settingsDialog.open()
            }
        }

        // Play local file
        ItemDelegate {
            Layout.fillWidth: true
            visible: true
            background: Rectangle {
                radius: 4
                color: parent.hovered ? Theme.active : "transparent"
            }
            contentItem: Text {
                text: Str.svrPlayLocalFile
                color: parent.hovered ? Theme.primary : Theme.textSecondary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {
                root.close()
                root.openFileDialog()
            }
        }

        // Refresh cache
        ItemDelegate {
            Layout.fillWidth: true
            background: Rectangle {
                radius: 4
                color: parent.hovered ? Theme.active : "transparent"
            }
            contentItem: Text {
                text: Str.svrRefreshCache
                color: parent.hovered ? Theme.primary : Theme.textSecondary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {
                root.close()
                Server.cache.clearAll()
            }
        }

        // Disconnect
        ItemDelegate {
            Layout.fillWidth: true
            visible: Server.embyConnected
            background: Rectangle {
                radius: 4
                color: parent.hovered ? Theme.active : "transparent"
            }
            contentItem: Text {
                text: Str.svrDisconnect
                color: parent.hovered ? Theme.errorRed : Theme.textSecondary
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: {
                root.close()
                Server.disconnectCurrentServer()
                Nav.popToRoot()
            }
        }
    }

    SettingsDialog { id: settingsDialog }
}
