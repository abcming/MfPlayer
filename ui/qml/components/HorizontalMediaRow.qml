pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

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
    // 外面告诉这一行「整行在视口外」: 卡片照样留着, 只是不画。
    // 只切 visible、不卸 model —— 卸了再挂会在滚动途中同步现建整行卡片 (卡顿根因)
    property bool culled: false
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

    // 外包一层定高 Item: Column 会跳过 visible:false 的子项, 直接隐藏 ListView
    // 整行就塌成只剩标题, 下面的行跟着上移, culled 判断又变, 来回抖
    Item {
        width: root.width
        height: root.rowHeight

        ListView {
            id: listView
            anchors.fill: parent
            model: root.listModel
            visible: !root.culled
            orientation: ListView.Horizontal
            clip: true
            // 缓冲区补到一整屏宽: 拉宽 / 最大化时新露出来的卡早就建好了, 不会在
            // 那一帧同步现建。缓冲区里的卡 ListView 自己会 cull (只建不画), 而且是异步建
            cacheBuffer: Math.max(200, Screen.width - width + 200)
            spacing: root.cardSpacing
            delegate: root.delegate
        }
    }
}
