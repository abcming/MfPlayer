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
    //   2. playlistData 只有当前这一集 —— 卡片拿不到同季列表，要拿得多发一次请求。
    //      **代价：播完不会自动接下一集**，想连播还是得从详情页进。
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
            subtitleIndex: -1
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
