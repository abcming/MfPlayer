pragma Singleton
import QtQuick

QtObject {
    // ── 导航 / 通用 ──
    readonly property string navHome: "主页"
    readonly property string navBrowseMedia: "媒体库"
    readonly property string navBack: "← 返回"
    readonly property string navAllMedia: "所有媒体"
    readonly property string navMovieCount: "电影"
    readonly property string navSeriesCount: "节目"
    readonly property string navEpisodeCount: "集数"
    readonly property string navFavorites: "收藏"
    readonly property string navSearch: "搜索"
    readonly property string favMovies: "收藏影片"
    readonly property string favSeries: "收藏节目"
    readonly property string favEpisodes: "收藏集"
    readonly property string favPeople: "收藏人物"
    readonly property string searchPlaceholder: "搜索影片、节目、人物..."
    readonly property string searchRecommend: "推荐"
    readonly property string searchSeries: "节目"
    readonly property string searchEpisodes: "集"
    readonly property string searchMovies: "影片"
    readonly property string searchPeople: "人物"

    // ── 登录对话框 ──
    readonly property string loginTitle: "连接 Emby 服务器"
    readonly property string loginServerAddress: "服务器地址"
    readonly property string loginUsername: "用户名"
    readonly property string loginPassword: "密码"
    readonly property string loginConnect: "连接"

    // ── 通用弹窗 ──
    readonly property string dlgNotice: "提示"
    readonly property string dlgConfirm: "确定"

    // ── 服务器面板 / 用户菜单 ──
    readonly property string svrTitle: "服务器"
    readonly property string svrNoServers: "暂无已保存的服务器"
    readonly property string svrAddServer: "添加服务器"
    readonly property string svrPlayLocalFile: "播放本地文件"
    readonly property string svrRefreshCache: "刷新缓存"
    readonly property string svrDisconnect: "退出登录"
    readonly property string svrSkipSslVerify: "跳过 SSL 证书验证"
    readonly property string hdrPeakBrightness: "HDR 峰值亮度"
    readonly property string svrDefaultUser: "用户"
    readonly property string svrNotLoggedIn: "未登录"
    readonly property string svrSettings: "设置"

    // ── 主页 ──
    readonly property string homeMyMedia: "我的媒体"
    readonly property string homeContinueWatching: "继续观看"
    readonly property string homeLatestPrefix: "最新"
    readonly property string homeNotLoggedInPrompt: "登录 Emby 服务器，开始播放"
    readonly property string homeNotLoggedInHint: "点击左下角头像区域添加服务器"
    readonly property string homeSearchPlaceholder: "搜索电影、电视剧..."
    readonly property string homeLetterIndexAll: "全"

    // ── 媒体库 Tab 栏 ──
    readonly property string libTabShows: "节目"
    readonly property string libTabMovies: "影片"
    readonly property string libTabAll: "全部"
    readonly property string libTabSuggestions: "推荐"

    readonly property string libTabFavorites: "收藏"
    readonly property string libTabGenres: "类型"
    readonly property string libTabStudios: "播出平台"
    readonly property string libTabEpisodes: "集"
    readonly property string libTabFolders: "文件夹"

    // ── 排序与筛选 ──
    readonly property string sortByName: "名称"
    readonly property string sortByYear: "年份"
    readonly property string sortByRating: "评分"
    readonly property string sortByDateAdded: "添加时间"
    readonly property string sortByPlayed: "播放时间"
    readonly property string sortAscending: "升序"
    readonly property string sortDescending: "降序"
    readonly property string filterTitle: "筛选"
    readonly property string filterAll: "全部"
    readonly property string filterPlayed: "已看"
    readonly property string filterUnplayed: "未看"
    readonly property string filterFavorites: "收藏"

    // ── 详情页 ──
    readonly property string detailPlay: "播放"
    readonly property string detailResume: "恢复播放"
    readonly property string detailRestart: "从头开始"
    readonly property string detailCastAndCrew: "演职人员"
    readonly property string detailStudios: "工作室"
    readonly property string detailSimilar: "相似推荐"
    readonly property string detailFilmography: "全部作品"
    readonly property string detailMediaInfo: "媒体信息"
    readonly property string detailSelectSeason: "选择季"
    readonly property string detailStatusContinuing: "● 连载中"
    readonly property string detailStatusEnded: "● 已完结"
    readonly property string detailYearOngoing: " - 现在"
    readonly property string detailBorn: "出生"
    readonly property string detailDied: "去世"
    readonly property string detailBirthPlace: "出生地"

    // ── 标签页 / 徽章 ──
    readonly property string tabVideo: "视频"
    readonly property string tabAudio: "音频"
    readonly property string tabSubtitle: "字幕"
    readonly property string tabDefault: "默认"
    readonly property string tabForced: "强制"

    // ── 轨道选择器 ──
    readonly property string trackLabelVideo: "版本: "
    readonly property string trackLabelAudio: "音频: "
    readonly property string trackLabelSubtitle: "字幕: "
    readonly property string trackDefault: "默认"
    readonly property string trackOff: "关"
    readonly property string trackVersionPrefix: "版本 "

    // ── 播放页 ──
    readonly property string playerPlaylist: "播放列表"
    readonly property string playerChapterPrefix: "章节 "

    // ── 设置 ──
    readonly property string settingsTitle: "设置"
    readonly property string settingsGeneral: "通用"
    readonly property string settingsKeys: "按键"
    readonly property string settingsAdvanced: "高级"
    readonly property string settingsPlayback: "播放"
    readonly property string settingsAudioLang: "首选音频语言"
    readonly property string settingsSubLang: "首选字幕语言"
    readonly property string settingsActionAfterEnd: "播放结束后"
    readonly property string settingsActionNext: "播下一集"
    readonly property string settingsActionLoop: "循环播放"
    readonly property string settingsActionStop: "停止"
    readonly property string settingsSeekForward: "快进步长 (秒)"
    readonly property string settingsSeekBackward: "快退步长 (秒)"
    readonly property string keySeekBackward: "快退"
    readonly property string keySeekForward: "快进"
    readonly property string keyPlayPause: "播放/暂停"
    readonly property string keyFullscreen: "全屏"
    readonly property string keyStats: "统计面板"
    readonly property string keySpeedDown: "减速"
    readonly property string keySpeedUp: "加速"
    readonly property string keyVolumeUp: "音量+"
    readonly property string keyVolumeDown: "音量-"
    readonly property string settingsHdrBrightness: "HDR 峰值亮度"
    readonly property string settingsAbout: "关于"

    // ── 文件对话框 ──
    readonly property string fileDialogTitle: "选择视频文件"
    readonly property string fileFilterVideo: "视频文件 (*.mp4 *.mkv *.avi *.mov *.webm *.flv *.wmv)"
    readonly property string fileFilterAll: "所有文件 (*)"

    // ── Emby 媒体类型 ──
    readonly property string typeMovie: "Movie"
    readonly property string typeSeries: "Series"
    readonly property string typeEpisode: "Episode"
    readonly property string typePerson: "Person"

    // ── 错误信息 ──
    readonly property string playFailed: "播放失败"

    // ── 媒体信息键名（尾随空格是有意的，与值拼接） ──
    readonly property string miTitle: "标题 "
    readonly property string miEmbeddedTitle: "内嵌标题 "
    readonly property string miCodec: "编解码器 "
    readonly property string miDolbyConfig: "杜比配置 "
    readonly property string miProfile: "配置 "
    readonly property string miLevel: "等级 "
    readonly property string miResolution: "分辨率 "
    readonly property string miAspectRatio: "宽高比 "
    readonly property string miInterlaced: "隔行 "
    readonly property string miFrameRate: "帧率 "
    readonly property string miBitrate: "比特率 "
    readonly property string miVideoRange: "视频范围 "
    readonly property string miColorPrimaries: "基色 "
    readonly property string miColorSpace: "色域 "
    readonly property string miColorTransfer: "色彩转换 "
    readonly property string miBitDepth: "位深度 "
    readonly property string miPixelFormat: "像素格式 "
    readonly property string miRefFrames: "参考帧 "
    readonly property string miLanguage: "语言 "
    readonly property string miLayout: "布局 "
    readonly property string miChannels: "频道 "
    readonly property string miSampleRate: "采样率 "
    readonly property string miForced: "强制 "
    readonly property string miExternal: "外挂 "
    readonly property string miDefault: "默认 "  // trailing space intentional
    readonly property string miYes: "是"
    readonly property string miNo: "否"

    // ── 格式化函数 ──
    function episodeFullLabel(indexNumber, episodeName) {
        return "第 " + (indexNumber || "?") + " 集  " + (episodeName || "")
    }

    function episodeShortLabel(indexNumber) {
        return "第 " + (indexNumber || "?") + " 集"
    }

    function seasonLabel(indexNumber) {
        return "第 " + (indexNumber || "?") + " 季"
    }

    function videoDisplayLabel(displayTitle) {
        return "视频：" + (displayTitle || "?")
    }

    function remainingTimeLong(totalTicks, positionTicks) {
        if (!totalTicks || !positionTicks || positionTicks <= 0) return ""
        var remain = Math.round((totalTicks - positionTicks) / 10000000)
        if (remain <= 0) return ""
        var h = Math.floor(remain / 3600)
        var m = Math.floor((remain % 3600) / 60)
        if (h > 0) return "剩余 " + h + " 小时 " + m + " 分钟"
        return "剩余 " + m + " 分钟"
    }

    function remainingTimeCompact(totalTicks, positionTicks) {
        if (!totalTicks || !positionTicks || positionTicks <= 0) return ""
        var remain = Math.round((totalTicks - positionTicks) / 10000000)
        if (remain <= 0) return ""
        var h = Math.floor(remain / 3600)
        var m = Math.floor((remain % 3600) / 60)
        return "剩余 " + (h > 0 ? h + " 时 " : "") + m + " 分"
    }

    function runtimeLabel(minutes) {
        if (!minutes || minutes <= 0) return ""
        var h = Math.floor(minutes / 60)
        var m = minutes % 60
        return h > 0 ? (h + " 小时 " + m + " 分钟") : (m + " 分钟")
    }

    function formatDate(isoString) {
        if (!isoString) return ""
        var d = isoString.substring(0, 10).split("-")
        if (d.length !== 3) return ""
        return d[0] + "年" + parseInt(d[1]) + "月" + parseInt(d[2]) + "日"
    }
}
