#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include "core/settings/settingsstore.h"
#include "core/providers/emby/embyclient.h"
#include "core/cache/cachestore.h"
#include "core/server/credentialstore.h"

class ServerManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool embyConnected READ embyConnected NOTIFY embyConnectedChanged FINAL)
    Q_PROPERTY(QJsonArray serverList READ serverList NOTIFY serverListChanged FINAL)
    Q_PROPERTY(SettingsStore* settings READ settings CONSTANT)
    Q_PROPERTY(EmbyClient* emby READ emby CONSTANT)
    Q_PROPERTY(CacheStore* cache READ cache CONSTANT)

public:
    explicit ServerManager(QObject *parent = nullptr);
    ~ServerManager() override;

    bool embyConnected() const;
    QJsonArray serverList() const;
    SettingsStore *settings() const { return m_settings; }
    EmbyClient *emby() const { return m_emby; }
    CacheStore *cache() const { return m_cache; }
    CredentialStore *creds() const { return m_creds; }

public slots:
    Q_INVOKABLE void connectEmby(const QString &serverUrl, const QString &username, const QString &password);
    Q_INVOKABLE void addServer(const QString &serverUrl, const QString &username, const QString &password);
    Q_INVOKABLE void switchToServer(int serverId);
    Q_INVOKABLE void removeServerFromList(int serverId);
    Q_INVOKABLE void disconnectCurrentServer();
    Q_INVOKABLE void logout();
    Q_INVOKABLE bool restoreSession();

signals:
    void embyConnectedChanged();
    void serverListChanged();
    void loggedOut();
    void librariesReady(const QJsonArray &libraries);
    void playError(const QString &message);

private:
    void performLogin(const QString &serverUrl, const QString &username, const QString &password);
    void onLibrariesFetched(const QJsonArray &libraries);
    void onTokenExpired();

    SettingsStore *m_settings;
    EmbyClient *m_emby;
    CacheStore *m_cache;
    CredentialStore *m_creds;
    int m_serverGeneration = 0;
    int m_librariesGeneration = 0;
    int m_pendingSwitchServerId = -1;
    QString m_pendingSwitchPassword;
    bool m_reauthing = false;
};
