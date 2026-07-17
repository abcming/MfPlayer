#include "dbworker.h"
#include "common/constants.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QSqlError>
#include <QJsonDocument>
#include <QDateTime>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QDebug>

static QString hashUrl(const QString &url) {
    return QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Md5).toHex();
}

DBWorker::DBWorker(const QString &dbPath, QObject *parent)
    : QObject(nullptr)  // no parent — will be moved to m_thread
    , m_dbPath(dbPath)
{
    // Move to worker thread. Event loop starts when m_thread.start() is called.
    moveToThread(&m_thread);
}

DBWorker::~DBWorker() {
    stop();
}

void DBWorker::start() {
    m_thread.start();
    // Queue init on the worker thread
    QMetaObject::invokeMethod(this, "init", Qt::QueuedConnection);
}

void DBWorker::stop() {
    m_stopFlag = true;
    m_thread.quit();
    if (!m_thread.wait(2000)) {
        m_thread.terminate();
        m_thread.wait();
    }
    // Close DB on whatever thread we're on (main thread during shutdown).
    // At this point the worker thread has stopped, so no concurrent access.
    if (m_db.isOpen()) {
        m_db.close();
        QSqlDatabase::removeDatabase("mfplayer_db_worker");
    }
}

// ── Initialization (runs on worker thread) ──────────────────────────

void DBWorker::init() {
    // Ensure parent directory exists
    QDir().mkpath(QFileInfo(m_dbPath).absolutePath());

    m_db = QSqlDatabase::addDatabase("QSQLITE", "mfplayer_db_worker");
    m_db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        qWarning() << "DBWorker: failed to open database:" << m_db.lastError().text();
        emit dbError("Failed to open database: " + m_db.lastError().text());
        return;
    }
    // DDL (CREATE TABLE IF NOT EXISTS) is handled by CacheStore::init() on the
    // main thread, which runs before DBWorker starts.  Just ensure WAL mode for
    // this connection.
    {
        QSqlQuery pragma(m_db);
        pragma.exec("PRAGMA journal_mode=WAL");
    }
    emit ready();
}

bool DBWorker::isFresh(qint64 timestamp) const {
    qint64 now = QDateTime::currentSecsSinceEpoch();
    return (now - timestamp) < Constants::kCacheExpirySeconds;
}

// ── Content cache writes ────────────────────────────────────────────

void DBWorker::putItems(const QString &cacheKey, const QString &data,
                         uint32_t generation, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO items_json (cache_key, data, fetched_at) VALUES (?,?,?)");
    q.addBindValue(cacheKey);
    q.addBindValue(data);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    Q_UNUSED(guard);
    Q_UNUSED(generation);
    emit itemsWritten(cacheKey);
}

void DBWorker::putItemDetail(const QString &itemId, const QString &data,
                              uint32_t generation, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO item_detail (item_id, data, fetched_at) VALUES (?,?,?)");
    q.addBindValue(itemId);
    q.addBindValue(data);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    Q_UNUSED(guard);
    Q_UNUSED(generation);
    emit itemDetailWritten(itemId);
}

void DBWorker::putSeasons(const QString &seriesId, const QJsonArray &seasons,
                           uint32_t generation, QPointer<QObject> guard) {
    if (m_stopFlag) return;
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
    Q_UNUSED(guard);
    Q_UNUSED(generation);
    emit seasonsWritten(seriesId);
}

void DBWorker::putEpisodes(const QString &seriesId, const QString &seasonId,
                            const QString &data, uint32_t generation,
                            QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO episodes (series_id, season_id, data, fetched_at) "
              "VALUES (?,?,?,?)");
    q.addBindValue(seriesId);
    q.addBindValue(seasonId);
    q.addBindValue(data);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    Q_UNUSED(guard);
    Q_UNUSED(generation);
    emit episodesWritten(seriesId, seasonId);
}

void DBWorker::putImagePath(const QString &urlHash, const QString &localPath,
                             uint32_t generation, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO images (url_hash, file_path, fetched_at) VALUES (?,?,?)");
    q.addBindValue(urlHash);
    q.addBindValue(localPath);
    q.addBindValue(QDateTime::currentSecsSinceEpoch());
    q.exec();
    Q_UNUSED(guard);
    Q_UNUSED(generation);
    emit imagePathWritten(urlHash);
}

// ── Image cache maintenance ──────────────────────────────────────────

void DBWorker::loadImageCache(QPointer<QObject> guard) {
    if (m_stopFlag) return;
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
    if (!stale.isEmpty()) {
        m_db.transaction();
        QSqlQuery del(m_db);
        const int batchSize = 500;
        for (int i = 0; i < stale.size(); i += batchSize) {
            int chunk = batchSize;
            if (i + chunk > stale.size())
                chunk = stale.size() - i;
            QStringList placeholders;
            placeholders.reserve(chunk);
            for (int j = 0; j < chunk; ++j)
                placeholders << "?";
            del.prepare("DELETE FROM images WHERE url_hash IN ("
                        + placeholders.join(",") + ")");
            for (int j = 0; j < chunk; ++j)
                del.addBindValue(stale[i + j]);
            del.exec();
        }
        m_db.commit();
    }
    if (guard)
        emit imageCacheLoaded(validEntries);
}

void DBWorker::getImagePath(const QString &url, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("SELECT file_path, fetched_at FROM images WHERE url_hash = ?");
    q.addBindValue(hashUrl(url));
    q.exec();
    QString path;
    if (q.next() && isFresh(q.value(1).toLongLong())
        && QFile::exists(q.value(0).toString())) {
        path = q.value(0).toString();
    }
    if (guard)
        emit imagePathReady(url, path);
}

void DBWorker::removeStaleImages(const QStringList &urlHashes, QPointer<QObject> guard) {
    if (m_stopFlag || urlHashes.isEmpty()) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    const int batchSize = 500;
    for (int i = 0; i < urlHashes.size(); i += batchSize) {
        int chunk = batchSize;
        if (i + chunk > urlHashes.size())
            chunk = urlHashes.size() - i;
        QStringList placeholders;
        placeholders.reserve(chunk);
        for (int j = 0; j < chunk; ++j)
            placeholders << "?";
        q.prepare("DELETE FROM images WHERE url_hash IN ("
                  + placeholders.join(",") + ")");
        for (int j = 0; j < chunk; ++j)
            q.addBindValue(urlHashes[i + j]);
        q.exec();
    }
    m_db.commit();
    if (guard)
        emit staleImagesRemoved(urlHashes);
}

// ── Cache lifecycle ──────────────────────────────────────────────────

void DBWorker::expireCache(QPointer<QObject> guard) {
    if (m_stopFlag) return;
    qint64 cutoff = QDateTime::currentSecsSinceEpoch() - Constants::kCacheExpirySeconds;
    QStringList expiredHashes;

    {
        QSqlQuery q(m_db);
        q.prepare("SELECT url_hash, file_path FROM images WHERE fetched_at < ?");
        q.addBindValue(cutoff);
        q.exec();
        while (q.next()) {
            expiredHashes.append(q.value(0).toString());
            QString path = q.value(1).toString();
            if (!path.isEmpty()) QFile::remove(path);
        }
    }

    m_db.transaction();
    {
        QSqlQuery q(m_db);
        q.prepare("DELETE FROM items WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
        q.prepare("DELETE FROM items_json WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
        q.prepare("DELETE FROM item_detail WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
        q.prepare("DELETE FROM seasons WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
        q.prepare("DELETE FROM images WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
        q.prepare("DELETE FROM episodes WHERE fetched_at < ?");
        q.addBindValue(cutoff); q.exec();
    }
    m_db.commit();

    if (guard)
        emit expired(expiredHashes);
}

void DBWorker::clearContentCache(QPointer<QObject> guard) {
    if (m_stopFlag) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.exec("DELETE FROM items");
    q.exec("DELETE FROM items_json");
    q.exec("DELETE FROM item_detail");
    q.exec("DELETE FROM seasons");
    q.exec("DELETE FROM episodes");
    m_db.commit();
    if (guard)
        emit contentCleared();
}

void DBWorker::clearImageCache(const QString &cacheDir, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    {
        QSqlQuery q(m_db);
        q.exec("DELETE FROM images");
    }

    QDir dir(cacheDir);
    dir.removeRecursively();
    dir.mkpath(cacheDir);

    if (guard)
        emit imagesCleared();
}

// ── CredentialStore operations ───────────────────────────────────────

void DBWorker::addServer(const QString &serverUrl, const QString &username,
                          const QString &token, const QString &userId,
                          QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM servers WHERE server_url = ?");
    q.addBindValue(serverUrl);
    q.exec();

    int serverId = -1;
    if (q.next()) {
        serverId = q.value(0).toInt();
        q.prepare("UPDATE servers SET username=?, token=?, user_id=?, last_used=datetime('now') WHERE id=?");
        q.addBindValue(username);
        q.addBindValue(token);
        q.addBindValue(userId);
        q.addBindValue(serverId);
        q.exec();
    } else {
        q.prepare("UPDATE servers SET is_active = 0");
        q.exec();
        q.prepare("INSERT INTO servers (server_url, username, password, token, user_id, is_active, last_used) "
                  "VALUES (?,?,?,?,?,1,datetime('now'))");
        q.addBindValue(serverUrl);
        q.addBindValue(username);
        q.addBindValue(QString());  // password — no longer stored
        q.addBindValue(token);
        q.addBindValue(userId);
        q.exec();
        serverId = q.lastInsertId().toInt();
    }
    if (guard)
        emit serverAdded(serverId);
}

void DBWorker::getServers(QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QJsonArray servers;
    QSqlQuery q(m_db);
    q.exec("SELECT id, server_url, username, token, user_id, is_active, last_used FROM servers ORDER BY last_used DESC");
    while (q.next()) {
        QJsonObject obj;
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["token"] = q.value(3).toString();
        obj["userId"] = q.value(4).toString();
        obj["isActive"] = q.value(5).toBool();
        obj["lastUsed"] = q.value(6).toString();
        servers.append(obj);
    }
    if (guard)
        emit serversFetched(servers);
}

void DBWorker::getActiveServer(QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QJsonObject obj;
    QSqlQuery q(m_db);
    q.exec("SELECT id, server_url, username, token, user_id, last_used FROM servers WHERE is_active = 1 LIMIT 1");
    if (q.next()) {
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["token"] = q.value(3).toString();
        obj["userId"] = q.value(4).toString();
        obj["lastUsed"] = q.value(5).toString();
    }
    if (guard)
        emit activeServerFetched(obj);
}

void DBWorker::setActiveServer(int serverId, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    m_db.transaction();
    QSqlQuery q(m_db);
    q.exec("UPDATE servers SET is_active = 0");
    q.prepare("UPDATE servers SET is_active = 1, last_used = datetime('now') WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
    m_db.commit();
    Q_UNUSED(guard);
    // No result signal needed — caller just needs confirmation the work completed
}

void DBWorker::removeServer(int serverId, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM servers WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
    if (guard)
        emit serverRemoved(serverId);
}

void DBWorker::updateServerToken(int serverId, const QString &token,
                                  QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QSqlQuery q(m_db);
    q.prepare("UPDATE servers SET token = ? WHERE id = ?");
    q.addBindValue(token);
    q.addBindValue(serverId);
    q.exec();
    if (guard)
        emit serverTokenUpdated(serverId);
}

void DBWorker::getServerById(int serverId, QPointer<QObject> guard) {
    if (m_stopFlag) return;
    QJsonObject obj;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, server_url, username, token, user_id, last_used FROM servers WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
    if (q.next()) {
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["token"] = q.value(3).toString();
        obj["userId"] = q.value(4).toString();
        obj["lastUsed"] = q.value(5).toString();
    }
    if (guard)
        emit serverByIdFetched(obj);
}
