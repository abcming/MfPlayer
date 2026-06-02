#include "core/server/credentialstore.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QDebug>

CredentialStore::CredentialStore(const QSqlDatabase &db, QObject *parent)
    : QObject(parent), m_db(db)
{
    // Ensure the servers table exists (shared with CacheStore's cache.db)
    QSqlQuery q(m_db);
    q.exec("CREATE TABLE IF NOT EXISTS servers ("
           "id INTEGER PRIMARY KEY AUTOINCREMENT, server_url TEXT NOT NULL, "
           "username TEXT NOT NULL, password TEXT NOT NULL, "
           "token TEXT DEFAULT '', user_id TEXT DEFAULT '', "
           "is_active INTEGER DEFAULT 0, last_used TEXT)");
}

int CredentialStore::addServer(const QString &serverUrl, const QString &username,
                               const QString &password, const QString &token,
                               const QString &userId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM servers WHERE server_url = ? AND username = ?");
    q.addBindValue(serverUrl);
    q.addBindValue(username);
    q.exec();
    if (q.next()) {
        int id = q.value(0).toInt();
        QSqlQuery u(m_db);
        u.prepare("UPDATE servers SET password = ?, token = ?, user_id = ?, last_used = datetime('now') WHERE id = ?");
        u.addBindValue(password);
        u.addBindValue(token);
        u.addBindValue(userId);
        u.addBindValue(id);
        u.exec();
        return id;
    }
    QSqlQuery deact(m_db);
    deact.exec("UPDATE servers SET is_active = 0");
    QSqlQuery ins(m_db);
    ins.prepare("INSERT INTO servers (server_url, username, password, token, user_id, is_active, last_used) "
                "VALUES (?,?,?,?,?,1,datetime('now'))");
    ins.addBindValue(serverUrl);
    ins.addBindValue(username);
    ins.addBindValue(password);
    ins.addBindValue(token);
    ins.addBindValue(userId);
    ins.exec();
    return ins.lastInsertId().toInt();
}

QJsonArray CredentialStore::getServers() {
    QSqlQuery q(m_db);
    q.exec("SELECT id, server_url, username, token, user_id, is_active, last_used FROM servers ORDER BY last_used DESC");
    QJsonArray arr;
    while (q.next()) {
        QJsonObject obj;
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["token"] = q.value(3).toString();
        obj["userId"] = q.value(4).toString();
        obj["isActive"] = q.value(5).toBool();
        obj["lastUsed"] = q.value(6).toString();
        arr.append(obj);
    }
    return arr;
}

QJsonObject CredentialStore::getActiveServer() {
    QSqlQuery q(m_db);
    q.exec("SELECT id, server_url, username, password, token, user_id, last_used FROM servers WHERE is_active = 1 LIMIT 1");
    if (q.next()) {
        QJsonObject obj;
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["password"] = q.value(3).toString();
        obj["token"] = q.value(4).toString();
        obj["userId"] = q.value(5).toString();
        obj["lastUsed"] = q.value(6).toString();
        return obj;
    }
    return {};
}

void CredentialStore::setActiveServer(int serverId) {
    QSqlQuery q(m_db);
    m_db.transaction();
    q.exec("UPDATE servers SET is_active = 0");
    q.prepare("UPDATE servers SET is_active = 1, last_used = datetime('now') WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
    m_db.commit();
}

void CredentialStore::removeServer(int serverId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM servers WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
}

void CredentialStore::updateServerToken(int serverId, const QString &token, const QString &userId) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE servers SET token = ?, user_id = ?, last_used = datetime('now') WHERE id = ?");
    q.addBindValue(token);
    q.addBindValue(userId);
    q.addBindValue(serverId);
    q.exec();
}

QJsonObject CredentialStore::getServerById(int serverId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id, server_url, username, password, token, user_id, last_used FROM servers WHERE id = ?");
    q.addBindValue(serverId);
    q.exec();
    if (q.next()) {
        QJsonObject obj;
        obj["id"] = q.value(0).toInt();
        obj["serverUrl"] = q.value(1).toString();
        obj["username"] = q.value(2).toString();
        obj["password"] = q.value(3).toString();
        obj["token"] = q.value(4).toString();
        obj["userId"] = q.value(5).toString();
        obj["lastUsed"] = q.value(6).toString();
        return obj;
    }
    return {};
}
