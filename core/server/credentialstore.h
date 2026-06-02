#pragma once
#include <QObject>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlDatabase>

class CredentialStore : public QObject {
    Q_OBJECT

public:
    explicit CredentialStore(const QSqlDatabase &db, QObject *parent = nullptr);

    Q_INVOKABLE int addServer(const QString &serverUrl, const QString &username,
                              const QString &password, const QString &token,
                              const QString &userId);
    Q_INVOKABLE QJsonArray getServers();
    Q_INVOKABLE QJsonObject getActiveServer();
    Q_INVOKABLE void setActiveServer(int serverId);
    Q_INVOKABLE void removeServer(int serverId);
    Q_INVOKABLE void updateServerToken(int serverId, const QString &token, const QString &userId);
    QJsonObject getServerById(int serverId);

private:
    QSqlDatabase m_db;
};
