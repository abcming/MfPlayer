pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    visible: false
    width: 1280
    height: 720
    minimumWidth: 1280
    minimumHeight: 720
    color: Theme.panelDeep
    title: Qt.application.name

    FontLoader { source: "file:" + _appDir + "/fonts/SourceHanSans-Regular.ttc" }
    FontLoader { source: "file:" + _appDir + "/fonts/SourceHanSans-Medium.ttc" }
    FontLoader { source: "file:" + _appDir + "/fonts/SourceHanSans-Bold.ttc" }

    font.family: "Source Han Sans HC"

    property bool loggedIn: Server.embyConnected
    onLoggedInChanged: {
        if (loggedIn) loginDialog.close()
    }

    property int fsDepth: 0
    property int _prevDepth: 1

    function toggleMaximize() {
        if (root.visibility === Window.FullScreen)
            root.showNormal()
        else if (root.visibility === Window.Maximized)
            root.showNormal()
        else
            root.showMaximized()
    }

    Connections {
        target: Playback
        function onFullscreenChanged() {
            if (Playback.fullscreen)
                fsDepth = pageStack.depth
        }
    }

    Connections {
        target: Server
        function onLoggedOut() {
            Nav.popToRoot()
        }
    }

    function saveWindowSize() {
        if (root.visibility === Window.Windowed) {
            Server.settings.windowWidth = root.width
            Server.settings.windowHeight = root.height
        }
    }

    onWidthChanged: saveTimer.restart()
    onHeightChanged: saveTimer.restart()

    Timer {
        id: saveTimer
        interval: 500
        onTriggered: saveWindowSize()
    }

    Component.onCompleted: {
        Nav.pageStack = pageStack
        Nav.detailPage = detailPage
        Nav.playerPage = playerPage
        Nav.browsePage = browsePage
        Nav.loginDialog = loginDialog

        let sw = Server.settings.windowWidth
        let sh = Server.settings.windowHeight
        if (sw > 0 && sh > 0) {
            root.width = sw
            root.height = sh
        }

        // Try restore from saved active server (SQLite), fallback to INI
        Server.restoreSession()

        // System "Open with" — auto-play local file passed via command line
        if (_startupFile) {
            pageStack.push(playerPage, { localFile: _startupFile }, StackView.Immediate)
        }
    }

    // Prewarm the rounded-mask shader at startup so the GPU driver compiles it
    // before the first RoundedImage appears — avoids 50-150ms stall on first push.
    Item {
        visible: false
        width: 1; height: 1
        ShaderEffect {
            anchors.fill: parent
            fragmentShader: "qrc:/qt/qml/mfplayer/roundedmask.frag.qsb"
            property var source: null
            property real radius: 0
            property color bgColor: "black"
            property vector2d imgSize: Qt.vector2d(1, 1)
            property vector2d sourceSize: Qt.vector2d(1, 1)
        }
    }

    // ── HDR startup cover ───────────────────────────────────────────
    // During the first ~300ms after launch, _hdrActive is undefined but the
    // HDR10 swapchain (R10G10B10A2, Rec.2020 PQ) is already active.  Without
    // this cover, the sRGB QML UI renders as raw PQ electrical values and
    // appears severely overexposed (white-hot) until HdrPqOverlay's shader
    // activates.  This black overlay hides the window until the HDR state is
    // known.  On SDR systems _hdrActive becomes false and the cover lifts
    // immediately; on HDR it becomes true and the PQ-corrected UI is revealed.
    Rectangle {
        id: hdrStartupCover
        anchors.fill: parent
        color: "black"
        z: 9999
        opacity: (typeof _hdrActive !== "undefined") ? 0 : 1
        Behavior on opacity { NumberAnimation { duration: 80 } }
    }

    StackView {
        id: pageStack
        anchors {
            top: parent.top; left: parent.left; right: parent.right; bottom: parent.bottom
        }
        initialItem: browsePage

        // iOS-style push: new page slides over old from right
        pushEnter: Transition {
            NumberAnimation { property: "x"; from: root.width; to: 0; duration: 350; easing.type: Easing.OutCubic }
        }
        pushExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: -root.width * 0.2; duration: 350; easing.type: Easing.OutCubic }
        }
        popEnter: Transition {
            NumberAnimation { property: "x"; from: -root.width * 0.2; to: 0; duration: 350; easing.type: Easing.OutCubic }
        }
        popExit: Transition {
            NumberAnimation { property: "x"; from: 0; to: root.width; duration: 350; easing.type: Easing.OutCubic }
        }

        onDepthChanged: {
            if (pageStack.depth === 1)
                Detail.refreshResume()
            if (root.visibility === Window.FullScreen && pageStack.depth < fsDepth)
                root.showNormal()
            // DetailPage will handle restoration via onVisibleChanged
            _prevDepth = pageStack.depth
        }
    }

    Component { id: browsePage; BrowsePage {} }
    Component { id: detailPage; DetailPage {} }
    Component { id: playerPage; PlayerPage {} }

    Dialog {
        id: loginDialog
        modal: true
        closePolicy: Popup.NoAutoClose
        anchors.centerIn: parent
        width: 400
        title: ""
        padding: 0

        Overlay.modal: Rectangle {
            color: Theme.modalOverlay
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        enter: Transition {
            NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: 200; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 200 }
        }
        exit: Transition {
            NumberAnimation { property: "scale"; from: 1.0; to: 0.95; duration: 150; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 150 }
        }

        background: Rectangle {
            radius: 12
            color: Theme.panel
            border { color: Theme.active; width: 1 }
        }

        contentItem: Item {
            layer.enabled: typeof _hdrActive !== "undefined" && _hdrActive
            layer.format: ShaderEffectSource.RGBA16F
            layer.effect: ShaderEffect {
                property real sdrWhiteNits: Server.settings.sdrWhiteNits
                vertexShader: "qrc:/qt/qml/mfplayer/hdr_pq.vert.qsb"
                fragmentShader: "qrc:/qt/qml/mfplayer/hdr_pq.frag.qsb"
            }

            implicitHeight: {
                var h = 0
                for (var i = 0; i < children.length; i++) {
                    var c = children[i]
                    if (!c.visible) continue
                    var bh = c.y + (c.implicitHeight || 0)
                    if (bh > h) h = bh
                }
                return h
            }

            ColumnLayout {
                spacing: 0

            // Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 100
                color: "transparent"

                // Close button
                Button {
                    id: loginCloseBtn
                    anchors { right: parent.right; top: parent.top; margins: 8 }
                    width: 32; height: 32
                    flat: true

                    contentItem: Icon {
                        name: "close"
                        color: loginCloseBtn.hovered ? Theme.textSecondary : Theme.textMuted
                        size: 16
                    }

                    background: Rectangle {
                        radius: 16
                        color: loginCloseBtn.hovered ? Theme.active : "transparent"
                    }

                    onClicked: loginDialog.close()
                }

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 4

                    Label {
                        text: Qt.application.name
                        color: Theme.primary
                        font.pixelSize: 28
                        font.bold: true
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: Str.loginTitle
                        color: Theme.textTertiary
                        font.pixelSize: 13
                        Layout.alignment: Qt.AlignHCenter
                    }
                }
            }

            // Form
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 32
                Layout.bottomMargin: 24
                spacing: 14

                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true

                    Label {
                        text: Str.loginServerAddress
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    TextField {
                        id: serverField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: Server.settings.embyServer || "https://"
                        placeholderText: "http://your-emby-server:8096"
                        placeholderTextColor: Theme.textMuted
                        color: Theme.textPrimary
                        font.pixelSize: 14

                        background: Rectangle {
                            radius: 8
                            color: Theme.panelDeep
                            border { color: serverField.activeFocus ? Theme.primary : Theme.active; width: 1 }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true

                    Label {
                        text: Str.loginUsername
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    TextField {
                        id: userField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        text: Server.settings.embyUsername
                        color: Theme.textPrimary
                        font.pixelSize: 14

                        background: Rectangle {
                            radius: 8
                            color: Theme.panelDeep
                            border { color: userField.activeFocus ? Theme.primary : Theme.active; width: 1 }
                        }
                    }
                }

                ColumnLayout {
                    spacing: 4
                    Layout.fillWidth: true

                    Label {
                        text: Str.loginPassword
                        color: Theme.textSecondary
                        font.pixelSize: 12
                    }
                    TextField {
                        id: passField
                        Layout.fillWidth: true
                        Layout.preferredHeight: 40
                        echoMode: TextInput.Password
                        color: Theme.textPrimary
                        font.pixelSize: 14

                        background: Rectangle {
                            radius: 8
                            color: Theme.panelDeep
                            border { color: passField.activeFocus ? Theme.primary : Theme.active; width: 1 }
                        }
                    }
                }

                Label {
                    id: errorLabel
                    visible: false
                    color: Theme.errorRed
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    Layout.topMargin: 2
                }

                Button {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 44
                    Layout.topMargin: 6
                    enabled: serverField.text && userField.text && passField.text

                    background: Rectangle {
                        radius: 8
                        color: parent.enabled
                            ? (parent.hovered ? Theme.primaryHover : Theme.primary)
                            : Theme.active
                    }

                    contentItem: Label {
                        text: Str.loginConnect
                        color: parent.enabled ? Theme.textPrimary : Theme.textMuted
                        font.pixelSize: 15
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    onClicked: {
                        errorLabel.visible = false
                        Server.connectEmby(serverField.text, userField.text, passField.text)
                    }
                }
            }  // Form ColumnLayout
        }  // contentItem ColumnLayout
        }  // wrapper Item

        Connections {
            target: Server
            enabled: loginDialog.visible
            function onPlayError(msg) {
                errorLabel.text = msg
                errorLabel.visible = true
            }
        }
    }

    // 全局错误提示弹窗
    Dialog {
        id: globalErrorDialog
        anchors.centerIn: parent
        width: 320
        modal: true
        title: ""
        padding: 0

        Overlay.modal: Rectangle {
            color: Theme.modalOverlay
            Behavior on opacity { NumberAnimation { duration: 200 } }
        }

        enter: Transition {
            NumberAnimation { property: "scale"; from: 0.95; to: 1.0; duration: 200; easing.type: Easing.OutCubic }
            NumberAnimation { property: "opacity"; from: 0.0; to: 1.0; duration: 200 }
        }
        exit: Transition {
            NumberAnimation { property: "scale"; from: 1.0; to: 0.95; duration: 150; easing.type: Easing.InCubic }
            NumberAnimation { property: "opacity"; from: 1.0; to: 0.0; duration: 150 }
        }

        background: Rectangle {
            radius: 8
            color: Theme.panel
            border { color: Theme.active; width: 1 }
        }

        contentItem: Item {
            layer.enabled: typeof _hdrActive !== "undefined" && _hdrActive
            layer.format: ShaderEffectSource.RGBA16F
            layer.effect: ShaderEffect {
                property real sdrWhiteNits: Server.settings.sdrWhiteNits
                vertexShader: "qrc:/qt/qml/mfplayer/hdr_pq.vert.qsb"
                fragmentShader: "qrc:/qt/qml/mfplayer/hdr_pq.frag.qsb"
            }

            implicitHeight: {
                var h = 0
                for (var i = 0; i < children.length; i++) {
                    var c = children[i]
                    if (!c.visible) continue
                    var bh = c.y + (c.implicitHeight || 0)
                    if (bh > h) h = bh
                }
                return h
            }

            ColumnLayout {
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                color: "transparent"

                Label {
                    anchors.centerIn: parent
                    text: Str.dlgNotice
                    color: Theme.textPrimary
                    font.pixelSize: 15
                    font.bold: true
                }
            }

            Label {
                id: globalErrorText
                text: ""
                color: Theme.textSecondary
                font.pixelSize: 14
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                Layout.margins: 20
                horizontalAlignment: Text.AlignHCenter
            }

            Button {
                Layout.alignment: Qt.AlignHCenter
                Layout.margins: 16
                Layout.preferredWidth: 100
                Layout.preferredHeight: 36

                background: Rectangle {
                    radius: 6
                    color: parent.hovered ? Theme.primaryHover : Theme.primary
                }

                contentItem: Label {
                    text: Str.dlgConfirm
                    color: Theme.textPrimary
                    font.pixelSize: 14
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                onClicked: globalErrorDialog.close()
            }
        }  // contentItem ColumnLayout
        }  // wrapper Item
    }

    Connections {
        target: Server
        function onPlayError(msg) {
            if (!loginDialog.visible) {
                globalErrorText.text = msg
                globalErrorDialog.open()
            }
        }
    }
}
