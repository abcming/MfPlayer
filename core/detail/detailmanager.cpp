#include "core/detail/detailmanager.h"
#include "core/media/models/mediamodel.h"
#include "common/constants.h"
#include <algorithm>

static QJsonArray sortByIndexNumber(QJsonArray arr) {
    QList<QJsonValue> list(arr.begin(), arr.end());
    std::sort(list.begin(), list.end(), [](const QJsonValue &a, const QJsonValue &b) {
        return a.toObject()["IndexNumber"].toInt() < b.toObject()["IndexNumber"].toInt();
    });
    QJsonArray result;
    for (const auto &v : list) result.append(v);
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
    if (!m_browsingItemId.isEmpty() && itemId == m_browsingItemId) {
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
    m_currentSeriesId = seriesId;

    QJsonArray cached = m_cache->getSeasons(seriesId);
    if (!cached.isEmpty()) {
        cached = sortByIndexNumber(cached);
        m_seasonModel->setItems(cached);
        emit seasonsChanged();
        m_currentSeasonId = cached.first().toObject()["Id"].toString();
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

void DetailManager::onSeasonsFetched(const QJsonArray &seasons) {
    QJsonArray sorted = sortByIndexNumber(seasons);
    m_cache->putSeasons(m_currentSeriesId, sorted);
    m_seasonModel->setItems(sorted);
    emit seasonsChanged();
    if (!m_currentSeriesId.isEmpty() && !sorted.isEmpty()) {
        m_currentSeasonId = sorted.first().toObject()["Id"].toString();
        m_emby->fetchEpisodes(m_currentSeriesId, m_currentSeasonId);
    }
}

void DetailManager::onEpisodesFetched(const QJsonArray &episodes) {
    m_cache->putEpisodes(m_currentSeriesId, m_currentSeasonId, episodes);
    m_episodeModel->setItems(episodes);
}
