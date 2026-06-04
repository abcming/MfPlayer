#pragma once
#include <QObject>
#include <QThread>
#include <QSqlDatabase>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <atomic>

// SQLite worker that runs on its own thread with an independent DB connection.
// All SQL operations are slots invoked via QueuedConnection from the main thread.
// Results are delivered back via named signals — connect before calling.
//
// Owned by CacheStore; also serves CredentialStore through the same thread.
class DBWorker : public QObject {
    Q_OBJECT

public:
    explicit DBWorker(const QString &dbPath, QObject *parent = nullptr);
    ~DBWorker() override;

    // Call from main thread — queues init on the worker thread.
    // Emits ready() when the database is open and tables are created.
    void start();

    // Call from main thread during shutdown — stops accepting work,
    // drains the event queue, and quits the thread (with timeout).
    void stop();

public slots:
    // ── Initialization ──
    void init();

    // ── Content cache writes ──
    void putItems(const QString &parentId, const QJsonArray &items,
                  uint32_t generation, QPointer<QObject> guard);
    void putItemDetail(const QString &itemId, const QString &data,
                       uint32_t generation, QPointer<QObject> guard);
    void putSeasons(const QString &seriesId, const QJsonArray &seasons,
                    uint32_t generation, QPointer<QObject> guard);
    void putEpisodes(const QString &seriesId, const QString &seasonId,
                     const QString &data, uint32_t generation,
                     QPointer<QObject> guard);
    void putImagePath(const QString &urlHash, const QString &localPath,
                      uint32_t generation, QPointer<QObject> guard);

    // ── Image cache maintenance ──
    void loadImageCache(QPointer<QObject> guard);
    void getImagePath(const QString &url, QPointer<QObject> guard);
    void removeStaleImages(const QStringList &urlHashes, QPointer<QObject> guard);

    // ── Cache lifecycle ──
    void expireCache(QPointer<QObject> guard);
    void clearContentCache(QPointer<QObject> guard);
    void clearImageCache(const QString &cacheDir, QPointer<QObject> guard);

    // ── CredentialStore ──
    void addServer(const QString &serverUrl, const QString &username,
                   const QString &token, const QString &userId,
                   QPointer<QObject> guard);
    void getServers(QPointer<QObject> guard);
    void getActiveServer(QPointer<QObject> guard);
    void setActiveServer(int serverId, QPointer<QObject> guard);
    void removeServer(int serverId, QPointer<QObject> guard);
    void updateServerToken(int serverId, const QString &token,
                           QPointer<QObject> guard);
    void getServerById(int serverId, QPointer<QObject> guard);

signals:
    // ── Lifecycle ──
    void ready();

    // ── Content cache results ──
    void itemsWritten(const QString &parentId);
    void itemDetailWritten(const QString &itemId);
    void seasonsWritten(const QString &seriesId);
    void episodesWritten(const QString &seriesId, const QString &seasonId);
    void imagePathWritten(const QString &urlHash);

    // ── Image cache results ──
    void imageCacheLoaded(QHash<QString, QString> validEntries);
    void imagePathReady(const QString &url, const QString &localPath);
    void staleImagesRemoved(const QStringList &hashes);

    // ── Lifecycle results ──
    void expired(QStringList expiredImageHashes);
    void contentCleared();
    void imagesCleared();

    // ── CredentialStore results ──
    void serverAdded(int serverId);
    void serversFetched(QJsonArray servers);
    void activeServerFetched(QJsonObject server);
    void serverRemoved(int serverId);
    void serverTokenUpdated(int serverId);
    void serverByIdFetched(QJsonObject server);

    // ── Errors ──
    void dbError(const QString &message);

private:
    void initTables();
    bool isFresh(qint64 timestamp) const;

    QThread m_thread;
    QSqlDatabase m_db;
    QString m_dbPath;
    std::atomic<bool> m_stopFlag{false};
};
