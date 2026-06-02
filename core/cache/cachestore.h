#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QMutex>
#include <QSet>
#include <memory>
#include <thread>
#include <vector>
#include <mutex>

class CurlEngine;

class CacheStore : public QObject {
    Q_OBJECT

public:
    explicit CacheStore(QObject *parent = nullptr);
    ~CacheStore() override;

    void init();
    QSqlDatabase database() const { return m_db; }

    // ── Content cache (items, details, seasons, episodes) ─────────
    QJsonArray getItems(const QString &parentId);
    void putItems(const QString &parentId, const QJsonArray &items);

    QJsonObject getItemDetail(const QString &itemId);
    void putItemDetail(const QString &itemId, const QJsonObject &detail);

    QJsonArray getSeasons(const QString &seriesId);
    void putSeasons(const QString &seriesId, const QJsonArray &seasons);

    QJsonArray getEpisodes(const QString &seriesId, const QString &seasonId);
    void putEpisodes(const QString &seriesId, const QString &seasonId, const QJsonArray &episodes);

    // ── Image cache (disk + memory) ───────────────────────────────
    QString getImagePath(const QString &url);
    void putImagePath(const QString &url, const QString &localPath);
    QString imageSavePath(const QString &url) const;
    Q_INVOKABLE QString cachedImageUrl(const QString &url);   // file:// URL if cached
    QString resolveImagePath(const QString &urlHash) const;   // raw path for ImageCacheProvider
    Q_INVOKABLE void fetchImage(const QString &url);          // async download + cache

    // ── Cache lifecycle ───────────────────────────────────────────
    void updateItemFieldInCache(const QString &itemId, const QString &fieldName, const QVariant &value);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void clearContentCache();
    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void expireCache();

signals:
    void imageReady(const QString &url, const QString &localPath);

private:
    void initTables();
    void loadImageCache();
    bool isFresh(qint64 timestamp) const;
    void doFetchImage(const QString &url, int retries);
    void processDownloadQueue();

    QSqlDatabase m_db;
    std::unique_ptr<CurlEngine> m_curl;
    QString m_cacheDir;
    mutable QMutex m_imageCacheMutex;
    QHash<QString, QString> m_imageCache;       // url → filePath
    QSet<QString> m_pendingDownloads;            // hashes of in-flight downloads
    QHash<QString, QJsonArray> m_itemsCache;     // parentId → items
    QHash<QString, qint64> m_itemsCacheTime;     // parentId → fetched_at
    QHash<QString, QJsonObject> m_detailCache;   // itemId → detail JSON
    QHash<QString, QJsonArray> m_seasonsCache;   // seriesId → seasons
    QHash<QString, QJsonArray> m_episodesCache;  // "seriesId\0seasonId" → episodes
    int m_activeDownloads = 0;
    QList<QPair<QString, int>> m_downloadQueue;
    static const int kMaxActiveDownloads = 8;
    uint32_t m_writeGeneration = 0;  // incremented by clearContentCache to cancel deferred writes
    std::thread m_expireThread;
    std::thread m_clearThread;
    std::vector<std::thread> m_workerThreads;  // image validation+write threads
    std::mutex m_workerMutex;                  // protects m_workerThreads
};
