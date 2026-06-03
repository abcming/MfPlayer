#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QVariant>
#include <QHash>
#include <QString>
#include "core/providers/emby/embyclient.h"
#include "core/cache/cachestore.h"
#include "core/media/models/mediamodel.h"

class DetailManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(MediaModel* seasonModel READ seasonModel CONSTANT)
    Q_PROPERTY(MediaModel* episodeModel READ episodeModel CONSTANT)
    Q_PROPERTY(MediaModel* resumeModel READ resumeModel CONSTANT)
    Q_PROPERTY(MediaModel* similarModel READ similarModel CONSTANT)
    Q_PROPERTY(MediaModel* personMoviesModel READ personMoviesModel CONSTANT)
    Q_PROPERTY(MediaModel* personSeriesModel READ personSeriesModel CONSTANT)
    Q_PROPERTY(QVariantMap nextEpisode READ nextEpisode NOTIFY nextEpisodeChanged FINAL)

public:
    explicit DetailManager(EmbyClient *emby, CacheStore *cache, QObject *parent = nullptr);

    MediaModel *seasonModel() const;
    MediaModel *episodeModel() const;
    MediaModel *resumeModel() const;
    MediaModel *similarModel() const;
    MediaModel *personMoviesModel() const;
    MediaModel *personSeriesModel() const;
    QVariantMap nextEpisode() const { return m_nextEpisode; }

public slots:
    Q_INVOKABLE void browseItem(const QString &itemId);
    Q_INVOKABLE void fetchSeasons(const QString &seriesId);
    Q_INVOKABLE void fetchEpisodes(const QString &seriesId, const QString &seasonId);
    Q_INVOKABLE void refreshResume() { m_emby->fetchResume(12); }
    Q_INVOKABLE void markPlayed(const QString &itemId) { m_emby->markPlayed(itemId); }
    Q_INVOKABLE void markUnplayed(const QString &itemId) { m_emby->markUnplayed(itemId); }
    Q_INVOKABLE void addFavorite(const QString &itemId) { m_emby->addFavorite(itemId); }
    Q_INVOKABLE void removeFavorite(const QString &itemId) { m_emby->removeFavorite(itemId); }
    void clearAll();

signals:
    void itemDetailReady(const QString &itemId, const QVariantMap &data);
    void seasonsChanged();
    void nextEpisodeChanged();
    void playedStatusChanged(const QString &itemId, bool played);
    void favoriteChanged(const QString &itemId, bool isFavorite);

private:
    void onItemDetailFetched(const QJsonObject &detail);
    void onSeasonsFetched(const QJsonArray &seasons);
    void onEpisodesFetched(const QJsonArray &episodes);

    EmbyClient *m_emby;
    CacheStore *m_cache;
    MediaModel *m_seasonModel;
    MediaModel *m_episodeModel;
    MediaModel *m_resumeModel;
    MediaModel *m_similarModel;
    MediaModel *m_personMoviesModel;
    MediaModel *m_personSeriesModel;
    QString m_browsingItemId;
    QString m_currentSeriesId;
    QString m_currentSeasonId;
    QVariantMap m_nextEpisode;
    QHash<QString, QJsonArray> m_similarCache;
};
