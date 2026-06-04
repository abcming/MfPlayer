#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QUrlQuery>
#include <functional>
#include <memory>

#include "core/network/curlengine.h"

using JsonArrayCallback = std::function<void(const QJsonArray &)>;

struct FetchParams {
    QString parentId;
    QString includeTypes;
    QString filters;
    QString sortBy;
    QString sortOrder;
    QString genreIds;
    QString studioIds;
    QString personIds;
    QString tags;
    QString years;
    QString officialRatings;
    QString searchTerm;
    int limit = 0;
    int startIndex = 0;
    bool recursive = true;
};

class EmbyClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool authenticated READ authenticated NOTIFY authenticatedChanged FINAL)
    Q_PROPERTY(QString serverUrl READ serverUrl NOTIFY serverUrlChanged FINAL)
    Q_PROPERTY(QString accessToken READ accessToken NOTIFY accessTokenChanged FINAL)
    Q_PROPERTY(QString userId READ userId NOTIFY userIdChanged FINAL)

public:
    explicit EmbyClient(QObject *parent = nullptr);
    ~EmbyClient() override;

    bool authenticated() const;
    QString serverUrl() const;
    QString accessToken() const;
    QString userId() const;

    Q_INVOKABLE void setServer(const QString &url);
    Q_INVOKABLE void setAuth(const QString &token, const QString &userId);
    Q_INVOKABLE void logout();
    void setSkipSslVerify(bool skip);

    // Resolve a relative API path to full URL (e.g. "/emby/Items/..." → "http://server/emby/Items/...")
    Q_INVOKABLE QString imageUrl(const QString &relativePath) const {
        if (relativePath.isEmpty()) return {};
        return m_serverUrl + relativePath;
    }

public slots:
    void login(const QString &username, const QString &password);
    void fetchLibraries();
    void fetchItems(const QString &parentId, const QString &includeTypes = QString(), bool recursive = true);
    void fetchItemDetail(const QString &itemId);
    void fetchSeasons(const QString &seriesId);
    void fetchEpisodes(const QString &seriesId, const QString &seasonId);
    void fetchResume(int limit = 12);
    void fetchLatest(int limit, const QString &parentId, const QString &includeTypes, const QString &tag);
    void fetchSimilar(const QString &includeTypes, const QString &excludeId, int limit = 10);
    void fetchPersonFilms(const QString &personId, const QString &includeTypes);

public:
    void fetchPlaybackInfo(const QString &itemId,
                            std::function<void(const QString &streamUrl, const QString &playSessionId, const QJsonArray &mediaSources)> callback,
                            const QString &mediaSourceId = QString(),
                            int subtitleStreamIndex = -1);

public slots:
    Q_INVOKABLE void reportPlaybackStart(const QString &itemId, qint64 positionTicks = 0,
                                         const QString &playSessionId = QString(),
                                         const QString &mediaSourceId = QString(),
                                         std::function<void()> onReady = nullptr);
    Q_INVOKABLE void reportPlaybackProgress(const QString &itemId, qint64 positionTicks,
                                             const QString &playSessionId = QString(),
                                             const QString &mediaSourceId = QString(),
                                             bool isPaused = false);
    Q_INVOKABLE void reportPlaybackStop(const QString &itemId, qint64 positionTicks,
                                         const QString &playSessionId = QString(),
                                         const QString &mediaSourceId = QString(),
                                         std::function<void()> onReady = nullptr);

    // Played status
    Q_INVOKABLE void markPlayed(const QString &itemId);
    Q_INVOKABLE void markUnplayed(const QString &itemId);

    // Favorites
    Q_INVOKABLE void addFavorite(const QString &itemId);
    Q_INVOKABLE void removeFavorite(const QString &itemId);

    // Search
    Q_INVOKABLE void searchHints(const QString &term,
                                  const QString &includeTypes = "Movie,Series");

    // Genres & Studios
    Q_INVOKABLE void fetchGenres(const QString &parentId);
    Q_INVOKABLE void fetchStudios(const QString &parentId);

    // Extended fetchItems with optional filters
    Q_INVOKABLE void fetchItemsFiltered(const FetchParams &params);
    void fetchItemsFiltered(const FetchParams &params, JsonArrayCallback callback);

    // Suggestions (uses /Users/{UserId}/Suggestions endpoint)
    Q_INVOKABLE void fetchSuggestions(const QString &parentId, const QString &includeTypes = QString());

    // Item counts
    Q_INVOKABLE void fetchItemCounts();
    // Next-Up for Series
    Q_INVOKABLE void fetchNextUp(const QString &seriesId);

    // Suggestions tab: library-filtered resume + latest
    Q_INVOKABLE void fetchSuggestionsResume(const QString &parentId);
    Q_INVOKABLE void fetchSuggestionsLatest(const QString &parentId, const QString &includeTypes);

    // Hide from resume
    Q_INVOKABLE void hideFromResume(const QString &itemId);

    // Persons list
    Q_INVOKABLE void fetchPersons(const QString &parentId, int limit = 30);
    Q_INVOKABLE void fetchFavPersons(int limit = 50);
    Q_INVOKABLE void searchPersons(const QString &term, int limit = 20);
    void searchItems(const QString &term, const QString &includeTypes, int limit,
                     JsonArrayCallback callback, const QString &sortBy = "SortName");

    // Additional filter types
    Q_INVOKABLE void fetchTags(const QString &parentId);
    Q_INVOKABLE void fetchYears(const QString &parentId);
    Q_INVOKABLE void fetchOfficialRatings(const QString &parentId);

    // Multi-part video
    Q_INVOKABLE void fetchAdditionalParts(const QString &itemId);

    // Missing episodes
    Q_INVOKABLE void fetchMissingEpisodes(const QString &parentId);

    // Live TV
    Q_INVOKABLE void fetchLiveTvChannels(int limit = 50);

    // Server info
    Q_INVOKABLE void fetchServerInfo();

signals:
    void authenticatedChanged();
    void serverUrlChanged();
    void accessTokenChanged();
    void userIdChanged();
    void loginSuccess(const QString &token, const QString &userId);
    void loginFailed(const QString &error);
    void librariesFetched(const QJsonArray &libraries);
    void itemsFetched(const QJsonArray &items, const QString &parentId, const QString &cacheKey, int totalRecordCount = -1);
    void itemDetailFetched(const QJsonObject &detail);
    void seasonsFetched(const QJsonArray &seasons);
    void episodesFetched(const QJsonArray &episodes);
    void resumeFetched(const QJsonArray &items);
    void latestFetched(const QJsonArray &items, const QString &tag);
    void similarFetched(const QJsonArray &items, const QString &excludeId);
    void playedStatusChanged(const QString &itemId, bool played);
    void favoriteChanged(const QString &itemId, bool isFavorite);
    void searchHintsFetched(const QJsonArray &hints);
    void genresFetched(const QJsonArray &genres);
    void studiosFetched(const QJsonArray &studios);
    void itemCountsFetched(int movieCount, int seriesCount, int episodeCount);
    void nextUpFetched(const QJsonObject &episode, const QString &seriesId);
    void suggestionsResumeFetched(const QJsonArray &items);
    void suggestionsLatestFetched(const QJsonArray &items);
    void personMoviesFetched(const QJsonArray &items, const QString &personId);
    void personSeriesFetched(const QJsonArray &items, const QString &personId);
    void hideFromResumeSuccess(const QString &itemId);
    void personsFetched(const QJsonArray &items);
    void tagsFetched(const QJsonArray &items);
    void yearsFetched(const QJsonArray &items);
    void officialRatingsFetched(const QJsonArray &items);
    void additionalPartsFetched(const QJsonArray &parts);
    void missingEpisodesFetched(const QJsonArray &items);
    void liveTvChannelsFetched(const QJsonArray &channels);
    void serverInfoFetched(const QJsonObject &info);
    void tokenExpired();
    void networkError(const QString &error);

private:
    CurlEngine::Headers defaultHeaders() const;
    QByteArray authHeader() const;
    void getJson(const QString &path, const QUrlQuery &query,
                 std::function<void(const QJsonDocument &)> callback);
    void postJson(const QString &path, const QJsonObject &body,
                  std::function<void(const QJsonDocument &)> callback);
    void deleteJson(const QString &path,
                    std::function<void()> callback);

    void invalidateHeaderCache();
    QJsonObject buildPlaybackBody(const QString &itemId, qint64 positionTicks, bool isPaused);

    std::unique_ptr<CurlEngine> m_curl;
    QString m_serverUrl;
    QString m_accessToken;
    QString m_userId;
    QString m_sessionId;
    bool m_authenticated = false;
    mutable QByteArray m_cachedAuthHeader;
    mutable CurlEngine::Headers m_cachedHeaders;
};
