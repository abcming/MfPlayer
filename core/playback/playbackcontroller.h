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
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY fullscreenChanged)
    Q_PROPERTY(MpvController* mpv READ mpv CONSTANT)

public:
    explicit PlaybackController(EmbyClient *emby, CacheStore *cache, SettingsStore *settings, QObject *parent = nullptr);
    ~PlaybackController() override;

    double position() const;
    double duration() const;
    bool playing() const;
    int volume() const;
    bool fullscreen() const;
    MpvController *mpv() const { return m_mpv; }

public slots:
    void playItem(const QString &itemId, qint64 startTimeTicks = 0,
                  const QString &mediaSourceId = QString(),
                  int audioIndex = -1, int subtitleIndex = -2);
    void playLocalFile(const QString &filePath);
    Q_INVOKABLE QVariantList scanFolderForLocalPlaylist(const QString &filePath) const;
    void pause();
    void resume();
    void stop();
    void stopPlayback();  // called on logout (no report, just stop)
    void seek(double pos);
    void setVolume(int vol);
    Q_INVOKABLE void setHdrPeakBrightness(int nits);
    Q_INVOKABLE void setSid(int sid) { m_mpv->setSid(sid); }
    Q_INVOKABLE void setAid(int aid) { m_mpv->setAid(aid); }
    Q_INVOKABLE void setChapter(int ch) { m_mpv->setChapter(ch); }
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

private:
    void connectMpvSignals();
    void initProgressTimer();
    void onProgressTimer();
    void updateCachedProgress(const QString &itemId, qint64 finalTicks);
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
