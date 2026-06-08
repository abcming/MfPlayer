pragma ComponentBehavior: Bound
pragma ValueTypeBehavior: Assertable
import QtQuick
import QtQuick.Layouts

Item {
    id: root
    property string name: ""
    property color color: "white"
    property int size: 40

    implicitWidth: size
    implicitHeight: size
    Layout.preferredWidth: size
    Layout.preferredHeight: size
    width: size
    height: size
    clip: true

    // All 19 icon shapes as direct children — only the matching one is visible.
    // Inactive shapes have visible:false and cost ~0 GPU (no scene graph node).
    MFIcon_arrow_back      { id: ic_arrow_back;      anchors.fill: parent; color: root.color; visible: root.name === "arrow_back" }
    MFIcon_check           { id: ic_check;           anchors.fill: parent; color: root.color; visible: root.name === "check" }
    MFIcon_chevron_right   { id: ic_chevron_right;   anchors.fill: parent; color: root.color; visible: root.name === "chevron_right" }
    MFIcon_close           { id: ic_close;           anchors.fill: parent; color: root.color; visible: root.name === "close" }
    MFIcon_fullscreen      { id: ic_fullscreen;      anchors.fill: parent; color: root.color; visible: root.name === "fullscreen" }
    MFIcon_fullscreen_exit { id: ic_fullscreen_exit; anchors.fill: parent; color: root.color; visible: root.name === "fullscreen_exit" }
    MFIcon_home            { id: ic_home;            anchors.fill: parent; color: root.color; visible: root.name === "home" }
    MFIcon_movie           { id: ic_movie;           anchors.fill: parent; color: root.color; visible: root.name === "movie" }
    MFIcon_pause           { id: ic_pause;           anchors.fill: parent; color: root.color; visible: root.name === "pause" }
    MFIcon_play_arrow      { id: ic_play_arrow;      anchors.fill: parent; color: root.color; visible: root.name === "play_arrow" }
    MFIcon_playlist_play   { id: ic_playlist_play;   anchors.fill: parent; color: root.color; visible: root.name === "playlist_play" }
    MFIcon_replay          { id: ic_replay;          anchors.fill: parent; color: root.color; visible: root.name === "replay" }
    MFIcon_search          { id: ic_search;          anchors.fill: parent; color: root.color; visible: root.name === "search" }
    MFIcon_skip_next       { id: ic_skip_next;       anchors.fill: parent; color: root.color; visible: root.name === "skip_next" }
    MFIcon_skip_previous   { id: ic_skip_previous;   anchors.fill: parent; color: root.color; visible: root.name === "skip_previous" }
    MFIcon_star            { id: ic_star;            anchors.fill: parent; color: root.color; visible: root.name === "star" }
    MFIcon_subtitles       { id: ic_subtitles;       anchors.fill: parent; color: root.color; visible: root.name === "subtitles" }
    MFIcon_toc             { id: ic_toc;             anchors.fill: parent; color: root.color; visible: root.name === "toc" }
    MFIcon_volume_up       { id: ic_volume_up;       anchors.fill: parent; color: root.color; visible: root.name === "volume_up" }
    MFIcon_volume_off      { id: ic_volume_off;      anchors.fill: parent; color: root.color; visible: root.name === "volume_off" }
    MFIcon_heart           { id: ic_heart;           anchors.fill: parent; color: root.color; visible: root.name === "heart" }
    MFIcon_heart_filled    { id: ic_heart_filled;    anchors.fill: parent; color: root.color; visible: root.name === "heart_filled" }
    MFIcon_sort            { id: ic_sort;            anchors.fill: parent; color: root.color; visible: root.name === "sort" }
    MFIcon_funnel          { id: ic_funnel;          anchors.fill: parent; color: root.color; visible: root.name === "funnel" }
    MFIcon_music_note      { id: ic_music_note;      anchors.fill: parent; color: root.color; visible: root.name === "music_note" }
}
