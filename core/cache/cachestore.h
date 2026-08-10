#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>
#include <QSet>
#include <memory>

class CurlEngine;
class DBWorker;

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
    // "image://imgcache/<hash>/<base64url(path)>" — 路径在主线程解析好后随请求
    // 一起交给 provider, reader 线程不再回头读 m_imageCache (见 .cpp 说明)
    // QML 侧唯一的图片入口: 未命中返回空, 命中判据和结果都靠它, 别再拆成两次调用
    Q_INVOKABLE QString providerUrl(const QString &url) const;
    QString imageCacheDir() const { return m_cacheDir; }      // provider 构造时取一次, 做路径收口
    Q_INVOKABLE void fetchImage(const QString &url);          // async download + cache
    void setSkipSslVerify(bool skip);

    // ── Cache lifecycle ───────────────────────────────────────────
    void updateItemFieldInCache(const QString &itemId, const QString &fieldName, const QVariant &value);
    Q_INVOKABLE void clearAll();
    Q_INVOKABLE void clearContentCache();
    Q_INVOKABLE void clearImageCache();
    Q_INVOKABLE void expireCache();

signals:
    void imageReady(const QString &url, const QString &localPath);

private:
    void connectDBWorker();
    bool isFresh(qint64 timestamp) const;
    // Insert into m_itemsCache with LRU bookkeeping — the ONLY way entries may
    // enter m_itemsCache (an entry present in the cache but absent from the LRU
    // list makes putItems() call QList::move(-1, ...) — UB in release builds).
    void cacheItemsInMemory(const QString &parentId, const QJsonArray &items);
    // url: actual request URL (may carry format=jpg on retry);
    // origUrl: original URL used for all bookkeeping (hash, imageReady, cache key)
    // gen = 发起时的 m_writeGeneration。重试链一路带着它, 写回前比对 —— 中途
    // clearImageCache() 过的话整条链的成果一律作废 (见 .cpp 写回处)
    void doFetchImage(const QString &url, int retries, const QString &origUrl, uint32_t gen);
    void processDownloadQueue();

    QSqlDatabase m_db;  // main-thread read connection
    DBWorker *m_dbWorker = nullptr;
    std::unique_ptr<CurlEngine> m_curl;
    QString m_cacheDir;
    // All accessed exclusively on the main thread — no mutex needed.
    struct ImageCacheEntry {
        QString filePath;  // absolute path on disk
        QString fileUrl;   // pre-built "file://" URL (avoids QUrl::fromLocalFile every cache hit)
    };
    QHash<QString, ImageCacheEntry> m_imageCache;  // urlHash → entry
    QHash<QString, QJsonArray> m_itemsCache;     // parentId → items (LRU bounded)
    QHash<QString, qint64> m_itemsCacheTime;     // parentId → fetched_at
    QList<QString> m_itemsCacheLru;              // LRU order for m_itemsCache
    static const int kMaxItemCacheEntries = 200;
    QHash<QString, QJsonObject> m_detailCache;   // itemId → detail JSON
    QHash<QString, QJsonArray> m_seasonsCache;   // seriesId → seasons
    QHash<QString, QJsonArray> m_episodesCache;  // "seriesId\0seasonId" → episodes
    QSet<QString> m_pendingDownloads;            // hashes of in-flight downloads
    QHash<QString, qint64> m_failedUrls;         // hash → failure timestamp (cooldown)
    int m_activeDownloads = 0;
    QList<QPair<QString, int>> m_downloadQueue;
    static const int kMaxActiveDownloads = 8;
    // 清缓存时 ++。**它不是靠 DBWorker 生效的** —— 那边五个 generation 形参全是
    // Q_UNUSED, 而且本来也不需要: 投递是 Qt::QueuedConnection, DBWorker 串行处理,
    // clear 之前投递的写入必定在 DELETE 之前执行, 一律会被扫掉。
    // 真正需要它的只有图片下载那条异步链 (下载→IO 池→回主线程写回), 那条链跨得过
    // clear, 闸门在 doFetchImage() 的写回处比对本值。
    uint32_t m_writeGeneration = 0;
};
