#pragma once
#include <QObject>
#include <QSettings>

class QTimer;

class SettingsStore : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString embyServer READ embyServer WRITE setEmbyServer NOTIFY embyServerChanged FINAL)
    Q_PROPERTY(QString embyUsername READ embyUsername WRITE setEmbyUsername NOTIFY embyUsernameChanged FINAL)
    Q_PROPERTY(QString embyToken READ embyToken WRITE setEmbyToken NOTIFY embyTokenChanged FINAL)
    Q_PROPERTY(QString embyUserId READ embyUserId WRITE setEmbyUserId NOTIFY embyUserIdChanged FINAL)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(int hdrPeakBrightness READ hdrPeakBrightness WRITE setHdrPeakBrightness NOTIFY hdrPeakBrightnessChanged FINAL)
    Q_PROPERTY(int sdrWhiteNits READ sdrWhiteNits WRITE setSdrWhiteNits NOTIFY sdrWhiteNitsChanged FINAL)
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY windowSizeChanged FINAL)
    Q_PROPERTY(int windowHeight READ windowHeight WRITE setWindowHeight NOTIFY windowSizeChanged FINAL)
    Q_PROPERTY(int sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged FINAL)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged FINAL)
    Q_PROPERTY(QString audioLanguage READ audioLanguage WRITE setAudioLanguage NOTIFY audioLanguageChanged FINAL)
    Q_PROPERTY(QString subtitleLanguage READ subtitleLanguage WRITE setSubtitleLanguage NOTIFY subtitleLanguageChanged FINAL)
    Q_PROPERTY(int actionAfterEnd READ actionAfterEnd WRITE setActionAfterEnd NOTIFY actionAfterEndChanged FINAL)
    Q_PROPERTY(int seekForwardStep READ seekForwardStep WRITE setSeekForwardStep NOTIFY seekForwardStepChanged FINAL)
    Q_PROPERTY(int seekBackwardStep READ seekBackwardStep WRITE setSeekBackwardStep NOTIFY seekBackwardStepChanged FINAL)
    Q_PROPERTY(int keySeekBackward READ keySeekBackward WRITE setKeySeekBackward NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keySeekForward READ keySeekForward WRITE setKeySeekForward NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keyPlayPause READ keyPlayPause WRITE setKeyPlayPause NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keyFullscreen READ keyFullscreen WRITE setKeyFullscreen NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keyStats READ keyStats WRITE setKeyStats NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keySpeedDown READ keySpeedDown WRITE setKeySpeedDown NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keySpeedUp READ keySpeedUp WRITE setKeySpeedUp NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keyVolumeUp READ keyVolumeUp WRITE setKeyVolumeUp NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(int keyVolumeDown READ keyVolumeDown WRITE setKeyVolumeDown NOTIFY keyBindingsChanged FINAL)
    Q_PROPERTY(bool skipSslVerify READ skipSslVerify WRITE setSkipSslVerify NOTIFY skipSslVerifyChanged FINAL)
    Q_PROPERTY(int graphicsApi READ graphicsApi WRITE setGraphicsApi NOTIFY graphicsApiChanged FINAL)

public:
    explicit SettingsStore(QObject *parent = nullptr);

    QString embyServer() const;
    void setEmbyServer(const QString &url);

    QString embyUsername() const;
    void setEmbyUsername(const QString &name);

    QString embyToken() const;
    void setEmbyToken(const QString &token);

    QString embyUserId() const;
    void setEmbyUserId(const QString &id);

    int volume() const;
    void setVolume(int vol);

    int hdrPeakBrightness() const;
    void setHdrPeakBrightness(int nits);

    int sdrWhiteNits() const;
    void setSdrWhiteNits(int nits);

    int windowWidth() const;
    void setWindowWidth(int w);
    int windowHeight() const;
    void setWindowHeight(int h);

    int sortBy() const;
    void setSortBy(int v);
    bool sortAscending() const;
    void setSortAscending(bool v);

    QString audioLanguage() const;
    void setAudioLanguage(const QString &lang);
    QString subtitleLanguage() const;
    void setSubtitleLanguage(const QString &lang);

    int actionAfterEnd() const;
    void setActionAfterEnd(int v);
    int seekForwardStep() const;
    void setSeekForwardStep(int v);
    int seekBackwardStep() const;
    void setSeekBackwardStep(int v);

    int keySeekBackward() const;
    void setKeySeekBackward(int v);
    int keySeekForward() const;
    void setKeySeekForward(int v);
    int keyPlayPause() const;
    void setKeyPlayPause(int v);
    int keyFullscreen() const;
    void setKeyFullscreen(int v);
    int keyStats() const;
    void setKeyStats(int v);
    int keySpeedDown() const;
    void setKeySpeedDown(int v);
    int keySpeedUp() const;
    void setKeySpeedUp(int v);
    int keyVolumeUp() const;
    void setKeyVolumeUp(int v);
    int keyVolumeDown() const;
    void setKeyVolumeDown(int v);

    bool skipSslVerify() const;
    void setSkipSslVerify(bool v);

    int graphicsApi() const;
    void setGraphicsApi(int v);

    Q_INVOKABLE void saveLogin(const QString &server, const QString &username,
                               const QString &token, const QString &userId);

signals:
    void embyServerChanged();
    void embyUsernameChanged();
    void embyTokenChanged();
    void embyUserIdChanged();
    void volumeChanged();
    void hdrPeakBrightnessChanged();
    void sdrWhiteNitsChanged();
    void windowSizeChanged();
    void sortByChanged();
    void sortAscendingChanged();
    void audioLanguageChanged();
    void subtitleLanguageChanged();
    void actionAfterEndChanged();
    void seekForwardStepChanged();
    void seekBackwardStepChanged();
    void keyBindingsChanged();
    void skipSslVerifyChanged();
    void graphicsApiChanged();

private:
    QSettings m_settings;
    // Cached values for debounced properties (write delayed by QTimer)
    int m_hdrPeakBrightness = 1000;
    int m_sdrWhiteNits = 203;
    int m_seekForwardStep = 5;
    int m_seekBackwardStep = 5;
    int m_windowWidth = 0;
    int m_windowHeight = 0;
    QTimer *m_hdrWriteTimer = nullptr;
    QTimer *m_sdrWriteTimer = nullptr;
    QTimer *m_seekFwdWriteTimer = nullptr;
    QTimer *m_seekBackWriteTimer = nullptr;
    QTimer *m_windowSizeTimer = nullptr;
};
