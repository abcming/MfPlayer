#include "settingsstore.h"

SettingsStore::SettingsStore(QObject *parent)
    : QObject(parent)
    , m_settings(QSettings::IniFormat, QSettings::UserScope,
                 "mfplayer", "mfplayer")
{
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
    return m_settings.value("player/hdrPeakBrightness", 1000).toInt();
}
void SettingsStore::setHdrPeakBrightness(int nits) {
    if (nits != hdrPeakBrightness()) {
        m_settings.setValue("player/hdrPeakBrightness", nits);
        emit hdrPeakBrightnessChanged();
    }
}

int SettingsStore::windowWidth() const {
    return m_settings.value("ui/width", 0).toInt();
}
void SettingsStore::setWindowWidth(int w) {
    if (w != windowWidth()) {
        m_settings.setValue("ui/width", w);
        emit windowSizeChanged();
    }
}

int SettingsStore::windowHeight() const {
    return m_settings.value("ui/height", 0).toInt();
}
void SettingsStore::setWindowHeight(int h) {
    if (h != windowHeight()) {
        m_settings.setValue("ui/height", h);
        emit windowSizeChanged();
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
    return m_settings.value("player/seekForwardStep", 5).toInt();
}
void SettingsStore::setSeekForwardStep(int v) {
    if (v != seekForwardStep()) {
        m_settings.setValue("player/seekForwardStep", v);
        emit seekForwardStepChanged();
    }
}

int SettingsStore::seekBackwardStep() const {
    return m_settings.value("player/seekBackwardStep", 5).toInt();
}
void SettingsStore::setSeekBackwardStep(int v) {
    if (v != seekBackwardStep()) {
        m_settings.setValue("player/seekBackwardStep", v);
        emit seekBackwardStepChanged();
    }
}

// Qt::Key enum: Left=0x01000012, Right=0x01000014, Space=0x20, F=0x46, I=0x49
// BracketLeft=0x5b, BracketRight=0x5d, Up=0x01000015, Down=0x01000013

int SettingsStore::keySeekBackward() const { return m_settings.value("keys/seekBackward", 0x01000012).toInt(); }
void SettingsStore::setKeySeekBackward(int v) { if (v != keySeekBackward()) { m_settings.setValue("keys/seekBackward", v); emit keyBindingsChanged(); } }
int SettingsStore::keySeekForward() const { return m_settings.value("keys/seekForward", 0x01000014).toInt(); }
void SettingsStore::setKeySeekForward(int v) { if (v != keySeekForward()) { m_settings.setValue("keys/seekForward", v); emit keyBindingsChanged(); } }
int SettingsStore::keyPlayPause() const { return m_settings.value("keys/playPause", 0x20).toInt(); }
void SettingsStore::setKeyPlayPause(int v) { if (v != keyPlayPause()) { m_settings.setValue("keys/playPause", v); emit keyBindingsChanged(); } }
int SettingsStore::keyFullscreen() const { return m_settings.value("keys/fullscreen", 0x46).toInt(); }
void SettingsStore::setKeyFullscreen(int v) { if (v != keyFullscreen()) { m_settings.setValue("keys/fullscreen", v); emit keyBindingsChanged(); } }
int SettingsStore::keyStats() const { return m_settings.value("keys/stats", 0x49).toInt(); }
void SettingsStore::setKeyStats(int v) { if (v != keyStats()) { m_settings.setValue("keys/stats", v); emit keyBindingsChanged(); } }
int SettingsStore::keySpeedDown() const { return m_settings.value("keys/speedDown", 0x5b).toInt(); }
void SettingsStore::setKeySpeedDown(int v) { if (v != keySpeedDown()) { m_settings.setValue("keys/speedDown", v); emit keyBindingsChanged(); } }
int SettingsStore::keySpeedUp() const { return m_settings.value("keys/speedUp", 0x5d).toInt(); }
void SettingsStore::setKeySpeedUp(int v) { if (v != keySpeedUp()) { m_settings.setValue("keys/speedUp", v); emit keyBindingsChanged(); } }
int SettingsStore::keyVolumeUp() const { return m_settings.value("keys/volumeUp", 0x01000015).toInt(); }
void SettingsStore::setKeyVolumeUp(int v) { if (v != keyVolumeUp()) { m_settings.setValue("keys/volumeUp", v); emit keyBindingsChanged(); } }
int SettingsStore::keyVolumeDown() const { return m_settings.value("keys/volumeDown", 0x01000013).toInt(); }
void SettingsStore::setKeyVolumeDown(int v) { if (v != keyVolumeDown()) { m_settings.setValue("keys/volumeDown", v); emit keyBindingsChanged(); } }

void SettingsStore::saveLogin(const QString &server, const QString &username,
                               const QString &token, const QString &userId) {
    setEmbyServer(server);
    setEmbyUsername(username);
    setEmbyToken(token);
    setEmbyUserId(userId);
}
