#pragma once
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QImage>
#include <QMutex>
#include <QSize>
#include <list>
#include <unordered_map>

class CacheStore;

class ImageCacheResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    struct CacheEntry {
        QImage image;
        std::list<QString>::iterator lruIt;
    };

    ImageCacheResponse(const QString &id, const QString &path,
                       const QSize &requestedSize, QMutex *cacheMutex,
                       std::unordered_map<QString, CacheEntry> *memCache,
                       std::list<QString> *lru, int maxEntries,
                       bool skipProcess = false);
    QQuickTextureFactory *textureFactory() const override;

    QImage m_image;  // public so provider can set it for cache-hit fast path

private:
    void process();
    // m_image ordering: set in process() before finished() emitted → Qt only
    // calls textureFactory() after finished() → write happens-before read. No mutex needed.
    QString m_path;
    QSize m_requestedSize;
    QMutex *m_cacheMutex;
    std::unordered_map<QString, CacheEntry> *m_memCache;
    std::list<QString> *m_lru;
    int m_maxEntries;
    QString m_id;
};

class ImageCacheProvider : public QQuickAsyncImageProvider {
public:
    explicit ImageCacheProvider(CacheStore *cache);

    // Async path — called when QML Image has asynchronous: true
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    friend class ImageCacheResponse;
    using CacheEntry = ImageCacheResponse::CacheEntry;
    // 不持有 CacheStore —— reader 线程读它就是数据竞争。只在构造时 (主线程)
    // 取一次缓存根目录, 用来卡住请求里带过来的路径
    QString m_cacheRoot;
    mutable QMutex m_mutex;
    std::unordered_map<QString, CacheEntry> m_memCache;  // O(1) lookup
    std::list<QString> m_lru;                              // front = most recent, O(1) splice
    static const int kMaxEntries = 200;
};
