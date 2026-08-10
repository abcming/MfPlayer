#pragma once

namespace EmbyFields {
    // ParentBackdropImageTags 同 CardWithUser: 收藏的集 / 媒体库「集」Tab 也走这条
    constexpr auto ListItems    = "Overview,ProductionYear,EndDate,Status,ChildCount,IndexNumber,ImageTags,BackdropImageTags,ParentBackdropImageTags,SortName,UserData";
    constexpr auto Detail       = "Overview,ProductionYear,EndDate,Status,Genres,People,CommunityRating,"
                                  "RunTimeTicks,MediaSources,ImageTags,BackdropImageTags,"
                                  "ParentBackdropImageTags,ParentLogoImageTag,ParentLogoItemId,"
                                  "Studios,OfficialRating,Taglines,MediaStreams,UserData";
    constexpr auto Seasons      = "ImageTags,BackdropImageTags";
    constexpr auto Episodes     = "Overview,IndexNumber,ImageTags,BackdropImageTags";
    // ParentBackdropImageTags: 集自己没有缩略图时回落到剧集的 backdrop 用。
    // 详情页那条路不需要 (它手上有整个 Series 的 itemData), 卡片够不着
    constexpr auto CardWithUser = "Overview,ProductionYear,EndDate,Status,PrimaryImageAspectRatio,SeriesName,ImageTags,BackdropImageTags,ParentBackdropImageTags,UserData";
    constexpr auto Card         = "Overview,ProductionYear,EndDate,Status,PrimaryImageAspectRatio,ImageTags,BackdropImageTags";
    constexpr auto NextUp       = "UserData,Overview,IndexNumber,ParentIndexNumber";
}
