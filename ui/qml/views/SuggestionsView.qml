pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Flickable {
    id: root

    property Component resumeCardDelegate
    property Component latestCardDelegate

    contentHeight: sugCol.implicitHeight + 20
    clip: true
    interactive: false

    ScrollBar.vertical: ScrollBar {
        policy: ScrollBar.AsNeeded
        minimumSize: 0.08
    }

    SmoothScroller {
        target: root
    }

    Column {
        id: sugCol
        width: root.width - 34
        x: 8
        spacing: 16

        HorizontalMediaRow {
            sectionTitle: Str.homeContinueWatching
            rowHeight: 190
            listModel: Library.suggestionsResumeModel
            delegate: root.resumeCardDelegate
        }

        HorizontalMediaRow {
            sectionTitle: Str.homeLatestPrefix
            rowHeight: 280
            listModel: Library.suggestionsLatestModel
            delegate: root.latestCardDelegate
        }
    }
}
