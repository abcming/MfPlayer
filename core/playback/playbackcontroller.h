#pragma once
#include <QObject>
#include <QJsonObject>
#include <QVariant>
#include <QTimer>
#include <QWindow>
#include <QPointer>
#include "core/settings/settingsstore.h"
#include "core/providers/emby/embyclient.h"
#include "platform/rendering/mpv/mpvcontroller.h"
#include "core/cache/cachestore.h"

class PlaybackController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged FINAL)
    Q_PROPERTY(MpvController* mpv READ mpv CONSTANT)
    Q_PROPERTY(QJsonObject currentItemDetail READ currentItemDetail NOTIFY currentItemDetailChanged FINAL)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged FINAL)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int currentSid READ currentSid NOTIFY sidChanged FINAL)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged FINAL)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY chapterChanged FINAL)

public:
    explicit PlaybackController(EmbyClient *emby, CacheStore *cache, SettingsStore *settings, QObject *parent = nullptr);
    ~PlaybackController() override;

    double position() const;
    double duration() const;
    bool playing() const;
    int volume() const;
    bool fullscreen() const;
    MpvController *mpv() const { return m_mpv; }
    QJsonObject currentItemDetail() const { return m_currentItemDetail; }
    double speed() const { return m_mpv->speed(); }
    QVariantList tracks() const { return m_mpv->tracks(); }
    int currentSid() const { return m_mpv->currentSid(); }
    QVariantList chapters() const { return m_mpv->chapters(); }
    int currentChapter() const { return m_mpv->currentChapter(); }

public slots:
    void playItem(const QString &itemId, qint64 startTimeTicks = 0,
                  const QString &mediaSourceId = QString(),
                  int audioIndex = -1, int subtitleIndex = -2);
    void playLocalFile(const QString &filePath);
    Q_INVOKABLE void scanFolderForLocalPlaylistAsync(const QString &filePath);
    void pause();
    void resume();
    void stop();
    void stopPlayback();  // called on logout (no report, just stop)
    void seek(double pos);
    void setVolume(int vol);
    Q_INVOKABLE void setHdrPeakBrightness(int nits);
    Q_INVOKABLE QString subtitleTrackTitle(int sid) const;
    Q_INVOKABLE void setSubPos(int pos) { m_mpv->setSubPos(pos); }
    Q_INVOKABLE void setSid(int sid) { m_mpv->setSid(sid); }
    Q_INVOKABLE void setAid(int aid) { m_mpv->setAid(aid); }
    Q_INVOKABLE void setChapter(int ch) { m_mpv->setChapter(ch); }
    Q_INVOKABLE void setSpeed(double speed) { m_mpv->setSpeed(speed); }
    Q_INVOKABLE void setSlang(const QString &lang) { m_mpv->setSlang(lang); }
    Q_INVOKABLE void setAlang(const QString &lang) { m_mpv->setAlang(lang); }
    Q_INVOKABLE void addSubtitleFile(const QString &url,
                                      const QString &title = QString(),
                                      const QString &lang = QString(),
                                      bool select = true) { m_mpv->addSubtitleFile(url, title, lang, select); }
    Q_INVOKABLE void reportPlayStopped(qint64 ticks) { m_emby->reportPlaybackStop(m_currentPlayItemId, ticks); }
    Q_INVOKABLE void toggleFullscreen();
    void setRootWindow(QWindow *window);

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void volumeChanged();
    void playError(const QString &message);
    void endOfFile();
    void fullscreenChanged();
    void resumeProgressUpdated();
    void localPlaylistReady(QVariantList playlist);
    void currentItemDetailChanged();
    void speedChanged();
    void tracksChanged();
    void sidChanged();
    void chaptersChanged();
    void chapterChanged();

private:
    void connectMpvSignals();
    void initProgressTimer();
    void onProgressTimer();
    void updateCachedProgress(const QString &itemId, qint64 finalTicks);
    void reportStopForCurrent();
    QJsonArray streamsForSelectedSource() const;
    void fuzzySelectSubtitle();
    static double jaroWinkler(const QString &a, const QString &b);
    static QString langCodeToName(const QString &code);

    EmbyClient *m_emby;
    CacheStore *m_cache;
    SettingsStore *m_settings;
    MpvController *m_mpv;
    QPointer<QWindow> m_rootWindow;
    QTimer *m_progressTimer;
    QTimer *m_volumeSaveTimer;
    QString m_currentPlayItemId;
    QString m_currentPlaySessionId;
    QString m_currentMediaSourceId;
    int m_pendingSubIdx = -2;
    int m_playGeneration = 0;
    QJsonObject m_currentItemDetail;
};
