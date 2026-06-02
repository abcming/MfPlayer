#include "common/version.h"
#include "common/embyfields.h"
#include "common/constants.h"
#include "core/providers/emby/embyclient.h"
#include "core/network/curlengine.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QUuid>
#include <QSet>
#include <QPointer>

EmbyClient::EmbyClient(QObject *parent)
    : QObject(parent)
    , m_curl(std::make_unique<CurlEngine>(this))
{
}

EmbyClient::~EmbyClient() = default;

bool EmbyClient::authenticated() const { return m_authenticated; }
QString EmbyClient::serverUrl() const { return m_serverUrl; }
QString EmbyClient::accessToken() const { return m_accessToken; }
QString EmbyClient::userId() const { return m_userId; }

void EmbyClient::setServer(const QString &url) {
    m_serverUrl = url;
    if (m_serverUrl.endsWith('/'))
        m_serverUrl.chop(1);
    emit serverUrlChanged();
}

void EmbyClient::setAuth(const QString &token, const QString &userId) {
    m_accessToken = token;
    m_userId = userId;
    m_authenticated = true;
    invalidateHeaderCache();
    emit authenticatedChanged();
    emit accessTokenChanged();
    emit userIdChanged();
}

void EmbyClient::login(const QString &username, const QString &password) {
    QJsonObject body;
    body["Username"] = username;
    body["Pw"] = password;

    CurlEngine::Headers headers;
    headers.push_back({"Content-Type", "application/json"});
    headers.push_back({"User-Agent", MFPLAYER_USER_AGENT});
    QString auth = QStringLiteral("MediaBrowser Client=\"%1\", Device=\"Desktop\", DeviceId=\"%1\", Version=\"1.0\"").arg(MFPLAYER_APP_NAME);
    headers.push_back({"X-Emby-Authorization", auth.toUtf8()});

    QString url = m_serverUrl + "/emby/Users/AuthenticateByName";
    QPointer<EmbyClient> guard(this);
    m_curl->post(url, headers, QJsonDocument(body).toJson(), [this, guard](const CurlResponse &r) {
        if (!guard) return;
        if (r.curlResult != CURLE_OK) {
            emit loginFailed(QString::fromUtf8(curl_easy_strerror(r.curlResult)));
            return;
        }
        QJsonObject obj = QJsonDocument::fromJson(r.body).object();
        QString token = obj["AccessToken"].toString();
        QString userId = obj["User"].toObject()["Id"].toString();
        if (token.isEmpty()) {
            emit loginFailed(QString::fromUtf8("登录失败：未获取到 Token"));
            return;
        }
        m_accessToken = token;
        m_userId = userId;
        m_sessionId = obj["SessionInfo"].toObject()["Id"].toString();
        m_authenticated = true;
        invalidateHeaderCache();
        emit authenticatedChanged();
        emit accessTokenChanged();
        emit userIdChanged();
        emit loginSuccess(token, userId);
    });
}

void EmbyClient::setSkipSslVerify(bool skip) {
    m_curl->setSkipSslVerify(skip);
}

void EmbyClient::logout() {
    m_accessToken.clear();
    m_userId.clear();
    m_authenticated = false;
    invalidateHeaderCache();
    emit authenticatedChanged();
    emit accessTokenChanged();
    emit userIdChanged();
}

void EmbyClient::invalidateHeaderCache() {
    m_cachedAuthHeader.clear();
    m_cachedHeaders.clear();
}

QByteArray EmbyClient::authHeader() const {
    if (!m_cachedAuthHeader.isEmpty())
        return m_cachedAuthHeader;
    QStringList parts;
    if (!m_accessToken.isEmpty())
        parts << QStringLiteral("Token=\"%1\"").arg(m_accessToken);
    if (!m_userId.isEmpty())
        parts << QStringLiteral("UserId=\"%1\"").arg(m_userId);
    parts << QStringLiteral("Client=\"%1\"").arg(MFPLAYER_APP_NAME);
    parts << QStringLiteral("Device=\"Desktop\"");
    parts << QStringLiteral("DeviceId=\"%1\"").arg(MFPLAYER_APP_NAME);
    parts << QStringLiteral("Version=\"1.0\"");
    m_cachedAuthHeader = QStringLiteral("MediaBrowser %1").arg(parts.join(", ")).toUtf8();
    return m_cachedAuthHeader;
}

CurlEngine::Headers EmbyClient::defaultHeaders() const {
    if (!m_cachedHeaders.isEmpty())
        return m_cachedHeaders;
    CurlEngine::Headers h;
    h.push_back({"Content-Type", "application/json"});
    h.push_back({"User-Agent", MFPLAYER_USER_AGENT});
    h.push_back({"X-Emby-Authorization", authHeader()});
    if (!m_accessToken.isEmpty())
        h.push_back({"X-Emby-Token", m_accessToken.toUtf8()});
    m_cachedHeaders = h;
    return m_cachedHeaders;
}

void EmbyClient::getJson(const QString &path, const QUrlQuery &query,
                          std::function<void(const QJsonDocument &)> callback) {
    QString safePath = path;
    if (!safePath.startsWith('/'))
        safePath.prepend('/');
    QUrl url(m_serverUrl + safePath);
    if (!query.isEmpty())
        url.setQuery(query);

    QPointer<EmbyClient> guard(this);
    m_curl->get(url.toString(QUrl::FullyEncoded), defaultHeaders(), [guard, cb = std::move(callback)](const CurlResponse &r) {
        if (!guard) return;
        if (!r.ok()) {
            if (r.httpStatus == 401)
                emit guard->tokenExpired();
            else
                emit guard->networkError(r.errorString());
        }
        cb(QJsonDocument::fromJson(r.ok() ? r.body : QByteArray()));
    });
}

void EmbyClient::postJson(const QString &path, const QJsonObject &body,
                           std::function<void(const QJsonDocument &)> callback) {
    QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    QString safePath = path;
    if (!safePath.startsWith('/'))
        safePath.prepend('/');
    QUrl url(m_serverUrl + safePath);

    QPointer<EmbyClient> guard(this);
    m_curl->post(url.toString(QUrl::FullyEncoded), defaultHeaders(), data, [guard, cb = std::move(callback)](const CurlResponse &r) {
        if (!guard) return;
        if (!r.ok()) {
            if (r.httpStatus == 401)
                emit guard->tokenExpired();
            else
                emit guard->networkError(r.errorString());
        }
        cb(QJsonDocument::fromJson(r.ok() ? r.body : QByteArray()));
    });
}

void EmbyClient::deleteJson(const QString &path,
                             std::function<void()> callback) {
    QString safePath = path;
    if (!safePath.startsWith('/'))
        safePath.prepend('/');
    QUrl url(m_serverUrl + safePath);

    QPointer<EmbyClient> guard(this);
    m_curl->del(url.toString(QUrl::FullyEncoded), defaultHeaders(), [guard, cb = std::move(callback)](const CurlResponse &r) {
        if (!guard) return;
        if (!r.ok()) {
            if (r.httpStatus == 401)
                emit guard->tokenExpired();
            else
                emit guard->networkError(r.errorString());
        }
        cb();
    });
}

void EmbyClient::fetchLibraries() {
    getJson("/emby/Users/" + m_userId + "/Views", {}, [this](const QJsonDocument &doc) {
        emit librariesFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchItems(const QString &parentId, const QString &includeTypes, bool recursive) {
    QUrlQuery query;
    query.addQueryItem("ParentId", parentId);
    query.addQueryItem("SortBy", "SortName");
    query.addQueryItem("SortOrder", "Ascending");
    if (recursive)
        query.addQueryItem("Recursive", "true");
    query.addQueryItem("Fields", EmbyFields::ListItems);
    if (!includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", includeTypes);

    QString cacheKey = parentId + ":" + includeTypes + ":" + (recursive ? "1" : "0");
    getJson("/emby/Users/" + m_userId + "/Items", query, [this, parentId, cacheKey](const QJsonDocument &doc) {
        emit itemsFetched(doc.object()["Items"].toArray(), parentId, cacheKey);
    });
}

void EmbyClient::fetchItemDetail(const QString &itemId) {
    QUrlQuery query;
    query.addQueryItem("Fields", EmbyFields::Detail);

    getJson("/emby/Users/" + m_userId + "/Items/" + itemId, query, [this](const QJsonDocument &doc) {
        emit itemDetailFetched(doc.object());
    });
}

void EmbyClient::fetchSeasons(const QString &seriesId) {
    QUrlQuery query;
    query.addQueryItem("userId", m_userId);
    query.addQueryItem("Fields", EmbyFields::Seasons);
    query.addQueryItem("SortBy", "IndexNumber");
    query.addQueryItem("SortOrder", "Ascending");

    getJson("/emby/Shows/" + seriesId + "/Seasons", query, [this](const QJsonDocument &doc) {
        emit seasonsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchEpisodes(const QString &seriesId, const QString &seasonId) {
    QUrlQuery query;
    query.addQueryItem("userId", m_userId);
    query.addQueryItem("SeasonId", seasonId);
    query.addQueryItem("Fields", EmbyFields::Episodes);
    query.addQueryItem("SortBy", "IndexNumber");
    query.addQueryItem("SortOrder", "Ascending");

    getJson("/emby/Shows/" + seriesId + "/Episodes", query, [this](const QJsonDocument &doc) {
        emit episodesFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchResume(int limit) {
    QUrlQuery query;
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("IncludeItemTypes", "Movie,Episode");
    query.addQueryItem("Fields", EmbyFields::CardWithUser);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("EnableTotalRecordCount", "false");

    getJson("/emby/Users/" + m_userId + "/Items/Resume", query, [this](const QJsonDocument &doc) {
        emit resumeFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchLatest(int limit, const QString &parentId,
                              const QString &includeTypes, const QString &tag) {
    QUrlQuery query;
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("EnableTotalRecordCount", "false");
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    if (!includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", includeTypes);

    getJson("/emby/Users/" + m_userId + "/Items/Latest", query, [this, tag](const QJsonDocument &doc) {
        QJsonArray items = doc.isArray() ? doc.array() : doc.object()["Items"].toArray();
        emit latestFetched(items, tag);
    });
}

void EmbyClient::fetchSimilar(const QString &includeTypes, const QString &excludeId, int limit) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("IncludeItemTypes", includeTypes);
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");

    getJson("/emby/Items/" + excludeId + "/Similar", query, [this, excludeId](const QJsonDocument &doc) {
        QJsonArray items = doc.isArray() ? doc.array() : doc.object()["Items"].toArray();
        emit similarFetched(items, excludeId);
    });
}

void EmbyClient::fetchPersonFilms(const QString &personId, const QString &includeTypes) {
    QUrlQuery query;
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("PersonIds", personId);
    query.addQueryItem("IncludeItemTypes", includeTypes);
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", "SortName");
    query.addQueryItem("SortOrder", "Ascending");

    bool isMovie = includeTypes.contains("Movie");
    getJson("/emby/Users/" + m_userId + "/Items", query, [this, personId, isMovie](const QJsonDocument &doc) {
        if (isMovie)
            emit personMoviesFetched(doc.object()["Items"].toArray(), personId);
        else
            emit personSeriesFetched(doc.object()["Items"].toArray(), personId);
    });
}

// ── Playback reporting ──

QJsonObject EmbyClient::buildPlaybackBody(const QString &itemId, qint64 positionTicks, bool isPaused) {
    QJsonObject body;
    body["ItemId"] = itemId;
    body["PositionTicks"] = positionTicks;
    body["IsPaused"] = isPaused;
    body["IsMuted"] = false;
    body["CanSeek"] = true;
    body["PlayMethod"] = Constants::kPlayMethodDirectStream;
    body["PlaybackRate"] = 1;
    body["RepeatMode"] = Constants::kRepeatModeNone;
    body["PlaylistIndex"] = 0;
    body["PlaylistLength"] = 1;
    return body;
}

void EmbyClient::reportPlaybackStart(const QString &itemId, qint64 positionTicks,
                                      const QString &playSessionId,
                                      const QString &mediaSourceId,
                                      std::function<void()> onReady) {
    auto ready = onReady ? onReady : []() {};
    QString psId = playSessionId.isEmpty()
        ? QUuid::createUuid().toString(QUuid::WithoutBraces)
        : playSessionId;

    // Fire-and-forget: register playback capabilities
    QJsonObject caps;
    QJsonArray playableTypes;
    playableTypes.append("Video");
    caps["PlayableMediaTypes"] = playableTypes;
    QJsonArray commands;
    commands.append("Play"); commands.append("Pause");
    commands.append("Stop"); commands.append("Seek");
    caps["SupportedCommands"] = commands;
    postJson("/emby/Sessions/Capabilities/Full", caps, [](const QJsonDocument &) {});

    // Report playback start immediately (no need to wait for capabilities)
    QJsonObject body = buildPlaybackBody(itemId, positionTicks, false);
    body["SessionId"] = m_sessionId;
    body["PlaySessionId"] = psId;
    if (!mediaSourceId.isEmpty())
        body["MediaSourceId"] = mediaSourceId;
    QJsonArray queue;
    QJsonObject qi;
    qi["Id"] = itemId;
    qi["PlaylistItemId"] = Constants::kPlaylistItemId;
    queue.append(qi);
    body["NowPlayingQueue"] = queue;
    postJson("/emby/Sessions/Playing", body, [ready](const QJsonDocument &) {
        ready();
    });
}

static QJsonObject buildDeviceProfile() {
    QJsonObject deviceProfile;
    deviceProfile["MaxStreamingBitrate"] = Constants::kMaxBitrate;
    deviceProfile["MusicStreamingTranscodingBitrate"] = Constants::kMaxBitrate;
    deviceProfile["MaxStaticBitrate"] = Constants::kMaxBitrate;

    QJsonArray directPlayProfiles;
    QJsonObject dpp;
    dpp["Type"] = "Video";
    dpp["Container"] = "mov,mp4,mkv,webm";
    dpp["AudioCodec"] = "aac,mp3,wav,ac3,eac3,flac,truehd,dts,dca,opus";
    dpp["VideoCodec"] = "h264,hevc,dvhe,dvh1,hev1,mpeg4,vp9,av1";
    directPlayProfiles.append(dpp);
    deviceProfile["DirectPlayProfiles"] = directPlayProfiles;

    QJsonArray transcodingProfiles;
    QJsonObject tp;
    tp["Type"] = "Video";
    tp["Protocol"] = "hls";
    tp["Container"] = "ts";
    tp["VideoCodec"] = "hevc,h264,mpeg4,vp9";
    tp["AudioCodec"] = "aac,mp3,wav,ac3,eac3,flac,opus";
    tp["MaxAudioChannels"] = "6";
    tp["MinSegments"] = 2;
    tp["BreakOnNonKeyFrames"] = true;
    tp["Context"] = "Streaming";
    transcodingProfiles.append(tp);
    deviceProfile["TranscodingProfiles"] = transcodingProfiles;

    QJsonArray codecProfiles;
    QJsonObject cp1;
    cp1["Type"] = "Video";
    cp1["Codec"] = "h264";
    QJsonArray cp1Cond;
    QJsonObject c1; c1["Condition"] = "NotEquals"; c1["Property"] = "IsAnamorphic"; c1["Value"] = "true"; c1["IsRequired"] = false; cp1Cond.append(c1);
    QJsonObject c2; c2["Condition"] = "EqualsAny"; c2["Property"] = "VideoProfile"; c2["Value"] = "high|main|baseline|constrained baseline"; c2["IsRequired"] = false; cp1Cond.append(c2);
    QJsonObject c3; c3["Condition"] = "LessThanEqual"; c3["Property"] = "VideoLevel"; c3["Value"] = "80"; c3["IsRequired"] = false; cp1Cond.append(c3);
    QJsonObject c4; c4["Condition"] = "NotEquals"; c4["Property"] = "IsInterlaced"; c4["Value"] = "true"; c4["IsRequired"] = false; cp1Cond.append(c4);
    cp1["ApplyConditions"] = cp1Cond;
    codecProfiles.append(cp1);
    QJsonObject cp2;
    cp2["Type"] = "Video";
    cp2["Codec"] = "hevc";
    QJsonArray cp2Cond;
    QJsonObject c5; c5["Condition"] = "NotEquals"; c5["Property"] = "IsAnamorphic"; c5["Value"] = "true"; c5["IsRequired"] = false; cp2Cond.append(c5);
    QJsonObject c6; c6["Condition"] = "EqualsAny"; c6["Property"] = "VideoProfile"; c6["Value"] = "high|main|main 10"; c6["IsRequired"] = false; cp2Cond.append(c6);
    QJsonObject c7; c7["Condition"] = "LessThanEqual"; c7["Property"] = "VideoLevel"; c7["Value"] = "175"; c7["IsRequired"] = false; cp2Cond.append(c7);
    QJsonObject c8; c8["Condition"] = "NotEquals"; c8["Property"] = "IsInterlaced"; c8["Value"] = "true"; c8["IsRequired"] = false; cp2Cond.append(c8);
    cp2["ApplyConditions"] = cp2Cond;
    codecProfiles.append(cp2);
    deviceProfile["CodecProfiles"] = codecProfiles;

    QJsonArray responseProfiles;
    QJsonObject rp;
    rp["Type"] = "Video";
    rp["Container"] = "m4v";
    rp["MimeType"] = "video/mp4";
    responseProfiles.append(rp);
    deviceProfile["ResponseProfiles"] = responseProfiles;

    QJsonArray subtitleProfiles;
    QJsonObject sp1; sp1["Format"] = "ass"; sp1["Method"] = "Embed"; subtitleProfiles.append(sp1);
    QJsonObject sp2; sp2["Format"] = "ssa"; sp2["Method"] = "Embed"; subtitleProfiles.append(sp2);
    QJsonObject sp3; sp3["Format"] = "subrip"; sp3["Method"] = "Embed"; subtitleProfiles.append(sp3);
    QJsonObject sp4; sp4["Format"] = "sub"; sp4["Method"] = "Embed"; subtitleProfiles.append(sp4);
    QJsonObject sp5; sp5["Format"] = "pgssub"; sp5["Method"] = "Embed"; subtitleProfiles.append(sp5);
    QJsonObject sp6; sp6["Format"] = "subrip"; sp6["Method"] = "External"; subtitleProfiles.append(sp6);
    QJsonObject sp7; sp7["Format"] = "sub"; sp7["Method"] = "External"; subtitleProfiles.append(sp7);
    QJsonObject sp8; sp8["Format"] = "ass"; sp8["Method"] = "External"; subtitleProfiles.append(sp8);
    QJsonObject sp9; sp9["Format"] = "ssa"; sp9["Method"] = "External"; subtitleProfiles.append(sp9);
    QJsonObject sp10; sp10["Format"] = "vtt"; sp10["Method"] = "External"; subtitleProfiles.append(sp10);
    QJsonObject sp11; sp11["Format"] = "vtt"; sp11["Method"] = "Embed"; subtitleProfiles.append(sp11);
    deviceProfile["SubtitleProfiles"] = subtitleProfiles;

    return deviceProfile;
}

void EmbyClient::fetchPlaybackInfo(const QString &itemId,
                                    std::function<void(const QString &, const QString &)> callback,
                                    const QString &mediaSourceId,
                                    int subtitleStreamIndex) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("AutoOpenLiveStream", "false");
    query.addQueryItem("IsPlayback", "true");
    if (!mediaSourceId.isEmpty())
        query.addQueryItem("MediaSourceId", mediaSourceId);
    if (subtitleStreamIndex >= 0)
        query.addQueryItem("SubtitleStreamIndex", QString::number(subtitleStreamIndex));

    static const QJsonObject s_deviceProfile = buildDeviceProfile();

    QJsonObject body;
    body["DeviceProfile"] = s_deviceProfile;

    QString path = "/emby/Items/" + itemId + "/PlaybackInfo?"
                   + query.toString(QUrl::FullyEncoded);

    postJson(path, body, [callback](const QJsonDocument &doc) {
        QJsonObject obj = doc.object();
        QJsonArray sources = obj["MediaSources"].toArray();
        QString streamUrl;
        QString playSessionId = obj["PlaySessionId"].toString();
        if (!sources.isEmpty())
            streamUrl = sources.first().toObject()["DirectStreamUrl"].toString();
        if (streamUrl.isEmpty())
            qWarning() << "EmbyClient: PlaybackInfo returned no DirectStreamUrl";
        if (callback) callback(streamUrl, playSessionId);
    });
}

void EmbyClient::reportPlaybackProgress(const QString &itemId, qint64 positionTicks,
                                         const QString &playSessionId,
                                         const QString &mediaSourceId,
                                         bool isPaused) {
    QJsonObject body = buildPlaybackBody(itemId, positionTicks, isPaused);
    body["EventName"] = "timeupdate";
    if (!playSessionId.isEmpty())
        body["PlaySessionId"] = playSessionId;
    if (!mediaSourceId.isEmpty())
        body["MediaSourceId"] = mediaSourceId;

    postJson("/emby/Sessions/Playing/Progress", body, [](const QJsonDocument &) {});
}

void EmbyClient::reportPlaybackStop(const QString &itemId, qint64 positionTicks,
                                     const QString &playSessionId,
                                     const QString &mediaSourceId,
                                     std::function<void()> onReady) {
    QJsonObject body = buildPlaybackBody(itemId, positionTicks, false);
    if (!playSessionId.isEmpty())
        body["PlaySessionId"] = playSessionId;
    if (!mediaSourceId.isEmpty())
        body["MediaSourceId"] = mediaSourceId;
    QJsonArray queue;
    QJsonObject qi;
    qi["Id"] = itemId;
    qi["PlaylistItemId"] = Constants::kPlaylistItemId;
    queue.append(qi);
    body["NowPlayingQueue"] = queue;

    auto ready = onReady ? onReady : []() {};
    postJson("/emby/Sessions/Playing/Stopped", body, [ready](const QJsonDocument &) {
        ready();
    });
}

// ── Played status ──

void EmbyClient::markPlayed(const QString &itemId) {
    postJson("/emby/Users/" + m_userId + "/PlayedItems/" + itemId, {},
             [this, itemId](const QJsonDocument &) {
                 emit playedStatusChanged(itemId, true);
             });
}

void EmbyClient::markUnplayed(const QString &itemId) {
    deleteJson("/emby/Users/" + m_userId + "/PlayedItems/" + itemId,
               [this, itemId]() {
                   emit playedStatusChanged(itemId, false);
               });
}

// ── Favorites ──

void EmbyClient::addFavorite(const QString &itemId) {
    postJson("/emby/Users/" + m_userId + "/FavoriteItems/" + itemId, {},
             [this, itemId](const QJsonDocument &) {
                 emit favoriteChanged(itemId, true);
             });
}

void EmbyClient::removeFavorite(const QString &itemId) {
    deleteJson("/emby/Users/" + m_userId + "/FavoriteItems/" + itemId,
               [this, itemId]() {
                   emit favoriteChanged(itemId, false);
               });
}

// ── Search ──

void EmbyClient::searchHints(const QString &term, const QString &includeTypes) {
    QUrlQuery query;
    query.addQueryItem("SearchTerm", term);
    query.addQueryItem("IncludeItemTypes", includeTypes);
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("Limit", "30");

    getJson("/emby/Items", query, [this](const QJsonDocument &doc) {
        QJsonArray all = doc.isArray() ? doc.array() : doc.object()["Items"].toArray();
        QJsonArray unique;
        QSet<QString> seenId, seenKey;
        for (const QJsonValue &v : all) {
            QJsonObject item = v.toObject();
            QString id = item["Id"].toString();
            if (id.isEmpty() || seenId.contains(id)) continue;
            seenId.insert(id);
            // Dedup: same name+year for Movie (catches multi-version hits)
            QString type = item["Type"].toString();
            if (type == Constants::kTypeMovie) {
                QString key = item["Name"].toString() + "|" + QString::number(item["ProductionYear"].toInt());
                if (key.isEmpty() || seenKey.contains(key)) continue;
                seenKey.insert(key);
            }
            unique.append(item);
        }
        emit searchHintsFetched(unique);
    });
}

// ── Genres ──

void EmbyClient::fetchGenres(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Fields", "ImageTags");
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    getJson("/emby/Genres", query, [this](const QJsonDocument &doc) {
        emit genresFetched(doc.object()["Items"].toArray());
    });
}

// ── Studios ──

void EmbyClient::fetchStudios(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Fields", "ImageTags");
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    getJson("/emby/Studios", query, [this](const QJsonDocument &doc) {
        emit studiosFetched(doc.object()["Items"].toArray());
    });
}

// ── Filtered Items ──

void EmbyClient::fetchItemsFiltered(const FetchParams &p) {
    QUrlQuery query;
    if (!p.parentId.isEmpty())
        query.addQueryItem("ParentId", p.parentId);
    query.addQueryItem("Recursive", p.recursive ? "true" : "false");
    query.addQueryItem("Fields", EmbyFields::ListItems);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", p.sortBy.isEmpty() ? "SortName" : p.sortBy);
    query.addQueryItem("SortOrder", p.sortOrder.isEmpty() ? "Ascending" : p.sortOrder);
    if (!p.includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", p.includeTypes);
    if (!p.filters.isEmpty())
        query.addQueryItem("Filters", p.filters);
    if (!p.genreIds.isEmpty())
        query.addQueryItem("GenreIds", p.genreIds);
    if (!p.studioIds.isEmpty())
        query.addQueryItem("StudioIds", p.studioIds);
    if (!p.personIds.isEmpty())
        query.addQueryItem("PersonIds", p.personIds);
    if (!p.tags.isEmpty())
        query.addQueryItem("Tags", p.tags);
    if (!p.years.isEmpty())
        query.addQueryItem("Years", p.years);
    if (!p.officialRatings.isEmpty())
        query.addQueryItem("OfficialRatings", p.officialRatings);
    if (!p.searchTerm.isEmpty())
        query.addQueryItem("SearchTerm", p.searchTerm);
    if (p.limit > 0)
        query.addQueryItem("Limit", QString::number(p.limit));
    if (p.startIndex > 0)
        query.addQueryItem("StartIndex", QString::number(p.startIndex));

    getJson("/emby/Users/" + m_userId + "/Items", query, [this, p](const QJsonDocument &doc) {
        QJsonObject obj = doc.object();
        QString cacheKey = p.parentId + ":F:" + p.includeTypes + ":" + p.filters + ":" + p.sortBy + ":" + p.genreIds + ":" + p.studioIds + ":" + p.personIds;
        int total = obj.contains("TotalRecordCount") ? obj["TotalRecordCount"].toInt() : -1;
        emit itemsFetched(obj["Items"].toArray(), p.parentId, cacheKey, total);
    });
}

void EmbyClient::fetchItemsFiltered(const FetchParams &p, JsonArrayCallback callback) {
    QUrlQuery query;
    query.addQueryItem("Recursive", p.recursive ? "true" : "false");
    query.addQueryItem("Fields", EmbyFields::ListItems);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", p.sortBy.isEmpty() ? "SortName" : p.sortBy);
    query.addQueryItem("SortOrder", p.sortOrder.isEmpty() ? "Ascending" : p.sortOrder);
    if (!p.parentId.isEmpty())
        query.addQueryItem("ParentId", p.parentId);
    if (!p.includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", p.includeTypes);
    if (!p.filters.isEmpty())
        query.addQueryItem("Filters", p.filters);
    if (!p.genreIds.isEmpty())
        query.addQueryItem("GenreIds", p.genreIds);
    if (!p.studioIds.isEmpty())
        query.addQueryItem("StudioIds", p.studioIds);
    if (!p.personIds.isEmpty())
        query.addQueryItem("PersonIds", p.personIds);
    if (!p.tags.isEmpty())
        query.addQueryItem("Tags", p.tags);
    if (!p.years.isEmpty())
        query.addQueryItem("Years", p.years);
    if (!p.officialRatings.isEmpty())
        query.addQueryItem("OfficialRatings", p.officialRatings);
    if (!p.searchTerm.isEmpty())
        query.addQueryItem("SearchTerm", p.searchTerm);
    if (p.limit > 0)
        query.addQueryItem("Limit", QString::number(p.limit));
    if (p.startIndex > 0)
        query.addQueryItem("StartIndex", QString::number(p.startIndex));

    getJson("/emby/Users/" + m_userId + "/Items", query, [callback](const QJsonDocument &doc) {
        callback(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchSuggestions(const QString &parentId, const QString &includeTypes) {
    QUrlQuery query;
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    query.addQueryItem("Limit", "50");
    query.addQueryItem("Fields", EmbyFields::ListItems);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("EnableTotalRecordCount", "false");
    if (!includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", includeTypes);

    getJson("/emby/Users/" + m_userId + "/Suggestions", query, [this, parentId](const QJsonDocument &doc) {
        emit itemsFetched(doc.object()["Items"].toArray(), parentId, parentId + ":suggestions");
    });
}

void EmbyClient::fetchSuggestionsResume(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("Limit", "12");
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("ParentId", parentId);
    query.addQueryItem("IncludeItemTypes", "Movie,Episode");
    query.addQueryItem("Fields", EmbyFields::CardWithUser);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("EnableTotalRecordCount", "false");

    getJson("/emby/Users/" + m_userId + "/Items/Resume", query, [this](const QJsonDocument &doc) {
        emit suggestionsResumeFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchSuggestionsLatest(const QString &parentId, const QString &includeTypes) {
    QUrlQuery query;
    query.addQueryItem("Limit", "20");
    query.addQueryItem("ParentId", parentId);
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("EnableTotalRecordCount", "false");
    if (!includeTypes.isEmpty())
        query.addQueryItem("IncludeItemTypes", includeTypes);

    getJson("/emby/Users/" + m_userId + "/Items/Latest", query, [this](const QJsonDocument &doc) {
        emit suggestionsLatestFetched(doc.isArray() ? doc.array() : doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchItemCounts() {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    getJson("/emby/Items/Counts", query, [this](const QJsonDocument &doc) {
        QJsonObject obj = doc.isObject() ? doc.object() : QJsonObject();
        emit itemCountsFetched(
            obj["MovieCount"].toInt(),
            obj["SeriesCount"].toInt(),
            obj["EpisodeCount"].toInt()
        );
    });
}

void EmbyClient::fetchNextUp(const QString &seriesId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("SeriesId", seriesId);
    query.addQueryItem("Limit", "1");
    query.addQueryItem("Fields", EmbyFields::NextUp);

    getJson("/emby/Shows/NextUp", query,
            [this, seriesId](const QJsonDocument &doc) {
        QJsonArray items = doc.object()["Items"].toArray();
        if (!items.isEmpty())
            emit nextUpFetched(items.first().toObject(), seriesId);
        else
            emit nextUpFetched(QJsonObject(), seriesId);
    });
}

// ── Hide from resume ──

void EmbyClient::hideFromResume(const QString &itemId) {
    postJson("/emby/Users/" + m_userId + "/Items/" + itemId + "/HideFromResume", {},
             [this, itemId](const QJsonDocument &) {
        emit hideFromResumeSuccess(itemId);
    });
}

// ── Persons ──

void EmbyClient::fetchPersons(const QString &parentId, int limit) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", "SortName");
    query.addQueryItem("SortOrder", "Ascending");
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);

    getJson("/emby/Persons", query, [this](const QJsonDocument &doc) {
        emit personsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchFavPersons(int limit) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Filters", "IsFavorite");
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", "SortName");
    query.addQueryItem("SortOrder", "Ascending");
    getJson("/emby/Persons", query, [this](const QJsonDocument &doc) {
        emit personsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::searchPersons(const QString &term, int limit) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("SearchTerm", term);
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", "SortName");
    query.addQueryItem("SortOrder", "Ascending");
    getJson("/emby/Persons", query, [this](const QJsonDocument &doc) {
        emit personsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::searchItems(const QString &term, const QString &includeTypes, int limit,
                              JsonArrayCallback callback, const QString &sortBy) {
    QUrlQuery query;
    query.addQueryItem("Recursive", "true");
    query.addQueryItem("Fields", EmbyFields::ListItems);
    query.addQueryItem("ImageTypeLimit", "1");
    query.addQueryItem("SortBy", sortBy);
    query.addQueryItem("SortOrder", "Ascending");
    query.addQueryItem("IncludeItemTypes", includeTypes);
    if (!term.isEmpty())
        query.addQueryItem("SearchTerm", term);
    if (limit > 0)
        query.addQueryItem("Limit", QString::number(limit));

    getJson("/emby/Users/" + m_userId + "/Items", query, [callback](const QJsonDocument &doc) {
        callback(doc.object()["Items"].toArray());
    });
}

// ── Additional filter types ──

void EmbyClient::fetchTags(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    getJson("/emby/Tags", query, [this](const QJsonDocument &doc) {
        emit tagsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchYears(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    getJson("/emby/Years", query, [this](const QJsonDocument &doc) {
        emit yearsFetched(doc.object()["Items"].toArray());
    });
}

void EmbyClient::fetchOfficialRatings(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);
    getJson("/emby/OfficialRatings", query, [this](const QJsonDocument &doc) {
        emit officialRatingsFetched(doc.object()["Items"].toArray());
    });
}

// ── Additional Parts ──

void EmbyClient::fetchAdditionalParts(const QString &itemId) {
    getJson("/emby/Videos/" + itemId + "/AdditionalParts", {}, [this](const QJsonDocument &doc) {
        emit additionalPartsFetched(doc.object()["Items"].toArray());
    });
}

// ── Missing Episodes ──

void EmbyClient::fetchMissingEpisodes(const QString &parentId) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Limit", "50");
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");
    if (!parentId.isEmpty())
        query.addQueryItem("ParentId", parentId);

    getJson("/emby/Shows/Missing", query, [this](const QJsonDocument &doc) {
        emit missingEpisodesFetched(doc.object()["Items"].toArray());
    });
}

// ── Live TV ──

void EmbyClient::fetchLiveTvChannels(int limit) {
    QUrlQuery query;
    query.addQueryItem("UserId", m_userId);
    query.addQueryItem("Limit", QString::number(limit));
    query.addQueryItem("Fields", EmbyFields::Card);
    query.addQueryItem("ImageTypeLimit", "1");

    getJson("/emby/LiveTv/Channels", query, [this](const QJsonDocument &doc) {
        emit liveTvChannelsFetched(doc.object()["Items"].toArray());
    });
}

// ── Server Info ──

void EmbyClient::fetchServerInfo() {
    getJson("/emby/System/Info", {}, [this](const QJsonDocument &doc) {
        emit serverInfoFetched(doc.object());
    });
}

