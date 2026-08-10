#include "core/cache/imagecacheprovider.h"
#include "core/cache/cachestore.h"
#include <QDir>
#include <QSize>
#include <QSizeF>
#include <QImageReader>
#include <QTimer>
#include <QMutexLocker>
#include <QThreadPool>
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
        // Cache hit: m_image already set by caller, no thread needed.
        // Defer finished() — cannot emit in constructor.
        QTimer::singleShot(0, this, &QQuickImageResponse::finished);
    } else {
        // Cache miss: decode on the global CPU pool. Lifetime is safe without
        // join: process() always ends by queueing finished(), and Qt only
        // deletes the response after finished() is delivered on the main thread.
        QThreadPool::globalInstance()->start([this]() { process(); });
    }
}

void ImageCacheResponse::process() {
    if (m_path.isEmpty()) {
        m_image = QImage(1, 1, QImage::Format_ARGB32);
        m_image.fill(Qt::transparent);
        // Queue finished() to main thread — safe if object is destroyed before delivery
        QMetaObject::invokeMethod(this, [this]() { emit finished(); }, Qt::QueuedConnection);
        return;
    }

    // Disk I/O + decode — runs on background thread, NOT the render thread
    QImage decoded;
    if (m_requestedSize.isValid() && m_requestedSize.width() > 0) {
        QImageReader reader(m_path);
        reader.setAutoTransform(true);
        QSize orig = reader.size();
        if (orig.isValid() && (orig.width() > m_requestedSize.width()
                            || orig.height() > m_requestedSize.height())) {
            QSize scaled = QSizeF(orig).scaled(QSizeF(m_requestedSize), Qt::KeepAspectRatio).toSize();
            reader.setScaledSize(scaled);
        }
        decoded = reader.read();
    }
    if (decoded.isNull())
        decoded.load(m_path);

    if (decoded.isNull()) {
        decoded = QImage(1, 1, QImage::Format_ARGB32);
        decoded.fill(Qt::transparent);
    } else {
        // Publish image BEFORE cache insert — textureFactory() runs after
        // finished() signal, so ordering guarantee makes a mutex unnecessary.
        m_image = decoded;
        // Insert into shared memory cache (protected by provider's mutex).
        // Use std::move to avoid deep-copy under the mutex (m_image keeps a
        // shared ref, so the move only transfers the handle).
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
    // No mutex needed: m_image is set before finished() is emitted,
    // and Qt only calls textureFactory() after finished().
    // QImage is implicitly shared — no conversion, no copy (the old
    // QPixmap round-trip cost a ~1.5MB memcpy per request, cache hits included).
    return QQuickTextureFactory::textureFactoryForImage(m_image);
}

// ── Async image provider ──────────────────────────────────────────

ImageCacheProvider::ImageCacheProvider(CacheStore *cache)
    : QQuickAsyncImageProvider()
    , m_cacheRoot(QDir::cleanPath(cache->imageCacheDir()))
{
}

QQuickImageResponse *ImageCacheProvider::requestImageResponse(const QString &id, const QSize &requestedSize) {
    // id = "<hash>/<base64url(绝对路径)>" — 路径由 CacheStore::providerUrl() 在主线程
    // 解析好带过来。这个函数跑在 Qt 的 pixmap reader 线程, 不碰 CacheStore 的任何成员。
    const int slash = id.lastIndexOf('/');
    const QString hash = slash > 0 ? id.left(slash) : id;

    // LRU 键只用 hash — 同一张图换个路径表示不该在 200 条的表里重复占位
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_memCache.find(hash);
        if (it != m_memCache.end()) {
            m_lru.splice(m_lru.begin(), m_lru, it->second.lruIt);  // O(1) move to front
            auto *resp = new ImageCacheResponse(hash, QString(), requestedSize,
                                                &m_mutex, &m_memCache, &m_lru, kMaxEntries,
                                                /*skipProcess=*/true);
            // Shallow copy (implicit sharing) — no decode thread was started
            resp->m_image = it->second.image;
            return resp;
        }
    }

    // Cache miss: 解出请求里带的路径。收口是硬的 —— 带路径的 provider 等于把
    // 任意本地文件读取入口暴露给 QML, 必须卡死在缓存目录内, 越界一律当没有路径
    // (process() 会给出透明图), 不放行也不"记个日志继续"。
    QString path;
    if (slash > 0) {
        const QString decoded = QString::fromUtf8(QByteArray::fromBase64(
            id.mid(slash + 1).toLatin1(), QByteArray::Base64UrlEncoding));
        const QString clean = QDir::cleanPath(decoded);
        if (QDir::isAbsolutePath(clean) && clean.startsWith(m_cacheRoot + '/'))
            path = clean;
    }

    return new ImageCacheResponse(hash, path, requestedSize,
                                  &m_mutex, &m_memCache, &m_lru, kMaxEntries,
                                  /*skipProcess=*/false);
}

// requestPixmap() sync fallback removed — all QML images use asynchronous: true.
