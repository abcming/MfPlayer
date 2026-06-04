#include "core/cache/imagecacheprovider.h"
#include "core/cache/cachestore.h"
#include <QSize>
#include <QSizeF>
#include <QImageReader>
#include <QTimer>
#include <QMutexLocker>
#include <QQuickTextureFactory>

// ── Async response ────────────────────────────────────────────────

ImageCacheResponse::ImageCacheResponse(const QString &id, const QString &path,
                                       const QSize &requestedSize, QMutex *cacheMutex,
                                       std::unordered_map<QString, CacheEntry> *memCache,
                                       std::list<QString> *lru, int maxEntries,
                                       bool skipProcess)
    : m_path(path), m_requestedSize(requestedSize)
    , m_cacheMutex(cacheMutex), m_memCache(memCache), m_lru(lru)
    , m_maxEntries(maxEntries), m_id(id)
{
    if (skipProcess) {
        // Cache hit: m_pixmap already set by caller, no thread needed.
        // Defer finished() — cannot emit in constructor.
        QTimer::singleShot(0, this, &QQuickImageResponse::finished);
    } else {
        // Cache miss: decode on background thread
        m_thread = std::thread([this]() { process(); });
    }
}

ImageCacheResponse::~ImageCacheResponse() {
    if (m_thread.joinable())
        m_thread.join();
}

void ImageCacheResponse::process() {
    if (m_path.isEmpty()) {
        m_pixmap = QPixmap(1, 1);
        m_pixmap.fill(Qt::transparent);
        // Queue finished() to main thread — safe if object is destroyed before delivery
        QMetaObject::invokeMethod(this, [this]() { emit finished(); }, Qt::QueuedConnection);
        return;
    }

    // Disk I/O + decode — runs on background thread, NOT the render thread
    QPixmap decoded;
    if (m_requestedSize.isValid() && m_requestedSize.width() > 0) {
        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        QSize orig = reader.size();
        if (orig.isValid() && (orig.width() > m_requestedSize.width()
                            || orig.height() > m_requestedSize.height())) {
            QSize scaled = QSizeF(orig).scaled(QSizeF(m_requestedSize), Qt::KeepAspectRatio).toSize();
            reader.setScaledSize(scaled);
        }
        QImage img = reader.read();
        if (!img.isNull())
            decoded = QPixmap::fromImage(img);
    }
    if (decoded.isNull())
        decoded.load(m_path);

    if (decoded.isNull()) {
        decoded = QPixmap(1, 1);
        decoded.fill(Qt::transparent);
    } else {
        // Publish pixmap BEFORE cache insert — textureFactory() runs after
        // finished() signal, so ordering guarantee makes m_pixmapMutex unnecessary.
        m_pixmap = decoded;
        // Insert into shared memory cache (protected by provider's mutex).
        // Use std::move to avoid QPixmap deep-copy under the mutex.
        QMutexLocker lock(m_cacheMutex);
        m_lru->push_front(m_id);
        (*m_memCache)[m_id] = {std::move(decoded), m_lru->begin()};
        while (static_cast<int>(m_lru->size()) > m_maxEntries) {
            m_memCache->erase(m_lru->back());
            m_lru->pop_back();
        }
    }

    // Queue finished() to main thread — safe if object is destroyed before delivery
    QMetaObject::invokeMethod(this, [this]() { emit finished(); }, Qt::QueuedConnection);
}

QQuickTextureFactory *ImageCacheResponse::textureFactory() const {
    // No mutex needed: m_pixmap is set before finished() is emitted,
    // and Qt only calls textureFactory() after finished().
    return QQuickTextureFactory::textureFactoryForImage(m_pixmap.toImage());
}

// ── Async image provider ──────────────────────────────────────────

ImageCacheProvider::ImageCacheProvider(CacheStore *cache)
    : QQuickAsyncImageProvider()
    , m_cache(cache)
{
}

QQuickImageResponse *ImageCacheProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    int slash = id.lastIndexOf('/');
    QString hash = slash > 0 ? id.left(slash) : id;

    // Fast path: memory cache hit — skip thread, pixmap set directly
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_memCache.find(id);
        if (it != m_memCache.end()) {
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);  // O(1) move to front
            auto *resp = new ImageCacheResponse(id, QString(), requestedSize,
                                                &m_mutex, &m_memCache, &m_lru, kMaxEntries,
                                                /*skipProcess=*/true);
            // Set pixmap before thread could possibly touch it (thread not started)
            resp->m_pixmap = it->second.pixmap;
            return resp;
        }
    }

    // Cache miss: resolve file path, then decode on background thread
    QString path;
    {
        QMutexLocker lock(&m_mutex);
        path = m_cache->resolveImagePath(hash);
    }

    return new ImageCacheResponse(id, path, requestedSize,
                                  &m_mutex, &m_memCache, &m_lru, kMaxEntries,
                                  /*skipProcess=*/false);
}

// requestPixmap() sync fallback removed — all QML images use asynchronous: true.
