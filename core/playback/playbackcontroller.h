#pragma once
#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
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
    Q_PROPERTY(QJsonArray currentMediaSources READ currentMediaSources NOTIFY currentMediaSourcesChanged FINAL)
    Q_PROPERTY(QJsonArray currentPlaylist READ currentPlaylist NOTIFY currentPlaylistChanged FINAL)
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
    QJsonArray currentMediaSources() const { return m_currentMediaSources; }
    QJsonArray currentPlaylist() const { return m_currentPlaylist; }
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
    // 从卡片直接起播时用 —— 卡片只带了当前这一集, 播完接不上下一集。
    // 拉这一季的完整集列表填 currentPlaylist, 不经手 DetailManager
    // (那边的 m_currentSeriesId/m_episodeModel 是详情页的状态, 播放页写进去
    //  会把用户选的季冲掉, 两边还会抢同一组成员变量)
    Q_INVOKABLE void loadPlaylist(const QString &seriesId, const QString &seasonId);
    // 剧集海报上的播放钮用 —— 卡片只知道剧集 id, 不知道该从哪一集播。
    // 问服务器要 NextUp: 看过的给续播那一集, 没看过的给第一集。
    // 结果走 seriesEntryResolved (可能是空的 —— 那就交给 QML 决定怎么办)
    Q_INVOKABLE void resolveSeriesEntry(const QString &seriesId);
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
    void currentMediaSourcesChanged();
    void currentPlaylistChanged();
    void seriesEntryResolved(const QString &seriesId, const QJsonObject &episode);
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
    void onEpisodesFetched(const QJsonArray &episodes, const QString &seriesId,
                           const QString &seasonId);
    void onNextUpFetched(const QJsonObject &episode, const QString &seriesId);
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
    // 本次播放是否已成功 FILE_LOADED。未加载成功时 mpv 的 position 还是 0,
    // 拿它上报会把 Emby 上已有的续播进度抹掉 (m_hasVideo 不能当判据 —
    // 正常 EOF 结束时它也是 false, 会把该上报的也堵掉)
    bool m_fileLoaded = false;
    qint64 m_startTimeTicks = 0;
    // **只装完整详情, 或者空。** 不许往里塞半截对象 —— 它会被 updateCachedProgress()
    // 原样 putItemDetail() 写回缓存, 而那是整体覆盖, 一个没有 Id/Name/RunTimeTicks
    // 的壳会把完整详情顶掉 (内存 + SQLite 都是)
    QJsonObject m_currentItemDetail;
    // MediaSources 的唯一权威来源: 起播时从详情里取, PlaybackInfo 回来后覆盖成更新鲜的。
    // 单独放一份而不是塞进 m_currentItemDetail, 就是为了守住上面那条
    QJsonArray m_currentMediaSources;
    // loadPlaylist 请求身份。episodesFetched 是广播信号 (DetailManager 也连着),
    // 回来时必须拿这两个对上号才收 —— 否则详情页翻季时飞行中的响应会灌进播放列表
    QString m_playlistSeriesId;
    QString m_playlistSeasonId;
    QJsonArray m_currentPlaylist;
    // resolveSeriesEntry 的请求身份, 同理: nextUpFetched 也是广播信号
    QString m_pendingSeriesId;
};
