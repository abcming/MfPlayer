#pragma once
#include <cstdint>

namespace Constants {
    // Emby playback
    constexpr int64_t kTicksPerSecond = 10'000'000;
    constexpr int kMaxBitrate = 40'000'000;
    constexpr auto kPlayMethodDirectStream = "DirectStream";
    constexpr auto kRepeatModeNone = "RepeatNone";
    constexpr auto kPlaylistItemId = "playlistItem0";

    // Cache
    constexpr int kCacheExpirySeconds = 259'200;   // 3 days
    constexpr int kImageRetryDelayMs = 500;
    constexpr int kImageRetryCooldownMs = 30'000;  // 30s cooldown after download failure
    constexpr int kFormatRetryDelayMs = 100;

    // Item types
    constexpr auto kTypeMovie   = "Movie";
    constexpr auto kTypeSeries  = "Series";
    constexpr auto kTypeEpisode = "Episode";
    constexpr auto kTypePerson  = "Person";
    constexpr auto kStreamTypeSubtitle = "Subtitle";
    constexpr auto kStreamTypeAudio    = "Audio";
}
