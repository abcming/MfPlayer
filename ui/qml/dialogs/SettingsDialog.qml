import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

StyledPopup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 440
    height: 520
    padding: 0
    popupRadius: 12
    bgColor: Theme.panelDeep

    property int currentTab: 0
    property string listeningKey: ""

    property var langModel: [
        {name: "默认 (自动)", code: ""},
        {name: "English", code: "eng"},
        {name: "简体中文", code: "chs"},
        {name: "日本語", code: "jpn"},
        {name: "繁體中文", code: "chi"},
        {name: "العربية", code: "ara"},
        {name: "Norsk Bokmål", code: "nob"},
        {name: "Português", code: "por"},
        {name: "Français", code: "fre"},
    ]

    function langDisplayName(code) {
        for (var i = 0; i < langModel.length; i++)
            if (langModel[i].code === code) return langModel[i].name
        return code || "默认 (自动)"
    }

    function keyName(code) {
        if (code === 0x20) return "空格"
        if (code >= 0x21 && code <= 0x5a) return String.fromCharCode(code)
        if (code === 0x01000012) return "←"
        if (code === 0x01000014) return "→"
        if (code === 0x01000015) return "↑"
        if (code === 0x01000013) return "↓"
        if (code === 0x5b) return "["
        if (code === 0x5d) return "]"
        if (code <= 0) return "未绑定"
        return "Key(" + code + ")"
    }

    function getKey(name) {
        var s = Server.settings
        if (name === "ksb") return s.keySeekBackward
        if (name === "ksf") return s.keySeekForward
        if (name === "kpp") return s.keyPlayPause
        if (name === "kfs") return s.keyFullscreen
        if (name === "kst") return s.keyStats
        if (name === "ksd") return s.keySpeedDown
        if (name === "ksu") return s.keySpeedUp
        if (name === "kvu") return s.keyVolumeUp
        if (name === "kvd") return s.keyVolumeDown
        return 0
    }

    function setKey(name, code) {
        var s = Server.settings
        if (name === "ksb") s.keySeekBackward = code
        else if (name === "ksf") s.keySeekForward = code
        else if (name === "kpp") s.keyPlayPause = code
        else if (name === "kfs") s.keyFullscreen = code
        else if (name === "kst") s.keyStats = code
        else if (name === "ksd") s.keySpeedDown = code
        else if (name === "ksu") s.keySpeedUp = code
        else if (name === "kvu") s.keyVolumeUp = code
        else if (name === "kvd") s.keyVolumeDown = code
    }

    function clearConflict(newKey) {
        var all = ["ksb","ksf","kpp","kfs","kst","ksd","ksu","kvu","kvd"]
        for (var i = 0; i < all.length; i++) {
            if (all[i] !== root.listeningKey && root.getKey(all[i]) === newKey)
                root.setKey(all[i], -1)
        }
    }

    contentItem: Item {
        id: _contentRoot
        focus: true

        Timer { id: _capTimer; interval: 20; onTriggered: _contentRoot.forceActiveFocus() }

        Keys.onPressed: (event) => {
            if (root.listeningKey === "") return
            if (event.key === Qt.Key_Escape) {
                event.accepted = true
                root.listeningKey = ""
                return
            }
            event.accepted = true
            root.clearConflict(event.key)
            root.setKey(root.listeningKey, event.key)
            root.listeningKey = ""
        }

        HdrPqOverlay {
            anchors.fill: parent

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

            // ── Header ──
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 56; color: "transparent"

                Row {
                    anchors { left: parent.left; leftMargin: 20; verticalCenter: parent.verticalCenter }
                    spacing: 4

                    // Tab 0
                    Rectangle {
                        width: _ta0.implicitWidth + 20; height: 30; radius: 15
                        color: root.currentTab === 0 ? Theme.active : "transparent"
                        Label { id: _ta0; anchors.centerIn: parent; text: Str.settingsGeneral; font.pixelSize: 12; color: root.currentTab === 0 ? Theme.textPrimary : Theme.textSecondary }
                        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.currentTab = 0 }
                    }
                    // Tab 1
                    Rectangle {
                        width: _ta1.implicitWidth + 20; height: 30; radius: 15
                        color: root.currentTab === 1 ? Theme.active : "transparent"
                        Label { id: _ta1; anchors.centerIn: parent; text: Str.settingsKeys; font.pixelSize: 12; color: root.currentTab === 1 ? Theme.textPrimary : Theme.textSecondary }
                        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.currentTab = 1 }
                    }
                    // Tab 2
                    Rectangle {
                        width: _ta2.implicitWidth + 20; height: 30; radius: 15
                        color: root.currentTab === 2 ? Theme.active : "transparent"
                        Label { id: _ta2; anchors.centerIn: parent; text: Str.settingsAdvanced; font.pixelSize: 12; color: root.currentTab === 2 ? Theme.textPrimary : Theme.textSecondary }
                        MouseArea { anchors.fill: parent; hoverEnabled: true; onClicked: root.currentTab = 2 }
                    }
                }

                Button {
                    id: _closeBtn
                    anchors { right: parent.right; top: parent.top; margins: 10 }
                    width: 32; height: 32; flat: true
                    contentItem: Icon { name: "close"; color: _closeBtn.hovered ? Theme.textSecondary : Theme.textMuted; size: 16 }
                    background: Rectangle { radius: 16; color: _closeBtn.hovered ? Theme.active : "transparent" }
                    onClicked: root.close()
                }
            }

            Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.active }

            // ── Body ──
            Flickable {
                id: _flick
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                interactive: false
                contentWidth: _content.width
                contentHeight: _content.height + 24

                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                Column {
                    id: _content
                    width: _flick.width - 40; x: 20; y: 12

                    // ════════════════════ TAB 0: 通用 ════════════════════
                    Item { width: parent.width; height: childrenRect.height; visible: root.currentTab === 0

                        Column { width: parent.width; spacing: 14
                            Label { text: "播放"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }

                            // Audio language
                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsAudioLang; color: Theme.textSecondary; font.pixelSize: 12 }
                                Rectangle { id: _aLangBtn; width: parent.width; height: 36; radius: 6; color: Theme.panel
                                    border { color: _aLangPopup.visible ? Theme.primary : Theme.active; width: 1 }
                                    RowLayout { anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                                        Label { text: root.langDisplayName(Server.settings.audioLanguage); color: Theme.textPrimary; font.pixelSize: 13; Layout.fillWidth: true }
                                        Icon { name: "chevron_right"; color: Theme.textMuted; size: 16; rotation: 90 }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: _aLangPopup.open() }
                                }
                                StyledPopup { id: _aLangPopup; y: _aLangBtn.height + 4; width: parent.width; padding: 6; bgColor: Theme.panelDeep
                                    Column { width: parent.width; spacing: 2
                                        Repeater {
                                            model: root.langModel
                                            delegate: ItemDelegate {
                                                required property var modelData
                                                width: _aLangPopup.width - 12; hoverEnabled: true
                                                contentItem: RowLayout {
                                                    Label { text: modelData.name + (modelData.code ? "  (" + modelData.code + ")" : ""); color: parent.hovered ? Theme.primary : Theme.textSecondary; font.pixelSize: 13; Layout.fillWidth: true }
                                                    Icon { name: "check"; color: Theme.primary; size: 16; visible: modelData.code === Server.settings.audioLanguage }
                                                }
                                                background: Rectangle { radius: 4; color: parent.hovered ? Theme.active : "transparent" }
                                                onClicked: { Server.settings.audioLanguage = modelData.code; if (Playback.mpv && Playback.playing) Playback.mpv.setAlang(modelData.code); _aLangPopup.close() }
                                            }
                                        }
                                    }
                                }
                            }

                            // Subtitle language
                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsSubLang; color: Theme.textSecondary; font.pixelSize: 12 }
                                Rectangle { id: _sLangBtn; width: parent.width; height: 36; radius: 6; color: Theme.panel
                                    border { color: _sLangPopup.visible ? Theme.primary : Theme.active; width: 1 }
                                    RowLayout { anchors { fill: parent; leftMargin: 10; rightMargin: 6 }
                                        Label { text: root.langDisplayName(Server.settings.subtitleLanguage); color: Theme.textPrimary; font.pixelSize: 13; Layout.fillWidth: true }
                                        Icon { name: "chevron_right"; color: Theme.textMuted; size: 16; rotation: 90 }
                                    }
                                    MouseArea { anchors.fill: parent; onClicked: _sLangPopup.open() }
                                }
                                StyledPopup { id: _sLangPopup; y: _sLangBtn.height + 4; width: parent.width; padding: 6; bgColor: Theme.panelDeep
                                    Column { width: parent.width; spacing: 2
                                        Repeater {
                                            model: root.langModel
                                            delegate: ItemDelegate {
                                                required property var modelData
                                                width: _sLangPopup.width - 12; hoverEnabled: true
                                                contentItem: RowLayout {
                                                    Label { text: modelData.name + (modelData.code ? "  (" + modelData.code + ")" : ""); color: parent.hovered ? Theme.primary : Theme.textSecondary; font.pixelSize: 13; Layout.fillWidth: true }
                                                    Icon { name: "check"; color: Theme.primary; size: 16; visible: modelData.code === Server.settings.subtitleLanguage }
                                                }
                                                background: Rectangle { radius: 4; color: parent.hovered ? Theme.active : "transparent" }
                                                onClicked: { Server.settings.subtitleLanguage = modelData.code; if (Playback.mpv && Playback.playing) Playback.mpv.setSlang(modelData.code); _sLangPopup.close() }
                                            }
                                        }
                                    }
                                }
                            }

                            // Action after end
                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsActionAfterEnd; color: Theme.textSecondary; font.pixelSize: 12 }
                                Row { spacing: 4
                                    Rectangle { width: _a0t.implicitWidth + 20; height: 30; radius: 15; color: Server.settings.actionAfterEnd === 0 ? Theme.active : "transparent"
                                        Label { id: _a0t; anchors.centerIn: parent; text: Str.settingsActionNext; font.pixelSize: 12; color: Server.settings.actionAfterEnd === 0 ? Theme.primary : Theme.textSecondary; font.bold: Server.settings.actionAfterEnd === 0 }
                                        MouseArea { anchors.fill: parent; onClicked: Server.settings.actionAfterEnd = 0 }
                                    }
                                    Rectangle { width: _a1t.implicitWidth + 20; height: 30; radius: 15; color: Server.settings.actionAfterEnd === 1 ? Theme.active : "transparent"
                                        Label { id: _a1t; anchors.centerIn: parent; text: Str.settingsActionLoop; font.pixelSize: 12; color: Server.settings.actionAfterEnd === 1 ? Theme.primary : Theme.textSecondary; font.bold: Server.settings.actionAfterEnd === 1 }
                                        MouseArea { anchors.fill: parent; onClicked: Server.settings.actionAfterEnd = 1 }
                                    }
                                    Rectangle { width: _a2t.implicitWidth + 20; height: 30; radius: 15; color: Server.settings.actionAfterEnd === 2 ? Theme.active : "transparent"
                                        Label { id: _a2t; anchors.centerIn: parent; text: Str.settingsActionStop; font.pixelSize: 12; color: Server.settings.actionAfterEnd === 2 ? Theme.primary : Theme.textSecondary; font.bold: Server.settings.actionAfterEnd === 2 }
                                        MouseArea { anchors.fill: parent; onClicked: Server.settings.actionAfterEnd = 2 }
                                    }
                                }
                            }

                            // HDR
                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsHdrBrightness; color: Theme.textSecondary; font.pixelSize: 12 }
                                RowLayout { width: parent.width; spacing: 10
                                    Slider { id: _hdr; Layout.fillWidth: true; from: 100; to: 4000; stepSize: 100; value: Server.settings.hdrPeakBrightness
                                        onMoved: { Playback.setHdrPeakBrightness(value); Server.settings.hdrPeakBrightness = value } }
                                    Label { text: _hdr.value + " nits"; color: Theme.textMuted; font.pixelSize: 11; Layout.preferredWidth: 65 }
                                }
                            }

                            // SSL
                            RowLayout { width: parent.width; height: 32; spacing: 8
                                CheckBox {
                                    id: _sslSkip
                                    checked: Server.settings.skipSslVerify
                                    onCheckedChanged: Server.settings.skipSslVerify = checked
                                }
                                Label {
                                    text: Str.svrSkipSslVerify
                                    color: Theme.textSecondary; font.pixelSize: 12
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }

                            Rectangle { width: parent.width; height: 1; color: Theme.active }

                            // About
                            Label { text: Str.settingsAbout; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }
                            Row { spacing: 8; Label { text: "MfPlayer"; color: Theme.textSecondary; font.pixelSize: 12; font.bold: true; width: 80 } Label { text: _appVersion || "v1.0"; color: Theme.textMuted; font.pixelSize: 12 } }
                            Row { spacing: 8; Label { text: "mpv"; color: Theme.textSecondary; font.pixelSize: 12; font.bold: true; width: 80 } Label { text: Playback.mpv ? (Playback.mpv.mpvVersion || "?") : "?"; color: Theme.textMuted; font.pixelSize: 12 } }
                            Row { spacing: 8; Label { text: "Qt"; color: Theme.textSecondary; font.pixelSize: 12; font.bold: true; width: 80 } Label { text: _qtVersion || "?"; color: Theme.textMuted; font.pixelSize: 12 } }
                        }
                    }

                    // ════════════════════ TAB 1: 按键 ════════════════════
                    Item { width: parent.width; height: childrenRect.height; visible: root.currentTab === 1

                        Column { width: parent.width; spacing: 10
                            Label { text: "点击按键框 → 再按新键 → 即时生效"; color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }

                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keySeekBackward; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb0r; width: Math.max(_kb0t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "ksb" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "ksb" ? Theme.primary : "transparent"; width: root.listeningKey === "ksb" ? 2 : 0 }
                                    Label { id: _kb0t; anchors.centerIn: parent
                                        text: root.listeningKey === "ksb" ? "···" : root.keyName(root.getKey("ksb"))
                                        color: root.listeningKey === "ksb" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "ksb" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "ksb") root.listeningKey = ""; else { root.listeningKey = "ksb"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keySeekForward; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb1r; width: Math.max(_kb1t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "ksf" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "ksf" ? Theme.primary : "transparent"; width: root.listeningKey === "ksf" ? 2 : 0 }
                                    Label { id: _kb1t; anchors.centerIn: parent
                                        text: root.listeningKey === "ksf" ? "···" : root.keyName(root.getKey("ksf"))
                                        color: root.listeningKey === "ksf" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "ksf" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "ksf") root.listeningKey = ""; else { root.listeningKey = "ksf"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keyPlayPause; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb2r; width: Math.max(_kb2t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "kpp" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "kpp" ? Theme.primary : "transparent"; width: root.listeningKey === "kpp" ? 2 : 0 }
                                    Label { id: _kb2t; anchors.centerIn: parent
                                        text: root.listeningKey === "kpp" ? "···" : root.keyName(root.getKey("kpp"))
                                        color: root.listeningKey === "kpp" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "kpp" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "kpp") root.listeningKey = ""; else { root.listeningKey = "kpp"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keyFullscreen; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb3r; width: Math.max(_kb3t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "kfs" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "kfs" ? Theme.primary : "transparent"; width: root.listeningKey === "kfs" ? 2 : 0 }
                                    Label { id: _kb3t; anchors.centerIn: parent
                                        text: root.listeningKey === "kfs" ? "···" : root.keyName(root.getKey("kfs"))
                                        color: root.listeningKey === "kfs" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "kfs" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "kfs") root.listeningKey = ""; else { root.listeningKey = "kfs"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keyStats; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb4r; width: Math.max(_kb4t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "kst" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "kst" ? Theme.primary : "transparent"; width: root.listeningKey === "kst" ? 2 : 0 }
                                    Label { id: _kb4t; anchors.centerIn: parent
                                        text: root.listeningKey === "kst" ? "···" : root.keyName(root.getKey("kst"))
                                        color: root.listeningKey === "kst" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "kst" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "kst") root.listeningKey = ""; else { root.listeningKey = "kst"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keySpeedDown; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb5r; width: Math.max(_kb5t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "ksd" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "ksd" ? Theme.primary : "transparent"; width: root.listeningKey === "ksd" ? 2 : 0 }
                                    Label { id: _kb5t; anchors.centerIn: parent
                                        text: root.listeningKey === "ksd" ? "···" : root.keyName(root.getKey("ksd"))
                                        color: root.listeningKey === "ksd" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "ksd" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "ksd") root.listeningKey = ""; else { root.listeningKey = "ksd"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keySpeedUp; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb6r; width: Math.max(_kb6t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "ksu" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "ksu" ? Theme.primary : "transparent"; width: root.listeningKey === "ksu" ? 2 : 0 }
                                    Label { id: _kb6t; anchors.centerIn: parent
                                        text: root.listeningKey === "ksu" ? "···" : root.keyName(root.getKey("ksu"))
                                        color: root.listeningKey === "ksu" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "ksu" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "ksu") root.listeningKey = ""; else { root.listeningKey = "ksu"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keyVolumeUp; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb7r; width: Math.max(_kb7t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "kvu" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "kvu" ? Theme.primary : "transparent"; width: root.listeningKey === "kvu" ? 2 : 0 }
                                    Label { id: _kb7t; anchors.centerIn: parent
                                        text: root.listeningKey === "kvu" ? "···" : root.keyName(root.getKey("kvu"))
                                        color: root.listeningKey === "kvu" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "kvu" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "kvu") root.listeningKey = ""; else { root.listeningKey = "kvu"; _capTimer.start() } } }
                                }
                            }
                            RowLayout { spacing: 10; width: parent.width
                                Label { text: Str.keyVolumeDown; color: Theme.textSecondary; font.pixelSize: 13; Layout.preferredWidth: 80 }
                                Rectangle { id: _kb8r; width: Math.max(_kb8t.implicitWidth + 20, 80); height: 32; radius: 6
                                    color: root.listeningKey === "kvd" ? Theme.primary : Theme.active
                                    border { color: root.listeningKey === "kvd" ? Theme.primary : "transparent"; width: root.listeningKey === "kvd" ? 2 : 0 }
                                    Label { id: _kb8t; anchors.centerIn: parent
                                        text: root.listeningKey === "kvd" ? "···" : root.keyName(root.getKey("kvd"))
                                        color: root.listeningKey === "kvd" ? Theme.textPrimary : Theme.textSecondary; font.pixelSize: 13; font.bold: root.listeningKey === "kvd" }
                                    MouseArea { anchors.fill: parent; onClicked: { if (root.listeningKey === "kvd") root.listeningKey = ""; else { root.listeningKey = "kvd"; _capTimer.start() } } }
                                }
                            }
                        }
                    }

                    // ════════════════════ TAB 2: 高级 ════════════════════
                    Item { width: parent.width; height: childrenRect.height; visible: root.currentTab === 2

                        Column { width: parent.width; spacing: 14
                            Label { text: "快进退步长"; color: Theme.textMuted; font.pixelSize: 11; font.bold: true }

                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsSeekForward; color: Theme.textSecondary; font.pixelSize: 12 }
                                RowLayout { width: parent.width; spacing: 8
                                    Label { text: _sfw.value + " 秒"; color: Theme.textPrimary; font.pixelSize: 13; Layout.preferredWidth: 60 }
                                    Slider { id: _sfw; Layout.fillWidth: true; from: 1; to: 60; stepSize: 1; value: Server.settings.seekForwardStep; onMoved: Server.settings.seekForwardStep = value }
                                }
                            }

                            Column { width: parent.width; spacing: 4
                                Label { text: Str.settingsSeekBackward; color: Theme.textSecondary; font.pixelSize: 12 }
                                RowLayout { width: parent.width; spacing: 8
                                    Label { text: _sbw.value + " 秒"; color: Theme.textPrimary; font.pixelSize: 13; Layout.preferredWidth: 60 }
                                    Slider { id: _sbw; Layout.fillWidth: true; from: 1; to: 60; stepSize: 1; value: Server.settings.seekBackwardStep; onMoved: Server.settings.seekBackwardStep = value }
                                }
                            }

                            Item { width: parent.width; height: 8 }

                            // Label { text: "← → 快退/快进  |  空格 播放/暂停  |  F 全屏  |  I 统计  |  [ ] 变速  |  ↑ ↓ 音量"; color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.Wrap; width: parent.width }
                        }
                    }
                }
            }
            }  // ColumnLayout
        }  // HdrPqOverlay
    }

    onListeningKeyChanged: { if (root.listeningKey !== "") _capTimer.start() }
    onCurrentTabChanged: _flick.contentY = 0
    onClosed: root.listeningKey = ""
}
