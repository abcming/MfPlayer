import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Labeled horizontal media row: section title + horizontal ListView.
// Used for "继续观看", "最新添加", "演职人员", "相似推荐", "我的媒体" etc.

Column {
    id: root

    property string sectionTitle: ""
    property int count: -1
    property var listModel: []
    property Component delegate: null
    property real rowHeight: 200
    property real cardSpacing: 12
    property real titleFontSize: 16
    // External visibility gate — combined with internal listView.count > 0.
    // Setting visible from outside would override this binding; use this instead.
    property bool extraVisibleCondition: true

    width: parent ? parent.width : 0
    spacing: 8
    visible: listView.count > 0 && extraVisibleCondition

    RowLayout {
        spacing: 6
        visible: root.sectionTitle !== ""

        Label {
            text: root.sectionTitle
            color: Theme.primary
            font.pixelSize: root.titleFontSize
            font.bold: true
        }

        Label {
            text: root.count >= 0 ? root.count : ""
            color: Theme.textMuted
            font.pixelSize: root.titleFontSize - 4
            visible: root.count >= 0
        }
    }

    ListView {
        id: listView
        width: root.width
        height: root.rowHeight
        model: root.listModel
        orientation: ListView.Horizontal
        clip: true
        cacheBuffer: 200
        spacing: root.cardSpacing
        delegate: root.delegate
    }
}
