pragma Singleton
import QtQuick
import QtQuick.Controls

// Centralized navigation actions. Eliminates implicit scope chain coupling
// where child components accessed pageStack/detailPage/playerPage from
// Main.qml's scope without declaring the dependency.
//
// Main.qml sets pageStack and the Component references in onCompleted.
// All other components call Nav.pushDetail(itemId) etc. instead of
// reaching up through the QML scope chain.
QtObject {
    property StackView pageStack: null
    property Component detailPage: null
    property Component playerPage: null
    property Component browsePage: null
    property var loginDialog: null

    readonly property int depth: pageStack ? pageStack.depth : 0

    function pushDetail(itemId) {
        if (pageStack && detailPage)
            pageStack.push(detailPage, {itemId: itemId})
    }

    function pushPlayer(props, immediate) {
        if (pageStack && playerPage) {
            var op = immediate ? StackView.Immediate : StackView.Transition
            pageStack.push(playerPage, props || {}, op)
        }
    }

    // 卡片上的播放钮走这里 —— 「继续观看」/「收藏的集」/媒体库「集」Tab 共用一套，
    // 点了直接起播，不用先进详情页。
    //
    // 和 DetailPage.pushPlayerPage() 有两点不同，都是刻意的：
    //   1. 不传 itemData。卡片手上只有 model 的几个 role，没有完整详情。
    //      PlaybackController 会用 PlaybackInfo 回来的 MediaSources 补上，
    //      版本选择器照常能用（这条路正是 audit B-2 修好的）。
    //   2. playlistData 先只放当前这一集，起播不等网络。PlayerPage 拿 seriesId +
    //      seasonId 调 Playback.loadPlaylist() 去补同季列表，回来了再替换 ——
    //      上下集钮、播放列表面板、播完自动接下一集都靠它。
    function playCard(c) {
        var isEpisode = c.itemType === Str.typeEpisode
        pushPlayer({
            itemId: c.itemId,
            episodeTitle: (isEpisode && c.seriesName) ? c.seriesName : (c.itemName || ""),
            episodeSubtitle: isEpisode ? Str.episodeFullLabel(c.indexNumber, c.itemName) : "",
            episodeIndex: -1,
            playlistData: [{
                itemId: c.itemId,
                itemName: c.itemName || "",
                indexNumber: c.indexNumber || 0
            }],
            itemType: c.itemType || "",
            startTimeTicks: c.startTicks || 0,
            mediaSourceId: "",
            audioIndex: -1,
            subtitleIndex: -1,
            // 只有「集」需要补列表。电影/剧集这两个是空的，PlayerPage 就不会去补
            seriesId: isEpisode ? (c.seriesId || "") : "",
            seasonId: isEpisode ? (c.seasonId || "") : ""
        })
    }

    function pop() {
        if (pageStack) pageStack.pop()
    }

    function popToRoot() {
        if (pageStack) while (pageStack.depth > 1) pageStack.pop()
    }

    function openLogin() {
        if (loginDialog) loginDialog.open()
    }
}
