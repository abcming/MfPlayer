#pragma once
#include <QQuickAsyncImageProvider>
#include <QQuickImageResponse>
#include <QPixmap>
#include <QMutex>
#include <QSize>
#include <thread>
#include <list>
#include <unordered_map>

class CacheStore;

class ImageCacheResponse : public QQuickImageResponse {
    Q_OBJECT
public:
    struct CacheEntry {
        QPixmap pixmap;
        std::list<QString>::iterator lruIt;
    };

    ImageCacheResponse(const QString &id, const QString &path,
                       const QSize &requestedSize, QMutex *cacheMutex,
                       std::unordered_map<QString, CacheEntry> *memCache,
                       std::list<QString> *lru, int maxEntries,
                       bool skipProcess = false);
    ~ImageCacheResponse() override;
    QQuickTextureFactory *textureFactory() const override;

    QPixmap m_pixmap;  // public so provider can set it for cache-hit fast path

private:
    void process();
    mutable QMutex m_pixmapMutex;  // protects m_pixmap between process() and textureFactory()
    QString m_path;
    QSize m_requestedSize;
    QMutex *m_cacheMutex;
    std::unordered_map<QString, CacheEntry> *m_memCache;
    std::list<QString> *m_lru;
    int m_maxEntries;
    QString m_id;
    std::thread m_thread;
};

class ImageCacheProvider : public QQuickAsyncImageProvider {
public:
    explicit ImageCacheProvider(CacheStore *cache);

    // Async path — called when QML Image has asynchronous: true
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

    // Sync fallback — called when QML Image has asynchronous: false (e.g. logo)
    QPixmap requestPixmap(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    friend class ImageCacheResponse;
    using CacheEntry = ImageCacheResponse::CacheEntry;
    CacheStore *m_cache;
    mutable QMutex m_mutex;
    std::unordered_map<QString, CacheEntry> m_memCache;  // O(1) lookup
    std::list<QString> m_lru;                              // front = most recent, O(1) splice
    static const int kMaxEntries = 500;
};
