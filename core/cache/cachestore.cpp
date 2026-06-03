#include "common/version.h"
#include "common/constants.h"
#include "core/cache/cachestore.h"
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
}

CacheStore::~CacheStore() {
    m_stopFlag = true;
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        for (auto &t : m_workerThreads)
            if (t.joinable()) t.join();
    }
    if (m_clearThread.joinable())
        m_clearThread.join();
    if (m_expireThread.joinable())
        m_expireThread.join();
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase("mfplayer_cache");
    }
}

void CacheStore::init() {
    QDir().mkpath(m_cacheDir);

    m_db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_cache");
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    m_db.setDatabaseName(QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                         + "/mfplayer/cache.db");
    QDir().mkpath(QFileInfo(m_db.databaseName()).absolutePath());

    if (!m_db.open()) {
        qWarning() << "CacheStore: failed to open database:" << m_db.lastError().text();
        return;
    }
    initTables();
}

void CacheStore::initTables() {
    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS items ("
           "parent_id TEXT, item_id TEXT, type TEXT, name TEXT, year INT, "
           "overview TEXT, image_url TEXT, image_path TEXT, "
           "parent_series_id TEXT, index_number INT, child_count INT, "
           "sort_order INT, fetched_at INTEGER, sort_name TEXT, "
           "PRIMARY KEY (parent_id, item_id))");
    // Migration: add sort_name column to existing tables
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

    // Indexes for expireCache() performance
    q.exec("CREATE INDEX IF NOT EXISTS idx_items_fetched ON items(fetched_at)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_detail_fetched ON item_detail(fetched_at)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_seasons_fetched ON seasons(fetched_at)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_images_fetched ON images(fetched_at)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_episodes_fetched ON episodes(fetched_at)");

    // Defer image cache scan so the UI appears first.
    // resolveImagePath() falls back to SQL while m_imageCache is still empty — safe, just slower.
    QTimer::singleShot(0, this, &CacheStore::loadImageCache);

    // 3-day expiry — deferred to avoid blocking startup (SQLite DELETEs can
    // take 10-50ms on large caches). Runs on the next event loop tick.
    QTimer::singleShot(0, this, &CacheStore::expireCache);
}

void CacheStore::loadImageCache() {
    QSqlQuery q(m_db);
    q.exec("SELECT url_hash, file_path FROM images");
    QHash<QString, QString> validEntries;
    QStringList stale;
    while (q.next()) {
        QString hash = q.value(0).toString();
        QString path = q.value(1).toString();
        if (QFile::exists(path))
            validEntries.insert(hash, path);
        else
            stale.append(hash);
    }
    {
        QMutexLocker lock(&m_imageCacheMutex);
        m_imageCache = std::move(validEntries);
    }
    if (!stale.isEmpty()) {
        m_db.transaction();
        QSqlQuery del(m_db);
        del.prepare("DELETE FROM images WHERE url_hash = ?");
        for (const auto &h : stale) {
            del.addBindValue(h);
            del.exec();
            del.finish();
        }
        m_db.commit();
    }
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
    m_itemsCache[parentId] = items;
    m_itemsCacheTime[parentId] = now;
    // SQL write deferred to event loop idle
    uint32_t gen = m_writeGeneration;
    QTimer::singleShot(0, this, [this, parentId, items, now, gen]() {
        if (gen != m_writeGeneration) return;
        QSqlQuery q(m_db);
        m_db.transaction();
        q.prepare("INSERT OR REPLACE INTO items "
                  "(parent_id, item_id, type, name, year, overview, image_url, image_path, "
                  "parent_series_id, index_number, child_count, sort_order, fetched_at, sort_name) "
                  "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?)");
        for (int i = 0; i < items.size(); ++i) {
            auto obj = items[i].toObject();
            q.addBindValue(parentId);
            q.addBindValue(obj["Id"].toString());
            q.addBindValue(obj["Type"].toString());
            q.addBindValue(obj["Name"].toString());
            q.addBindValue(obj["ProductionYear"].toInt());
            q.addBindValue(obj["Overview"].toString());
            q.addBindValue(obj["ImageTags"].toObject()["Primary"].toString());
            q.addBindValue(QString());
            q.addBindValue(obj["ParentId"].toString());
            q.addBindValue(obj["IndexNumber"].toInt());
            q.addBindValue(obj["ChildCount"].toInt());
            q.addBindValue(i);
            q.addBindValue(now);
            q.addBindValue(obj["SortName"].toString());
            q.exec();
        }
        m_db.commit();
    });
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
    QTimer::singleShot(0, this, [this, itemId, data, gen]() {
        if (gen != m_writeGeneration) return;
        QSqlQuery q(m_db);
        q.prepare("INSERT OR REPLACE INTO item_detail (item_id, data, fetched_at) VALUES (?,?,?)");
        q.addBindValue(itemId);
        q.addBindValue(data);
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        q.exec();
    });
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
    QTimer::singleShot(0, this, [this, seriesId, seasons, gen]() {
        if (gen != m_writeGeneration) return;
        QSqlQuery q(m_db);
        m_db.transaction();
        q.prepare("INSERT OR REPLACE INTO seasons "
                  "(series_id, season_id, name, year, image_url, image_path, index_number, fetched_at) "
                  "VALUES (?,?,?,?,?,?,?,?)");
        qint64 now = QDateTime::currentSecsSinceEpoch();
        for (const auto &val : seasons) {
            auto obj = val.toObject();
            q.addBindValue(seriesId);
            q.addBindValue(obj["Id"].toString());
            q.addBindValue(obj["Name"].toString());
            q.addBindValue(obj["ProductionYear"].toInt());
            q.addBindValue(obj["ImageTags"].toObject()["Primary"].toString());
            q.addBindValue(QString());
            q.addBindValue(obj["IndexNumber"].toInt());
            q.addBindValue(now);
            q.exec();
        }
        m_db.commit();
    });
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
    QTimer::singleShot(0, this, [this, seriesId, seasonId, data, gen]() {
        if (gen != m_writeGeneration) return;
        QSqlQuery q(m_db);
        q.prepare("INSERT OR REPLACE INTO episodes (series_id, season_id, data, fetched_at) "
                  "VALUES (?,?,?,?)");
        q.addBindValue(seriesId);
        q.addBindValue(seasonId);
        q.addBindValue(data);
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        q.exec();
    });
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
        QMutexLocker lock(&m_imageCacheMutex);
        m_imageCache[h] = localPath;
    }
    uint32_t gen = m_writeGeneration;
    QTimer::singleShot(0, this, [this, h, localPath, gen]() {
        if (gen != m_writeGeneration) return;
        QSqlQuery q(m_db);
        q.prepare("INSERT OR REPLACE INTO images (url_hash, file_path, fetched_at) VALUES (?,?,?)");
        q.addBindValue(h);
        q.addBindValue(localPath);
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        q.exec();
    });
}

QString CacheStore::imageSavePath(const QString &url) const {
    return m_cacheDir + "/" + hashUrl(url);
}

QString CacheStore::resolveImagePath(const QString &urlHash) const {
    QMutexLocker lock(&m_imageCacheMutex);
    auto it = m_imageCache.constFind(urlHash);
    if (it != m_imageCache.constEnd()) {
        const QString &path = it.value();
        if (QFile::exists(path) && QFileInfo(path).size() > 0)
            return path;
    }
    return {};
}

QString CacheStore::cachedImageUrl(const QString &url) {
    QString hashKey = hashUrl(url);

    // Fast path: trust the in-memory cache — skip stat syscalls (QFile::exists +
    // QFileInfo::size) for entries already verified at startup by loadImageCache().
    // Worst case if the file was deleted externally: Image shows nothing, next
    // scroll cycle re-downloads. acceptably rare (only affects expireCache races).
    {
        QMutexLocker lock(&m_imageCacheMutex);
        auto it = m_imageCache.constFind(hashKey);
        if (it != m_imageCache.constEnd())
            return QUrl::fromLocalFile(it.value()).toString();
    }

    // Slow path: DB fallback for entries not yet loaded into memory
    QString path = getImagePath(url);
    if (!path.isEmpty()) {
        if (!QFile::exists(path)) {
            QSqlQuery q(m_db);
            q.prepare("DELETE FROM images WHERE url_hash = ?");
            q.addBindValue(hashKey);
            q.exec();
            return {};
        }
        QMutexLocker lock(&m_imageCacheMutex);
        m_imageCache[hashKey] = path;
        return QUrl::fromLocalFile(path).toString();
    }
    return {};
}

void CacheStore::clearContentCache() {
    ++m_writeGeneration;  // cancel pending deferred writes
    m_itemsCache.clear();
    m_itemsCacheTime.clear();
    m_detailCache.clear();
    m_seasonsCache.clear();
    m_episodesCache.clear();

    // SQL DELETE on background thread with its own connection
    if (m_clearThread.joinable())
        m_clearThread.join();
    QString dbName = m_db.databaseName();
    m_clearThread = std::thread([dbName]() {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_clear");
            db.setDatabaseName(dbName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            if (!db.open()) {
                QSqlDatabase::removeDatabase("mfplayer_clear");
                return;
            }
            db.transaction();
            QSqlQuery q(db);
            q.exec("DELETE FROM items");
            q.exec("DELETE FROM item_detail");
            q.exec("DELETE FROM seasons");
            q.exec("DELETE FROM episodes");
            db.commit();
        }
        QSqlDatabase::removeDatabase("mfplayer_clear");
    });
}

void CacheStore::clearImageCache() {
    ++m_writeGeneration;  // cancel pending putImagePath deferred writes
    {
        QMutexLocker lock(&m_imageCacheMutex);
        m_imageCache.clear();
        m_pendingDownloads.clear();
    }

    // SQL DELETE + recursive dir removal on background thread
    if (m_clearThread.joinable())
        m_clearThread.join();
    QString dbName = m_db.databaseName();
    QString cacheDir = m_cacheDir;
    m_clearThread = std::thread([dbName, cacheDir]() {
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_clear_img");
            db.setDatabaseName(dbName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            if (db.open()) {
                QSqlQuery q(db);
                q.exec("DELETE FROM images");
            }
            db.close();
        }
        QSqlDatabase::removeDatabase("mfplayer_clear_img");

        QDir dir(cacheDir);
        dir.removeRecursively();
        dir.mkpath(cacheDir);
    });
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
            break;
        }
    }
}

void CacheStore::clearAll() {
    clearContentCache();
    clearImageCache();
}

void CacheStore::expireCache() {
    // Run on a background thread with its own SQLite connection to avoid
    // blocking the GUI thread during the startup sweep.
    if (m_expireThread.joinable()) {
        // Previous sweep still running — skip, it covers the same work.
        return;
    }
    QString dbName = m_db.databaseName();
    QPointer<CacheStore> safeThis(this);
    m_expireThread = std::thread([dbName, safeThis]() {
        QStringList expiredHashes;
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_expire");
            db.setDatabaseName(dbName);
            db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
            if (!db.open()) {
                QSqlDatabase::removeDatabase("mfplayer_expire");
                return;
            }

            qint64 cutoff = QDateTime::currentSecsSinceEpoch() - Constants::kCacheExpirySeconds;

            {
                QSqlQuery q(db);

                // Collect expired image hashes and delete files on disk
                q.prepare("SELECT url_hash, file_path FROM images WHERE fetched_at < ?");
                q.addBindValue(cutoff);
                q.exec();
                while (q.next()) {
                    expiredHashes.append(q.value(0).toString());
                    QString path = q.value(1).toString();
                    if (!path.isEmpty()) QFile::remove(path);
                }

                db.transaction();
                q.prepare("DELETE FROM items WHERE fetched_at < ?");
                q.addBindValue(cutoff); q.exec();
                q.prepare("DELETE FROM item_detail WHERE fetched_at < ?");
                q.addBindValue(cutoff); q.exec();
                q.prepare("DELETE FROM seasons WHERE fetched_at < ?");
                q.addBindValue(cutoff); q.exec();
                q.prepare("DELETE FROM images WHERE fetched_at < ?");
                q.addBindValue(cutoff); q.exec();
                q.prepare("DELETE FROM episodes WHERE fetched_at < ?");
                q.addBindValue(cutoff); q.exec();
                db.commit();
            }  // QSqlQuery destroyed
        }  // QSqlDatabase destroyed (calls close() in destructor)
        QSqlDatabase::removeDatabase("mfplayer_expire");

        // Remove expired entries from the in-memory cache on the main thread
        if (!expiredHashes.isEmpty()) {
            QMetaObject::invokeMethod(safeThis, [safeThis, expiredHashes]() {
                if (!safeThis) return;
                QMutexLocker lock(&safeThis->m_imageCacheMutex);
                for (const auto &h : expiredHashes)
                    safeThis->m_imageCache.remove(h);
            });
        }
    });
}

void CacheStore::fetchImage(const QString &url) {
    if (url.isEmpty() || !url.startsWith("http"))
        return;

    QString hash = hashUrl(url);
    // Fast memory lookup + duplicate download prevention
    {
        QMutexLocker lock(&m_imageCacheMutex);
        auto it = m_imageCache.find(hash);
        if (it != m_imageCache.end()) {
            QString path = it.value();
            if (QFile::exists(path)) {
                emit imageReady(url, QUrl::fromLocalFile(path).toString());
                return;
            }
            // File disappeared — remove from cache and re-download
            m_imageCache.erase(it);
        }
        // Prevent duplicate concurrent downloads of the same URL
        if (m_pendingDownloads.contains(hash)) return;
        m_pendingDownloads.insert(hash);
    }

    if (m_activeDownloads >= kMaxActiveDownloads) {
        m_downloadQueue.append({url, 1});
        return;
    }
    m_activeDownloads++;
    doFetchImage(url, 1);
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
            {
                QMutexLocker lock(&safeThis->m_imageCacheMutex);
                safeThis->m_pendingDownloads.remove(hashUrl(url));
            }
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
                emit safeThis->imageReady(url, QString());
                done();
            }
            return;
        }

        if (r.httpStatus >= 400) {
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
                    emit safeThis->imageReady(url, QString());
                }
                done();
            });
        });
        // Track thread for clean shutdown. Worker runs ~2ms (image validation + write),
        // so accumulation is negligible. Destructor joins all.
        {
            std::lock_guard<std::mutex> wlock(safeThis->m_workerMutex);
            safeThis->m_workerThreads.push_back(std::move(worker));
        }
    }, 15);
}