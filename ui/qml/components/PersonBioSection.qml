pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls

Column {
    id: root
    property bool isPerson: false
    property string premiereDate: ""
    property string endDate: ""
    property var productionLocations: []
    visible: isPerson
    spacing: 4

    Label {
        text: {
            if (!root.premiereDate) return ""
            return Str.detailBorn + ": " + Str.formatDate(root.premiereDate)
        }
        color: Theme.textSecondary
        font.pixelSize: 13
        visible: text !== ""
    }

    Label {
        text: {
            if (!root.endDate) return ""
            return Str.detailDied + ": " + Str.formatDate(root.endDate)
        }
        color: Theme.textSecondary
        font.pixelSize: 13
        visible: text !== ""
    }

    Label {
        text: {
            var locs = root.productionLocations || []
            if (locs.length === 0) return ""
            return Str.detailBirthPlace + ": " + locs.join(", ")
        }
        color: Theme.textSecondary
        font.pixelSize: 13
        visible: text !== ""
    }
}
