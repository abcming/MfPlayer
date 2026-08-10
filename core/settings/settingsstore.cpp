#include "settingsstore.h"
#include <QTimer>

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
                 "mfplayer", "mfplayer")
{
    // Initialize cached values from QSettings
    m_hdrPeakBrightness = m_settings.value("player/hdrPeakBrightness", 1000).toInt();
    m_sdrWhiteNits = m_settings.value("player/sdrWhiteNits", 203).toInt();
    m_seekForwardStep = m_settings.value("player/seekForwardStep", 5).toInt();
    m_seekBackwardStep = m_settings.value("player/seekBackwardStep", 5).toInt();
    m_windowWidth = m_settings.value("ui/width", 0).toInt();
    m_windowHeight = m_settings.value("ui/height", 0).toInt();

    // Initialize cached key bindings from QSettings
    m_keySeekBackward = m_settings.value("keys/seekBackward", 0x01000012).toInt();
    m_keySeekForward = m_settings.value("keys/seekForward", 0x01000014).toInt();
    m_keyPlayPause = m_settings.value("keys/playPause", 0x20).toInt();
    m_keyFullscreen = m_settings.value("keys/fullscreen", 0x46).toInt();
    m_keyStats = m_settings.value("keys/stats", 0x49).toInt();
    m_keySpeedDown = m_settings.value("keys/speedDown", 0x5b).toInt();
    m_keySpeedUp = m_settings.value("keys/speedUp", 0x5d).toInt();
    m_keyVolumeUp = m_settings.value("keys/volumeUp", 0x01000015).toInt();
    m_keyVolumeDown = m_settings.value("keys/volumeDown", 0x01000013).toInt();

    // Set up debounce timers (avoids writing to disk on every drag pixel / resize event)
    m_hdrWriteTimer = new QTimer(this);
    m_hdrWriteTimer->setSingleShot(true);
    m_hdrWriteTimer->setInterval(200);
    connect(m_hdrWriteTimer, &QTimer::timeout, this, [this]() {
        m_settings.setValue("player/hdrPeakBrightness", m_hdrPeakBrightness);
    });

    m_sdrWriteTimer = new QTimer(this);
    m_sdrWriteTimer->setSingleShot(true);
    m_sdrWriteTimer->setInterval(200);
    connect(m_sdrWriteTimer, &QTimer::timeout, this, [this]() {
        m_settings.setValue("player/sdrWhiteNits", m_sdrWhiteNits);
    });

    m_seekFwdWriteTimer = new QTimer(this);
    m_seekFwdWriteTimer->setSingleShot(true);
    m_seekFwdWriteTimer->setInterval(200);
    connect(m_seekFwdWriteTimer, &QTimer::timeout, this, [this]() {
        m_settings.setValue("player/seekForwardStep", m_seekForwardStep);
    });

    m_seekBackWriteTimer = new QTimer(this);
    m_seekBackWriteTimer->setSingleShot(true);
    m_seekBackWriteTimer->setInterval(200);
    connect(m_seekBackWriteTimer, &QTimer::timeout, this, [this]() {
        m_settings.setValue("player/seekBackwardStep", m_seekBackwardStep);
    });

    m_windowSizeTimer = new QTimer(this);
    m_windowSizeTimer->setSingleShot(true);
    m_windowSizeTimer->setInterval(200);
    connect(m_windowSizeTimer, &QTimer::timeout, this, [this]() {
        m_settings.setValue("ui/width", m_windowWidth);
        m_settings.setValue("ui/height", m_windowHeight);
    });
}

SettingsStore::~SettingsStore() {
    // 防抖的值先落在成员变量里, 要等 200ms 后 timer 触发才写进 QSettings。
    // 调完 HDR 亮度 200ms 内 Alt+F4, 那个值从没进过 QSettings —— QSettings
    // 自己的 sync 也救不了它。这里把还在等的直接兑现。
    //
    // 走 invokeMethod 触发 timeout 而不是重抄一遍 setValue: key 只写在
    // 构造函数那一处, 以后加新的防抖项不会漏掉这里
    auto fire = [](QTimer *t) {
        if (t && t->isActive()) {
            t->stop();
            QMetaObject::invokeMethod(t, "timeout", Qt::DirectConnection);
        }
    };
    fire(m_hdrWriteTimer);
    fire(m_sdrWriteTimer);
    fire(m_seekFwdWriteTimer);
    fire(m_seekBackWriteTimer);
    fire(m_windowSizeTimer);
}

QString SettingsStore::embyServer() const {
    return m_settings.value("emby/server").toString();
}
void SettingsStore::setEmbyServer(const QString &url) {
    if (url != embyServer()) {
        m_settings.setValue("emby/server", url);
        emit embyServerChanged();
    }
}

QString SettingsStore::embyUsername() const {
    return m_settings.value("emby/username").toString();
}
void SettingsStore::setEmbyUsername(const QString &name) {
    if (name != embyUsername()) {
        m_settings.setValue("emby/username", name);
        emit embyUsernameChanged();
    }
}

QString SettingsStore::embyToken() const {
    return m_settings.value("emby/token").toString();
}
void SettingsStore::setEmbyToken(const QString &token) {
    if (token != embyToken()) {
        m_settings.setValue("emby/token", token);
        emit embyTokenChanged();
    }
}

QString SettingsStore::embyUserId() const {
    return m_settings.value("emby/userId").toString();
}
void SettingsStore::setEmbyUserId(const QString &id) {
    if (id != embyUserId()) {
        m_settings.setValue("emby/userId", id);
        emit embyUserIdChanged();
    }
}

int SettingsStore::volume() const {
    return m_settings.value("player/volume", 80).toInt();
}
void SettingsStore::setVolume(int vol) {
    if (vol != volume()) {
        m_settings.setValue("player/volume", vol);
        emit volumeChanged();
    }
}

int SettingsStore::hdrPeakBrightness() const {
    return m_hdrPeakBrightness;
}
void SettingsStore::setHdrPeakBrightness(int nits) {
    if (nits != m_hdrPeakBrightness) {
        m_hdrPeakBrightness = nits;
        emit hdrPeakBrightnessChanged();
        m_hdrWriteTimer->start();
    }
}

int SettingsStore::sdrWhiteNits() const {
    return m_sdrWhiteNits;
}
void SettingsStore::setSdrWhiteNits(int nits) {
    if (nits != m_sdrWhiteNits) {
        m_sdrWhiteNits = nits;
        emit sdrWhiteNitsChanged();
        m_sdrWriteTimer->start();
    }
}

int SettingsStore::windowWidth() const {
    return m_windowWidth;
}
void SettingsStore::setWindowWidth(int w) {
    if (w != m_windowWidth) {
        m_windowWidth = w;
        emit windowSizeChanged();
        m_windowSizeTimer->start();
    }
}

int SettingsStore::windowHeight() const {
    return m_windowHeight;
}
void SettingsStore::setWindowHeight(int h) {
    if (h != m_windowHeight) {
        m_windowHeight = h;
        emit windowSizeChanged();
        m_windowSizeTimer->start();
    }
}

int SettingsStore::sortBy() const {
    return m_settings.value("library/sortBy", 0).toInt();
}
void SettingsStore::setSortBy(int v) {
    if (v != sortBy()) {
        m_settings.setValue("library/sortBy", v);
        emit sortByChanged();
    }
}

bool SettingsStore::sortAscending() const {
    return m_settings.value("library/sortAscending", true).toBool();
}
void SettingsStore::setSortAscending(bool v) {
    if (v != sortAscending()) {
        m_settings.setValue("library/sortAscending", v);
        emit sortAscendingChanged();
    }
}

QString SettingsStore::audioLanguage() const {
    return m_settings.value("player/audioLanguage").toString();
}
void SettingsStore::setAudioLanguage(const QString &lang) {
    if (lang != audioLanguage()) {
        m_settings.setValue("player/audioLanguage", lang);
        emit audioLanguageChanged();
    }
}

QString SettingsStore::subtitleLanguage() const {
    return m_settings.value("player/subtitleLanguage").toString();
}
void SettingsStore::setSubtitleLanguage(const QString &lang) {
    if (lang != subtitleLanguage()) {
        m_settings.setValue("player/subtitleLanguage", lang);
        emit subtitleLanguageChanged();
    }
}

int SettingsStore::actionAfterEnd() const {
    return m_settings.value("player/actionAfterEnd", 0).toInt();
}
void SettingsStore::setActionAfterEnd(int v) {
    if (v != actionAfterEnd()) {
        m_settings.setValue("player/actionAfterEnd", v);
        emit actionAfterEndChanged();
    }
}

int SettingsStore::seekForwardStep() const {
    return m_seekForwardStep;
}
void SettingsStore::setSeekForwardStep(int v) {
    if (v != m_seekForwardStep) {
        m_seekForwardStep = v;
        emit seekForwardStepChanged();
        m_seekFwdWriteTimer->start();
    }
}

int SettingsStore::seekBackwardStep() const {
    return m_seekBackwardStep;
}
void SettingsStore::setSeekBackwardStep(int v) {
    if (v != m_seekBackwardStep) {
        m_seekBackwardStep = v;
        emit seekBackwardStepChanged();
        m_seekBackWriteTimer->start();
    }
}

// Qt::Key enum: Left=0x01000012, Right=0x01000014, Space=0x20, F=0x46, I=0x49
// BracketLeft=0x5b, BracketRight=0x5d, Up=0x01000015, Down=0x01000013

int SettingsStore::keySeekBackward() const { return m_keySeekBackward; }
void SettingsStore::setKeySeekBackward(int v) { if (v != m_keySeekBackward) { m_keySeekBackward = v; m_settings.setValue("keys/seekBackward", v); emit keyBindingsChanged(); } }
int SettingsStore::keySeekForward() const { return m_keySeekForward; }
void SettingsStore::setKeySeekForward(int v) { if (v != m_keySeekForward) { m_keySeekForward = v; m_settings.setValue("keys/seekForward", v); emit keyBindingsChanged(); } }
int SettingsStore::keyPlayPause() const { return m_keyPlayPause; }
void SettingsStore::setKeyPlayPause(int v) { if (v != m_keyPlayPause) { m_keyPlayPause = v; m_settings.setValue("keys/playPause", v); emit keyBindingsChanged(); } }
int SettingsStore::keyFullscreen() const { return m_keyFullscreen; }
void SettingsStore::setKeyFullscreen(int v) { if (v != m_keyFullscreen) { m_keyFullscreen = v; m_settings.setValue("keys/fullscreen", v); emit keyBindingsChanged(); } }
int SettingsStore::keyStats() const { return m_keyStats; }
void SettingsStore::setKeyStats(int v) { if (v != m_keyStats) { m_keyStats = v; m_settings.setValue("keys/stats", v); emit keyBindingsChanged(); } }
int SettingsStore::keySpeedDown() const { return m_keySpeedDown; }
void SettingsStore::setKeySpeedDown(int v) { if (v != m_keySpeedDown) { m_keySpeedDown = v; m_settings.setValue("keys/speedDown", v); emit keyBindingsChanged(); } }
int SettingsStore::keySpeedUp() const { return m_keySpeedUp; }
void SettingsStore::setKeySpeedUp(int v) { if (v != m_keySpeedUp) { m_keySpeedUp = v; m_settings.setValue("keys/speedUp", v); emit keyBindingsChanged(); } }
int SettingsStore::keyVolumeUp() const { return m_keyVolumeUp; }
void SettingsStore::setKeyVolumeUp(int v) { if (v != m_keyVolumeUp) { m_keyVolumeUp = v; m_settings.setValue("keys/volumeUp", v); emit keyBindingsChanged(); } }
int SettingsStore::keyVolumeDown() const { return m_keyVolumeDown; }
void SettingsStore::setKeyVolumeDown(int v) { if (v != m_keyVolumeDown) { m_keyVolumeDown = v; m_settings.setValue("keys/volumeDown", v); emit keyBindingsChanged(); } }

bool SettingsStore::skipSslVerify() const {
    return m_settings.value("network/skipSslVerify", false).toBool();
}
void SettingsStore::setSkipSslVerify(bool v) {
    if (v != skipSslVerify()) {
        m_settings.setValue("network/skipSslVerify", v);
        emit skipSslVerifyChanged();
    }
}

int SettingsStore::graphicsApi() const {
    return m_settings.value("render/graphicsApi", 0).toInt();
}
void SettingsStore::setGraphicsApi(int v) {
    if (v != graphicsApi()) {
        m_settings.setValue("render/graphicsApi", v);
        emit graphicsApiChanged();
    }
}

void SettingsStore::saveLogin(const QString &server, const QString &username,
                               const QString &token, const QString &userId) {
    setEmbyServer(server);
    setEmbyUsername(username);
    setEmbyToken(token);
    setEmbyUserId(userId);
}

void SettingsStore::reloadKeyBindings() {
    m_keySeekBackward = m_settings.value("keys/seekBackward", 0x01000012).toInt();
    m_keySeekForward = m_settings.value("keys/seekForward", 0x01000014).toInt();
    m_keyPlayPause = m_settings.value("keys/playPause", 0x20).toInt();
    m_keyFullscreen = m_settings.value("keys/fullscreen", 0x46).toInt();
    m_keyStats = m_settings.value("keys/stats", 0x49).toInt();
    m_keySpeedDown = m_settings.value("keys/speedDown", 0x5b).toInt();
    m_keySpeedUp = m_settings.value("keys/speedUp", 0x5d).toInt();
    m_keyVolumeUp = m_settings.value("keys/volumeUp", 0x01000015).toInt();
    m_keyVolumeDown = m_settings.value("keys/volumeDown", 0x01000013).toInt();
    emit keyBindingsChanged();
}
