#include "core/detail/detailmanager.h"
#include "core/media/models/mediamodel.h"
#include "common/constants.h"
#include <algorithm>

static QJsonArray sortByIndexNumber(QJsonArray arr) {
    // Pre-extract sort keys to avoid toObject() + JSON lookup per comparator call
    struct Pair { int key; QJsonValue val; };
    QVector<Pair> pairs;
    pairs.reserve(arr.size());
    for (const auto &v : arr)
        pairs.append({v.toObject()["IndexNumber"].toInt(), v});
    std::sort(pairs.begin(), pairs.end(),
              [](const Pair &a, const Pair &b) { return a.key < b.key; });
    QJsonArray result;
    for (const auto &p : pairs) result.append(p.val);
    return result;
}

DetailManager::DetailManager(EmbyClient *emby, CacheStore *cache, QObject *parent)
    : QObject(parent)
    , m_emby(emby)
    , m_cache(cache)
    , m_seasonModel(new MediaModel(this))
    , m_episodeModel(new MediaModel(this))
    , m_resumeModel(new MediaModel(this))
    , m_similarModel(new MediaModel(this))
    , m_personMoviesModel(new MediaModel(this))
    , m_personSeriesModel(new MediaModel(this))
{
    connect(m_emby, &EmbyClient::itemDetailFetched, this, &DetailManager::onItemDetailFetched);
    connect(m_emby, &EmbyClient::seasonsFetched, this, &DetailManager::onSeasonsFetched);
    connect(m_emby, &EmbyClient::episodesFetched, this, &DetailManager::onEpisodesFetched);

    connect(m_emby, &EmbyClient::resumeFetched, this, [this](const QJsonArray &items) {
        m_resumeModel->setItems(items);
    });

    connect(m_emby, &EmbyClient::similarFetched, this, [this](const QJsonArray &items, const QString &excludeId) {
        if (!m_similarCache.contains(excludeId)) {
            while (m_similarCache.size() >= kMaxSimilarCacheEntries && !m_similarCacheLru.isEmpty()) {
                QString oldest = m_similarCacheLru.takeFirst();
                m_similarCache.remove(oldest);
            }
            m_similarCacheLru.append(excludeId);
        }
        m_similarCache[excludeId] = items;
        if (excludeId == m_browsingItemId)
            m_similarModel->setItems(items);
    });

    connect(m_emby, &EmbyClient::playedStatusChanged, this, [this](const QString &itemId, bool played) {
        QJsonObject cached = m_cache->getItemDetail(itemId);
        if (!cached.isEmpty()) {
            QJsonObject ud = cached["UserData"].toObject();
            ud["Played"] = played;
            if (played) {
                ud["PlaybackPositionTicks"] = 0;
                ud["PlayedPercentage"] = 0;
            }
            cached["UserData"] = ud;
            m_cache->putItemDetail(itemId, cached);
        }
        m_resumeModel->updateItemByRoleName(itemId, "played", played);
        m_similarModel->updateItemByRoleName(itemId, "played", played);
        // Re-fetch resume so fully-played items disappear from Continue Watching.
        // The server-side /Items/Resume endpoint naturally excludes items with 0 progress.
        m_emby->fetchResume(12);
        emit playedStatusChanged(itemId, played);
    });

    connect(m_emby, &EmbyClient::favoriteChanged, this, [this](const QString &itemId, bool isFavorite) {
        QJsonObject cached = m_cache->getItemDetail(itemId);
        if (!cached.isEmpty()) {
            QJsonObject ud = cached["UserData"].toObject();
            ud["IsFavorite"] = isFavorite;
            cached["UserData"] = ud;
            m_cache->putItemDetail(itemId, cached);
        }
        m_resumeModel->updateItemByRoleName(itemId, "isFavorite", isFavorite);
        m_similarModel->updateItemByRoleName(itemId, "isFavorite", isFavorite);
        emit favoriteChanged(itemId, isFavorite);
    });

    connect(m_emby, &EmbyClient::personMoviesFetched, this, [this](const QJsonArray &items, const QString &personId) {
        if (personId != m_browsingItemId) return;
        m_personMoviesModel->setItems(items);
    });

    connect(m_emby, &EmbyClient::personSeriesFetched, this, [this](const QJsonArray &items, const QString &personId) {
        if (personId != m_browsingItemId) return;
        m_personSeriesModel->setItems(items);
    });

    connect(m_emby, &EmbyClient::nextUpFetched, this, [this](const QJsonObject &ep, const QString &seriesId) {
        if (seriesId != m_browsingItemId) return;
        if (ep.isEmpty()) {
            m_nextEpisode.clear();
        } else {
            QVariantMap ne;
            ne["Id"] = ep["Id"].toString();
            ne["Name"] = ep["Name"].toString();
            ne["SeriesName"] = ep["SeriesName"].toString();
            ne["IndexNumber"] = ep["IndexNumber"].toInt();
            ne["ParentIndexNumber"] = ep["ParentIndexNumber"].toInt();
            ne["SeasonId"] = ep["SeasonId"].toString();
            auto ud = ep["UserData"].toObject();
            ne["PlaybackPositionTicks"] = static_cast<qint64>(ud["PlaybackPositionTicks"].toDouble());
            ne["Played"] = ud["Played"].toBool();
            m_nextEpisode = ne;
        }
        emit nextEpisodeChanged();
    });
}

// ── Property getters ────────────────────────────────────────────────

MediaModel *DetailManager::seasonModel() const { return m_seasonModel; }
MediaModel *DetailManager::episodeModel() const { return m_episodeModel; }
MediaModel *DetailManager::resumeModel() const { return m_resumeModel; }
MediaModel *DetailManager::similarModel() const { return m_similarModel; }
MediaModel *DetailManager::personMoviesModel() const { return m_personMoviesModel; }
MediaModel *DetailManager::personSeriesModel() const { return m_personSeriesModel; }

// ── Public slots ────────────────────────────────────────────────────

void DetailManager::browseItem(const QString &itemId) {
    // Same item as before — preserve season/episode selection
    if (itemId == m_browsingItemId) {
        QJsonObject cached = m_cache->getItemDetail(itemId);
        if (!cached.isEmpty()) {
            emit itemDetailReady(itemId, cached.toVariantMap());
            return;
        }
    }
    m_browsingItemId = itemId;
    m_seasonModel->clear();
    m_episodeModel->clear();
    m_personMoviesModel->clear();
    m_personSeriesModel->clear();

    QJsonObject cached = m_cache->getItemDetail(itemId);
    if (!cached.isEmpty()) {
        QString type = cached["Type"].toString();
        if (!type.isEmpty()) {
            if (m_similarCache.contains(itemId))
                m_similarModel->setItems(m_similarCache[itemId]);
            else if (type == Constants::kTypePerson) {
                m_emby->fetchPersonFilms(itemId, Constants::kTypeMovie);
                m_emby->fetchPersonFilms(itemId, Constants::kTypeSeries);
            } else {
                m_emby->fetchSimilar(type, itemId);
            }
            if (type == Constants::kTypeSeries)
                m_emby->fetchNextUp(itemId);
        }
        emit itemDetailReady(itemId, cached.toVariantMap());
        return;
    }
    m_emby->fetchItemDetail(itemId);
}

void DetailManager::fetchSeasons(const QString &seriesId) {
    // Same series with seasons already loaded — preserve user's season selection
    if (seriesId == m_currentSeriesId && m_seasonModel->rowCount() > 0) {
        // Just re-fetch episodes for the current season (browseItem cleared episodeModel)
        QJsonArray cached = m_cache->getSeasons(seriesId);
        if (!cached.isEmpty()) {
            cached = sortByIndexNumber(cached);
            // Find the season matching m_currentSeasonId and fetch its episodes
            for (const auto &s : cached) {
                if (s.toObject()["Id"].toString() == m_currentSeasonId) {
                    m_emby->fetchEpisodes(seriesId, m_currentSeasonId);
                    return;
                }
            }
            // Previous season no longer exists — fall through to full refresh
        }
    }
    m_currentSeriesId = seriesId;

    QJsonArray cached = m_cache->getSeasons(seriesId);
    if (!cached.isEmpty()) {
        cached = sortByIndexNumber(cached);
        m_seasonModel->setItems(cached);
        emit seasonsChanged();
        // Preserve m_currentSeasonId if it still exists in the season list
        QString targetId;
        for (const auto &s : cached) {
            if (s.toObject()["Id"].toString() == m_currentSeasonId) {
                targetId = m_currentSeasonId;
                break;
            }
        }
        if (targetId.isEmpty())
            targetId = cached.first().toObject()["Id"].toString();
        m_currentSeasonId = targetId;
        m_emby->fetchEpisodes(seriesId, m_currentSeasonId);
        return;
    }
    // Cache miss: clear now so QML doesn't show previous series' seasons
    m_seasonModel->clear();
    m_episodeModel->clear();
    m_emby->fetchSeasons(seriesId);
}

void DetailManager::fetchEpisodes(const QString &seriesId, const QString &seasonId) {
    m_currentSeriesId = seriesId;
    m_currentSeasonId = seasonId;
    QJsonArray cached = m_cache->getEpisodes(seriesId, seasonId);
    if (!cached.isEmpty()) {
        m_episodeModel->setItems(cached);
        return;
    }
    // Cache miss: clear now so QML doesn't show previous season's episodes
    m_episodeModel->clear();
    m_emby->fetchEpisodes(seriesId, seasonId);
}

void DetailManager::clearAll() {
    m_seasonModel->clear();
    m_episodeModel->clear();
    m_resumeModel->clear();
    m_similarModel->clear();
    m_personMoviesModel->clear();
    m_personSeriesModel->clear();
    m_similarCache.clear();
    m_similarCacheLru.clear();
    m_browsingItemId.clear();
    m_currentSeriesId.clear();
    m_currentSeasonId.clear();
    m_nextEpisode.clear();
}

// ── Private slots (EmbyClient signal handlers) ──────────────────────

void DetailManager::onItemDetailFetched(const QJsonObject &detail) {
    QString itemId = detail["Id"].toString();
    if (itemId.isEmpty()) return;  // HTTP error returned empty doc
    m_cache->putItemDetail(itemId, detail);
    {
        QString type = detail["Type"].toString();
        if (!type.isEmpty()) {
            if (type == Constants::kTypePerson) {
                m_emby->fetchPersonFilms(itemId, Constants::kTypeMovie);
                m_emby->fetchPersonFilms(itemId, Constants::kTypeSeries);
            } else {
                m_emby->fetchSimilar(type, itemId);
                if (type == Constants::kTypeSeries)
                    m_emby->fetchNextUp(itemId);
            }
        }
    }
    emit itemDetailReady(itemId, detail.toVariantMap());
}

void DetailManager::onSeasonsFetched(const QJsonArray &seasons, const QString &seriesId) {
    // 响应必须属于当前正在看的剧集。不判的话: 请求 A 在飞时用户翻到 B,
    // A 的季列表回来会被 putSeasons(B) 写进 SQLite (持久污染), 紧接着 :264
    // 还会用 "B 的剧集 id + A 的季 id" 再发一个请求, 回来再污染一次集缓存
    if (seriesId != m_currentSeriesId) return;
    QJsonArray sorted = sortByIndexNumber(seasons);
    m_cache->putSeasons(seriesId, sorted);
    m_seasonModel->setItems(sorted);
    emit seasonsChanged();
    if (!sorted.isEmpty()) {
        m_currentSeasonId = sorted.first().toObject()["Id"].toString();
        m_emby->fetchEpisodes(seriesId, m_currentSeasonId);
    }
}

void DetailManager::onEpisodesFetched(const QJsonArray &episodes, const QString &seriesId,
                                      const QString &seasonId) {
    if (seriesId != m_currentSeriesId || seasonId != m_currentSeasonId) return;
    m_cache->putEpisodes(seriesId, seasonId, episodes);
    m_episodeModel->setItems(episodes);
}
