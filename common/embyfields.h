#pragma once

namespace EmbyFields {
    constexpr auto ListItems    = "Overview,ProductionYear,EndDate,Status,ChildCount,IndexNumber,ImageTags,BackdropImageTags,SortName,UserData";
    constexpr auto Detail       = "Overview,ProductionYear,EndDate,Status,Genres,People,CommunityRating,"
                                  "RunTimeTicks,MediaSources,ImageTags,BackdropImageTags,"
                                  "ParentBackdropImageTags,ParentLogoImageTag,ParentLogoItemId,"
                                  "Studios,OfficialRating,Taglines,MediaStreams,UserData";
    constexpr auto Seasons      = "ImageTags,BackdropImageTags";
    constexpr auto Episodes     = "Overview,IndexNumber,ImageTags,BackdropImageTags";
    constexpr auto CardWithUser = "Overview,ProductionYear,EndDate,Status,PrimaryImageAspectRatio,SeriesName,ImageTags,BackdropImageTags,UserData";
    constexpr auto Card         = "Overview,ProductionYear,EndDate,Status,PrimaryImageAspectRatio,ImageTags,BackdropImageTags";
    constexpr auto NextUp       = "UserData,Overview,IndexNumber,ParentIndexNumber";
}
