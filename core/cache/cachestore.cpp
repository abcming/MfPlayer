#include "common/version.h"
#include "common/constants.h"
#include "core/cache/cachestore.h"
#include "core/cache/dbworker.h"
#include "core/network/curlengine.h"
#include <QMutexLocker>
#include <QHash>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QCryptographicHash>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QBuffer>
#include <QImageReader>
#include <QPointer>
#include <QDebug>
#include <thread>

static QString hashUrl(const QString &url) {
    return QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex();
}

CacheStore::CacheStore(QObject *parent)
    : QObject(parent)
    , m_curl(std::make_unique<CurlEngine>(this))
{
    m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                 + "/mfplayer/images";

    // DBWorker lives on its own thread — no parent to avoid QObject thread affinity issues
    m_dbWorker = new DBWorker(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + "/mfplayer/cache.db");
}

CacheStore::~CacheStore() {
    m_stopFlag = true;

    // Stop DBWorker first — drains queue, quits thread
    delete m_dbWorker;

    // Join download worker threads
    for (auto &t : m_workerThreads)
        if (t.joinable()) t.join();

    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase("mfplayer_cache");
    }
}

void CacheStore::init() {
    QDir().mkpath(m_cacheDir);

    // Open main-thread read connection for fast in-memory cache fallback queries
    m_db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_cache");
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    m_db.setDatabaseName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                         + "/mfplayer/cache.db");
    QDir().mkpath(QFileInfo(m_db.databaseName()).absolutePath());
    if (!m_db.open())
        qWarning() << "CacheStore: failed to open read database:" << m_db.lastError().text();

    // Run DDL on main thread (fast: CREATE IF NOT EXISTS on existing tables <5ms)
    {
        QSqlQuery q(m_db);
        q.exec("CREATE TABLE IF NOT EXISTS items ("
               "parent_id TEXT, item_id TEXT, type TEXT, name TEXT, year INT, "
               "overview TEXT, image_url TEXT, image_path TEXT, "
               "parent_series_id TEXT, index_number INT, child_count INT, "
               "sort_order INT, fetched_at INTEGER, sort_name TEXT, "
               "PRIMARY KEY (parent_id, item_id))");
        q.exec("SELECT sort_name FROM items LIMIT 0");
        if (q.lastError().isValid())
            q.exec("ALTER TABLE items ADD COLUMN sort_name TEXT");
        q.exec("CREATE TABLE IF NOT EXISTS item_detail ("
               "item_id TEXT PRIMARY KEY, data TEXT, fetched_at INTEGER)");
        q.exec("CREATE TABLE IF NOT EXISTS seasons ("
               "series_id TEXT, season_id TEXT, name TEXT, year INT, "
               "image_url TEXT, image_path TEXT, index_number INT, "
               "fetched_at INTEGER, PRIMARY KEY (series_id, season_id))");
        q.exec("CREATE TABLE IF NOT EXISTS images ("
               "url_hash TEXT PRIMARY KEY, file_path TEXT, fetched_at INTEGER)");
        q.exec("CREATE TABLE IF NOT EXISTS episodes ("
               "series_id TEXT, season_id TEXT, data TEXT, fetched_at INTEGER, "
               "PRIMARY KEY (series_id, season_id))");
        q.exec("CREATE TABLE IF NOT EXISTS servers ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, server_url TEXT NOT NULL, "
               "username TEXT NOT NULL, password TEXT NOT NULL, "
               "token TEXT DEFAULT '', user_id TEXT DEFAULT '', "
               "is_active INTEGER DEFAULT 0, last_used TEXT)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_items_fetched ON items(fetched_at)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_detail_fetched ON item_detail(fetched_at)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_seasons_fetched ON seasons(fetched_at)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_images_fetched ON images(fetched_at)");
        q.exec("CREATE INDEX IF NOT EXISTS idx_episodes_fetched ON episodes(fetched_at)");
    }

    // Start DBWorker for writes and maintenance on its own thread
    connectDBWorker();
    m_dbWorker->start();

    // Defer image cache scan and expiry to next event loop tick so DBWorker
    // init can complete first (it opens its own connection asynchronously).
    QTimer::singleShot(0, this, [this]() {
        // Queue loadImageCache on DBWorker — result delivered via imageCacheLoaded signal
        QMetaObject::invokeMethod(m_dbWorker, "loadImageCache", Qt::QueuedConnection,
                                  Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
        // Queue expireCache on DBWorker (3-day sweep)
        QMetaObject::invokeMethod(m_dbWorker, "expireCache", Qt::QueuedConnection,
                                  Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
    });
}

void CacheStore::connectDBWorker() {
    // Image cache loaded from DB — merge into in-memory cache on main thread
    connect(m_dbWorker, &DBWorker::imageCacheLoaded, this, [this](QHash<QString, QString> entries) {
for (auto it = entries.begin(); it != entries.end(); ++it)
            m_imageCache.insert(it.key(), {it.value(), QUrl::fromLocalFile(it.value()).toString()});
    });

    // Expiry sweep completed — remove expired hashes from in-memory cache
    connect(m_dbWorker, &DBWorker::expired, this, [this](QStringList hashes) {
for (const auto &h : hashes)
            m_imageCache.remove(h);
    });
}

bool CacheStore::isFresh(qint64 timestamp) const {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    return (now - timestamp) < Constants::kCacheExpirySeconds;
}

QJsonArray CacheStore::getItems(const QString &parentId) {
    auto it = m_itemsCache.find(parentId);
    bool hasStaleMemory = false;
    QJsonArray staleData;
    if (it != m_itemsCache.end()) {
        auto ti = m_itemsCacheTime.find(parentId);
        if (ti != m_itemsCacheTime.end() && isFresh(ti.value()))
            return it.value();
        // Stale — keep it around for fallback if DB is empty
        hasStaleMemory = true;
        staleData = it.value();
        m_itemsCache.erase(it);
        if (ti != m_itemsCacheTime.end())
            m_itemsCacheTime.erase(ti);
    }

    QSqlQuery q(m_db);
    q.prepare("SELECT item_id, type, name, year, overview, image_url, image_path, "
              "parent_series_id, index_number, child_count, fetched_at, sort_name "
              "FROM items WHERE parent_id = ? ORDER BY sort_order ASC");
    q.addBindValue(parentId);
    q.exec();

    QJsonArray items;
    while (q.next()) {
        if (!isFresh(q.value(10).toLongLong()))
            continue;
        QJsonObject obj;
        obj["Id"] = q.value(0).toString();
        obj["Type"] = q.value(1).toString();
        obj["Name"] = q.value(2).toString();
        obj["ProductionYear"] = q.value(3).toInt();
        obj["Overview"] = q.value(4).toString();
        {
            QString tag = q.value(5).toString();
            if (!tag.isEmpty())
                obj["ImageTags"] = QJsonObject{{"Primary", tag}};
        }
        obj["SortName"] = q.value(11).toString();
        obj["ParentId"] = q.value(7).toString();
        obj["IndexNumber"] = q.value(8).toInt();
        obj["ChildCount"] = q.value(9).toInt();
        items.append(obj);
    }
    if (items.isEmpty() && hasStaleMemory) {
        // Fallback to stale memory data to prevent silent DB blanking
        return staleData;
    }
    m_itemsCache[parentId] = items;
    m_itemsCacheTime[parentId] = QDateTime::currentSecsSinceEpoch();
    return items;
}

void CacheStore::putItems(const QString &parentId, const QJsonArray &items) {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    // LRU eviction: keep at most kMaxItemCacheEntries parent folders in memory
    if (!m_itemsCache.contains(parentId)) {
        while (m_itemsCacheLru.size() >= kMaxItemCacheEntries) {
            QString oldest = m_itemsCacheLru.takeFirst();
            m_itemsCache.remove(oldest);
            m_itemsCacheTime.remove(oldest);
        }
        m_itemsCacheLru.append(parentId);
    } else {
        m_itemsCacheLru.move(m_itemsCacheLru.indexOf(parentId),
                             m_itemsCacheLru.size() - 1);
    }
    m_itemsCache[parentId] = items;
    m_itemsCacheTime[parentId] = now;
    // Offload SQL write to DBWorker thread — avoids blocking main thread
    uint32_t gen = m_writeGeneration;
    QMetaObject::invokeMethod(m_dbWorker, "putItems", Qt::QueuedConnection,
                              Q_ARG(QString, parentId),
                              Q_ARG(QJsonArray, items),
                              Q_ARG(uint32_t, gen),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

QJsonObject CacheStore::getItemDetail(const QString &itemId) {
    auto it = m_detailCache.find(itemId);
    if (it != m_detailCache.end())
        return it.value();
    QSqlQuery q(m_db);
    q.prepare("SELECT data, fetched_at FROM item_detail WHERE item_id = ?");
    q.addBindValue(itemId);
    q.exec();
    if (q.next() && isFresh(q.value(1).toLongLong())) {
        QJsonObject detail = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object();
        m_detailCache[itemId] = detail;
        return detail;
    }
    return {};
}

void CacheStore::putItemDetail(const QString &itemId, const QJsonObject &detail) {
    m_detailCache[itemId] = detail;
    auto data = QString::fromUtf8(QJsonDocument(detail).toJson(QJsonDocument::Compact));
    uint32_t gen = m_writeGeneration;
    QMetaObject::invokeMethod(m_dbWorker, "putItemDetail", Qt::QueuedConnection,
                              Q_ARG(QString, itemId),
                              Q_ARG(QString, data),
                              Q_ARG(uint32_t, gen),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

QJsonArray CacheStore::getSeasons(const QString &seriesId) {
    auto it = m_seasonsCache.find(seriesId);
    if (it != m_seasonsCache.end())
        return *it;

    QSqlQuery q(m_db);
    q.prepare("SELECT season_id, name, year, image_url, image_path, index_number, fetched_at "
              "FROM seasons WHERE series_id = ?");
    q.addBindValue(seriesId);
    q.exec();

    QJsonArray seasons;
    while (q.next()) {
        if (!isFresh(q.value(6).toLongLong()))
            continue;
        QJsonObject obj;
        obj["Id"] = q.value(0).toString();
        obj["Name"] = q.value(1).toString();
        obj["ProductionYear"] = q.value(2).toInt();
        {
            QString tag = q.value(3).toString();
            if (!tag.isEmpty())
                obj["ImageTags"] = QJsonObject{{"Primary", tag}};
        }
        obj["IndexNumber"] = q.value(5).toInt();
        seasons.append(obj);
    }
    if (!seasons.isEmpty())
        m_seasonsCache[seriesId] = seasons;
    return seasons;
}

void CacheStore::putSeasons(const QString &seriesId, const QJsonArray &seasons) {
    m_seasonsCache[seriesId] = seasons;
    uint32_t gen = m_writeGeneration;
    QMetaObject::invokeMethod(m_dbWorker, "putSeasons", Qt::QueuedConnection,
                              Q_ARG(QString, seriesId),
                              Q_ARG(QJsonArray, seasons),
                              Q_ARG(uint32_t, gen),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

QJsonArray CacheStore::getEpisodes(const QString &seriesId, const QString &seasonId) {
    QString key = seriesId + QChar(0) + seasonId;
    auto it = m_episodesCache.find(key);
    if (it != m_episodesCache.end())
        return *it;

    QSqlQuery q(m_db);
    q.prepare("SELECT data, fetched_at FROM episodes WHERE series_id = ? AND season_id = ?");
    q.addBindValue(seriesId);
    q.addBindValue(seasonId);
    q.exec();
    if (q.next() && isFresh(q.value(1).toLongLong())) {
        auto arr = QJsonDocument::fromJson(q.value(0).toString().toUtf8()).array();
        m_episodesCache[key] = arr;
        return arr;
    }
    return {};
}

void CacheStore::putEpisodes(const QString &seriesId, const QString &seasonId,
                              const QJsonArray &episodes) {
    m_episodesCache[seriesId + QChar(0) + seasonId] = episodes;
    auto data = QString::fromUtf8(QJsonDocument(episodes).toJson(QJsonDocument::Compact));
    uint32_t gen = m_writeGeneration;
    QMetaObject::invokeMethod(m_dbWorker, "putEpisodes", Qt::QueuedConnection,
                              Q_ARG(QString, seriesId),
                              Q_ARG(QString, seasonId),
                              Q_ARG(QString, data),
                              Q_ARG(uint32_t, gen),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

QString CacheStore::getImagePath(const QString &url) {
    QSqlQuery q(m_db);
    q.prepare("SELECT file_path, fetched_at FROM images WHERE url_hash = ?");
    q.addBindValue(hashUrl(url));
    q.exec();
    if (q.next() && isFresh(q.value(1).toLongLong())
        && QFile::exists(q.value(0).toString())) {
        return q.value(0).toString();
    }
    return {};
}

void CacheStore::putImagePath(const QString &url, const QString &localPath) {
    const QString h = hashUrl(url);
    {
m_imageCache[h] = ImageCacheEntry{localPath, QUrl::fromLocalFile(localPath).toString()};
    }
    // SQL write goes to DBWorker — no main-thread blocking
    QMetaObject::invokeMethod(m_dbWorker, "putImagePath", Qt::QueuedConnection,
                              Q_ARG(QString, h),
                              Q_ARG(QString, localPath),
                              Q_ARG(uint32_t, m_writeGeneration),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

QString CacheStore::imageSavePath(const QString &url) const {
    return m_cacheDir + "/" + hashUrl(url);
}

QString CacheStore::resolveImagePath(const QString &urlHash) const {
    // Trust the in-memory cache — files were verified at loadImageCache() time.
    // All accesses are on the main thread, no mutex needed.
    auto it = m_imageCache.constFind(urlHash);
    if (it != m_imageCache.constEnd())
        return it->filePath;
    return {};
}

QString CacheStore::cachedImageUrl(const QString &url) {
    QString hashKey = hashUrl(url);

    // Fast path: trust the in-memory cache — skip stat syscalls (QFile::exists +
    // QFileInfo::size) for entries already verified at startup by loadImageCache().
    // Worst case if the file was deleted externally: Image shows nothing, next
    // scroll cycle re-downloads. acceptably rare (only affects expireCache races).
    {
auto it = m_imageCache.constFind(hashKey);
        if (it != m_imageCache.constEnd())
            return it->fileUrl;
    }

    // loadImageCache() populated m_imageCache at startup. If the
    // image is not in memory, it genuinely doesn't exist on disk.
    // No DB fallback — keeps the scroll hot path SQLite-free.
    return {};
}

void CacheStore::clearContentCache() {
    ++m_writeGeneration;  // cancel pending writes queued to DBWorker
    m_itemsCache.clear();
    m_itemsCacheTime.clear();
    m_itemsCacheLru.clear();
    m_detailCache.clear();
    m_seasonsCache.clear();
    m_episodesCache.clear();

    // SQL DELETE on DBWorker thread — no main-thread blocking
    QMetaObject::invokeMethod(m_dbWorker, "clearContentCache", Qt::QueuedConnection,
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

void CacheStore::clearImageCache() {
    ++m_writeGeneration;  // cancel pending writes queued to DBWorker
    {
m_imageCache.clear();
        m_pendingDownloads.clear();
    }

    // SQL DELETE + recursive dir removal on DBWorker thread — no main-thread blocking
    QMetaObject::invokeMethod(m_dbWorker, "clearImageCache", Qt::QueuedConnection,
                              Q_ARG(QString, m_cacheDir),
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

void CacheStore::updateItemFieldInCache(const QString &itemId, const QString &fieldName, const QVariant &value) {
    for (auto it = m_itemsCache.begin(); it != m_itemsCache.end(); ++it) {
        QJsonArray &items = it.value();
        for (int i = 0; i < items.size(); ++i) {
            QJsonObject obj = items[i].toObject();
            if (obj["Id"].toString() != itemId) continue;
            QJsonObject ud = obj["UserData"].toObject();
            if (fieldName == "isFavorite")
                ud["IsFavorite"] = value.toBool();
            else if (fieldName == "played") {
                ud["Played"] = value.toBool();
                if (value.toBool()) {
                    ud["PlaybackPositionTicks"] = 0;
                    ud["PlayedPercentage"] = 0;
                }
            }
            obj["UserData"] = ud;
            items[i] = obj;
            return;  // item updated — stop scanning (was missing outer break, bug)
        }
    }
}

void CacheStore::clearAll() {
    clearContentCache();
    clearImageCache();
}

void CacheStore::expireCache() {
    // Offload to DBWorker — runs on its own thread, no main-thread blocking.
    // DBWorker serializes all access so no need for duplicate-prevention guard.
    QMetaObject::invokeMethod(m_dbWorker, "expireCache", Qt::QueuedConnection,
                              Q_ARG(QPointer<QObject>, QPointer<QObject>(this)));
}

void CacheStore::fetchImage(const QString &url) {
    if (url.isEmpty() || !url.startsWith("http"))
        return;

    QString hash = hashUrl(url);
    // Fast memory lookup + duplicate download prevention.
    // Trust the in-memory cache — files were verified at loadImageCache() time.
    // If the file was deleted externally (extremely rare), the Image simply won't
    // render and the next scroll cycle will re-download.
    {
auto it = m_imageCache.find(hash);
        if (it != m_imageCache.end()) {
            emit imageReady(url, it->fileUrl);
            return;
        }
        // Prevent duplicate concurrent downloads of the same URL
        if (m_pendingDownloads.contains(hash)) return;
        // Cooldown: don't retry a recently-failed URL (CDN rate-limit protection)
        auto fi = m_failedUrls.constFind(hash);
        if (fi != m_failedUrls.constEnd()) {
            qint64 now = QDateTime::currentSecsSinceEpoch();
            if (now - fi.value() < Constants::kImageRetryCooldownMs / 1000)
                return;
            m_failedUrls.erase(fi);
        }
        m_pendingDownloads.insert(hash);
    }

    if (m_activeDownloads >= kMaxActiveDownloads) {
        m_downloadQueue.append({url, 1});
        return;
    }
    m_activeDownloads++;
    doFetchImage(url, 1);
}

void CacheStore::setSkipSslVerify(bool skip) {
    m_curl->setSkipSslVerify(skip);
}

void CacheStore::processDownloadQueue() {
    if (m_downloadQueue.isEmpty()) return;
    auto next = m_downloadQueue.takeFirst();
    m_activeDownloads++;
    doFetchImage(next.first, next.second);
}

void CacheStore::doFetchImage(const QString &url, int retries) {
    CurlEngine::Headers headers;
    headers.push_back({"User-Agent", MFPLAYER_USER_AGENT});

    QPointer<CacheStore> safeThis(this);
    m_curl->get(url, headers, [safeThis, url, retries](const CurlResponse &r) {
        if (!safeThis) return;

        auto done = [safeThis, url]() {
            if (!safeThis) return;
            safeThis->m_pendingDownloads.remove(hashUrl(url));
            safeThis->m_activeDownloads--;
            safeThis->processDownloadQueue();
        };

        if (r.curlResult != CURLE_OK) {
            qWarning() << "CacheStore: image download failed:" << curl_easy_strerror(r.curlResult);
            if (retries > 0) {
                QTimer::singleShot(Constants::kImageRetryDelayMs, safeThis, [safeThis, url, retries]() {
                    if (safeThis) safeThis->doFetchImage(url, retries - 1);
                });
            } else {
                safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
                emit safeThis->imageReady(url, QString());
                done();
            }
            return;
        }

        if (r.httpStatus >= 400) {
            safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
            emit safeThis->imageReady(url, QString());
            done();
            return;
        }

        QByteArray data = r.body;
        if (data.isEmpty()) {
            qWarning() << "CacheStore: image download empty:" << url;
            if (retries > 0) {
                QTimer::singleShot(Constants::kImageRetryDelayMs, safeThis, [safeThis, url, retries]() {
                    if (safeThis) safeThis->doFetchImage(url, retries - 1);
                });
            } else {
                safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
                emit safeThis->imageReady(url, QString());
                done();
            }
            return;
        }

        // Re-fetch as JPEG if Emby returns a format Qt can't decode (e.g. WebP)
        QByteArray ct = r.contentType;
        if (ct.contains("image/webp") || ct.contains("image/avif") || ct.contains("image/heif")) {
            if (retries > 0) {
                QString newUrl = url.contains('?') ? url + "&format=jpg" : url + "?format=jpg";
                QTimer::singleShot(Constants::kFormatRetryDelayMs, safeThis, [safeThis, newUrl, retries]() {
                    if (safeThis) safeThis->doFetchImage(newUrl, retries - 1);
                });
                return;
            }
            qWarning() << "CacheStore: unsupported image format for" << url;
            safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
            emit safeThis->imageReady(url, QString());
            done();
            return;
        }

        // Validate + write on a worker thread to avoid blocking the main thread
        // with QImageReader::canRead() (~2ms) and QFile::write (~1ms).
        QString savePath = safeThis->imageSavePath(url);
        std::thread worker([safeThis, url, data = std::move(data), savePath, retries, done]() mutable {
            // Validate the downloaded data is actually a decodable image.
            // Guards against HTML error pages and truncated downloads being cached.
            {
                QBuffer buf(&data);
                buf.open(QIODevice::ReadOnly);
                QImageReader reader(&buf);
                if (!reader.canRead()) {
                    qWarning() << "CacheStore: downloaded data is not a valid image:" << url;
                    QMetaObject::invokeMethod(safeThis, [safeThis, url, retries, done]() {
                        if (!safeThis) { done(); return; }
                        if (retries > 0) {
                            QString newUrl = url.contains('?') ? url + "&format=jpg" : url + "?format=jpg";
                            QTimer::singleShot(Constants::kFormatRetryDelayMs, safeThis, [safeThis, newUrl, retries]() {
                                if (safeThis) safeThis->doFetchImage(newUrl, retries - 1);
                            });
                        } else {
                            safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
                            emit safeThis->imageReady(url, QString());
                        }
                        done();
                    });
                    return;
                }
            }

            // Write to temp file first, then atomically rename.
            // This prevents truncating a file that another concurrent download
            // or Qt's async image decoder is currently reading.
            QString tmpPath = savePath + ".tmp";
            bool writeOk = false;
            {
                QFile f(tmpPath);
                if (f.open(QIODevice::WriteOnly)) {
                    f.write(data);
                    f.close();
                    QFile::remove(savePath);
                    writeOk = QFile::rename(tmpPath, savePath);
                }
            }
            data.clear();  // free downloaded bytes ASAP

            QMetaObject::invokeMethod(safeThis, [safeThis, url, savePath, writeOk, done]() {
                if (!safeThis) { done(); return; }
                if (writeOk) {
                    safeThis->putImagePath(url, savePath);
                    emit safeThis->imageReady(url, QUrl::fromLocalFile(savePath).toString());
                } else {
                    qWarning() << "CacheStore: failed to write image:" << savePath;
                    safeThis->m_failedUrls[hashUrl(url)] = QDateTime::currentSecsSinceEpoch();
                    emit safeThis->imageReady(url, QString());
                }
                done();
            });
        });
        // Track thread for clean shutdown. Worker runs ~2ms (image validation + write).
        // Worker threads are only touched from the main thread, so no mutex needed.
        safeThis->m_workerThreads.push_back(std::move(worker));
    }, 15);
}