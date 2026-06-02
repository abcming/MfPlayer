#include "core/server/servermanager.h"

ServerManager::ServerManager(QObject *parent)
    : QObject(parent)
    , m_settings(new SettingsStore(this))
    , m_emby(new EmbyClient(this))
    , m_cache(new CacheStore(this))
{
    m_cache->init();
    m_creds = new CredentialStore(m_cache->database(), this);
    connect(m_emby, &EmbyClient::librariesFetched, this, &ServerManager::onLibrariesFetched);
    connect(m_emby, &EmbyClient::tokenExpired, this, &ServerManager::onTokenExpired);
}

ServerManager::~ServerManager() = default;

bool ServerManager::embyConnected() const {
    return m_emby->authenticated();
}

QJsonArray ServerManager::serverList() const {
    return m_creds->getServers();
}

void ServerManager::connectEmby(const QString &serverUrl, const QString &username,
                                 const QString &password) {
    performLogin(serverUrl, username, password);
}

void ServerManager::addServer(const QString &serverUrl, const QString &username,
                               const QString &password) {
    performLogin(serverUrl, username, password);
}

void ServerManager::performLogin(const QString &serverUrl, const QString &username,
                                  const QString &password) {
    int gen = ++m_serverGeneration;
    m_emby->logout();  // clear old credentials so avatar URL doesn't mix old token + new server
    m_emby->setServer(serverUrl);
    disconnect(m_emby, &EmbyClient::loginSuccess, this, nullptr);
    disconnect(m_emby, &EmbyClient::loginFailed, this, nullptr);
    connect(m_emby, &EmbyClient::loginSuccess, this, [this, gen, serverUrl, username, password](const QString &token, const QString &userId) {
        if (gen != m_serverGeneration) return;
        m_creds->addServer(serverUrl, username, password, token, userId);
        m_settings->saveLogin(serverUrl, username, token, userId);
        emit embyConnectedChanged();
        emit serverListChanged();
        m_librariesGeneration = m_serverGeneration;
        m_emby->fetchLibraries();
    });
    connect(m_emby, &EmbyClient::loginFailed, this, [this, gen](const QString &error) {
        if (gen != m_serverGeneration) return;
        emit playError("登录失败：" + error);
    });
    m_emby->login(username, password);
}

void ServerManager::switchToServer(int serverId) {
    ++m_serverGeneration;
    QJsonObject s = m_creds->getServerById(serverId);
    if (s.isEmpty()) return;

    QString serverUrl = s["serverUrl"].toString();
    QString token = s["token"].toString();
    QString userId = s["userId"].toString();
    QString password = s["password"].toString();
    QString username = s["username"].toString();

    m_emby->logout();  // clear old credentials before changing server URL
    m_emby->setServer(serverUrl);
    m_emby->setAuth(token, userId);
    m_creds->setActiveServer(serverId);
    m_settings->saveLogin(serverUrl, username, token, userId);
    emit embyConnectedChanged();
    emit serverListChanged();

    // Signal receivers to clear their data before fetching new
    emit loggedOut();
    m_librariesGeneration = m_serverGeneration;
    m_emby->fetchLibraries();

    // If libraries come back empty, token may have expired — try re-login
    m_pendingSwitchServerId = serverId;
    m_pendingSwitchPassword = password;
}

void ServerManager::removeServerFromList(int serverId) {
    QJsonObject active = m_creds->getActiveServer();
    bool wasActive = (active["id"].toInt() == serverId);

    m_creds->removeServer(serverId);

    if (wasActive) {
        // Was the active server — disconnect
        disconnectCurrentServer();
    }
    emit serverListChanged();
}

void ServerManager::disconnectCurrentServer() {
    ++m_serverGeneration;
    m_emby->logout();

    // Delete active server from DB
    QJsonObject active = m_creds->getActiveServer();
    if (!active.isEmpty()) {
        m_creds->removeServer(active["id"].toInt());
    }

    m_settings->saveLogin(QString(), QString(), QString(), QString());
    m_pendingSwitchServerId = -1;
    m_pendingSwitchPassword.clear();
    emit embyConnectedChanged();
    emit serverListChanged();
    emit loggedOut();
}

void ServerManager::logout() {
    disconnectCurrentServer();
}

bool ServerManager::restoreSession() {
    // Try SQLite active server first
    QJsonObject active = m_creds->getActiveServer();
    if (!active.isEmpty()) {
        QString url = active["serverUrl"].toString();
        QString token = active["token"].toString();
        QString userId = active["userId"].toString();
        QString username = active["username"].toString();
        if (!url.isEmpty() && !token.isEmpty() && !userId.isEmpty()) {
            m_emby->setServer(url);
            m_emby->setAuth(token, userId);
            m_settings->saveLogin(url, username, token, userId);
            m_librariesGeneration = m_serverGeneration;
            m_emby->fetchLibraries();
            emit embyConnectedChanged();
            return true;
        }
    }

    // Fallback: try old INI settings
    QString srv = m_settings->embyServer();
    QString token = m_settings->embyToken();
    QString userId = m_settings->embyUserId();
    if (!srv.isEmpty() && !token.isEmpty() && !userId.isEmpty()) {
        m_emby->setServer(srv);
        m_emby->setAuth(token, userId);
        m_librariesGeneration = m_serverGeneration;
        m_emby->fetchLibraries();
        emit embyConnectedChanged();
        return true;
    }

    return false;
}

void ServerManager::onLibrariesFetched(const QJsonArray &libraries) {
    // Discard stale responses from a previous server connection
    if (m_librariesGeneration != m_serverGeneration) return;

    // Token expiry recovery: if switching server and no libraries returned, try re-login
    if (libraries.isEmpty() && m_pendingSwitchServerId >= 0 && !m_pendingSwitchPassword.isEmpty()) {
        int serverId = m_pendingSwitchServerId;
        QString password = m_pendingSwitchPassword;
        int gen = m_serverGeneration;
        m_pendingSwitchServerId = -1;
        m_pendingSwitchPassword.clear();

        disconnect(m_emby, &EmbyClient::loginSuccess, this, nullptr);
        disconnect(m_emby, &EmbyClient::loginFailed, this, nullptr);
        connect(m_emby, &EmbyClient::loginSuccess, this, [this, gen, serverId, password](const QString &token, const QString &userId) {
            if (gen != m_serverGeneration) return;
            m_creds->updateServerToken(serverId, token, userId);
            m_settings->saveLogin(m_emby->serverUrl(), m_settings->embyUsername(), token, userId);
            emit embyConnectedChanged();
            emit serverListChanged();
            m_librariesGeneration = m_serverGeneration;
            m_emby->fetchLibraries();  // retry with new token
        });
        connect(m_emby, &EmbyClient::loginFailed, this, [this, gen](const QString &error) {
            if (gen != m_serverGeneration) return;
            emit playError("服务器登录已过期：" + error);
            disconnectCurrentServer();
        });
        // Use the stored username from settings (set during switchToServer)
        m_emby->login(m_settings->embyUsername(), password);
        return;
    }

    if (!libraries.isEmpty()) {
        m_pendingSwitchServerId = -1;
        m_pendingSwitchPassword.clear();
    }

    emit librariesReady(libraries);
}

void ServerManager::onTokenExpired() {
    if (m_reauthing) return;
    QJsonObject active = m_creds->getActiveServer();
    if (active.isEmpty()) return;
    QString username = active["username"].toString();
    QString password = active["password"].toString();
    if (password.isEmpty()) return;

    m_reauthing = true;
    int serverId = active["id"].toInt();
    int gen = m_serverGeneration;

    disconnect(m_emby, &EmbyClient::loginSuccess, this, nullptr);
    disconnect(m_emby, &EmbyClient::loginFailed, this, nullptr);
    connect(m_emby, &EmbyClient::loginSuccess, this, [this, gen, serverId](const QString &token, const QString &userId) {
        if (gen != m_serverGeneration) { m_reauthing = false; return; }
        m_reauthing = false;
        m_creds->updateServerToken(serverId, token, userId);
        m_settings->saveLogin(m_emby->serverUrl(), m_settings->embyUsername(), token, userId);
    });
    connect(m_emby, &EmbyClient::loginFailed, this, [this, gen](const QString &) {
        if (gen != m_serverGeneration) { m_reauthing = false; return; }
        m_reauthing = false;
        emit playError("登录已过期，请重新登录");
        disconnectCurrentServer();
    });
    m_emby->login(username, password);
}
