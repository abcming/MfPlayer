pragma Singleton
import QtQuick

QtObject {
    // ── Material Design 2 Dark Theme ──
    readonly property color primary: "#4285F4"
    readonly property color primaryHover: "#669DF6"
    readonly property color panel: "#1E1E1E"
    readonly property color panelDeep: "#121212"
    readonly property color active: "#2D2D2D"
    readonly property color activeHover: "#3D3D3D"
    readonly property color textPrimary: "#E8EAED"
    readonly property color textSecondary: "#9AA0A6"
    readonly property color textTertiary: "#80868B"
    readonly property color textMuted: "#5F6368"
    readonly property color textDimmer: "#444444"
    readonly property color textDisabled: "#303030"
    readonly property color green: "#34A853"
    readonly property color star: "#FDD663"
    readonly property color errorRed: "#F28B82"
    readonly property color trackBg: "#3C4043"
    readonly property color transparentOverlay: "#cc000000"
    readonly property color modalOverlay: "#80000000"
    readonly property color popupBg: Qt.rgba(18/255, 18/255, 18/255, 0.75)

    readonly property int radiusSmall: 4
    readonly property int radiusMedium: 6
    readonly property int radiusLarge: 8
    readonly property int radiusRound: 18

    readonly property int scrollAnimDuration: 140
    readonly property int popupEnterDuration: 200
    readonly property int popupExitDuration: 150
    readonly property int popupRadius: 8
}
