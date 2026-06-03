#pragma once
#include <QObject>
#include <QString>
#include <QVariant>
#include <QVariantList>

#include <atomic>

struct mpv_handle;
struct mpv_render_context;
struct mpv_node;
struct mpv_event_property;
class QQuickWindow;

class MpvController : public QObject {
    Q_OBJECT
    Q_PROPERTY(double position READ position NOTIFY positionChanged FINAL)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged FINAL)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged FINAL)
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged FINAL)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY hasVideoChanged FINAL)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged FINAL)
    Q_PROPERTY(int currentSid READ currentSid NOTIFY sidChanged FINAL)
    Q_PROPERTY(int currentAid READ currentAid NOTIFY aidChanged FINAL)
    Q_PROPERTY(QVariantMap videoOutParams READ videoOutParams NOTIFY videoOutParamsChanged FINAL)
    Q_PROPERTY(QVariantMap videoParams READ videoParams NOTIFY videoParamsChanged FINAL)
    Q_PROPERTY(QVariantMap audioOutParams READ audioOutParams NOTIFY audioOutParamsChanged FINAL)
    Q_PROPERTY(QVariantMap stats READ stats NOTIFY statsChanged FINAL)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged FINAL)
    Q_PROPERTY(int currentChapter READ currentChapter NOTIFY chapterChanged FINAL)
    Q_PROPERTY(double speed READ speed NOTIFY speedChanged FINAL)
    Q_PROPERTY(QString mpvVersion READ mpvVersion CONSTANT)

public:
    explicit MpvController(QObject *parent = nullptr);
    ~MpvController() override;

    Q_INVOKABLE void play(const QString &url, const QString &referrer = QString(), double startSeconds = 0);
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double pos);
    Q_INVOKABLE void setVolume(int vol);
    Q_INVOKABLE void setSid(int sid);
    Q_INVOKABLE void setAid(int aid);
    Q_INVOKABLE void setSlang(const QString &lang);
    Q_INVOKABLE void setAlang(const QString &lang);
    Q_INVOKABLE void addSubtitleFile(const QString &url,
                                      const QString &title = QString(),
                                      const QString &lang = QString(),
                                      bool select = true);
    Q_INVOKABLE void setChapter(int ch);
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void setTargetPeak(int nits);
    Q_INVOKABLE void toggleStats();
    /// Called after HDR swapchain detection. Sets target-trc/target-prim
    /// to pq+bt.2020 (HDR) or srgb+bt.709 (SDR). Peak is left untouched.
    Q_INVOKABLE void updateHdrDisplayActive(bool active);

    double position() const;
    double duration() const;
    bool playing() const;
    int volume() const;
    bool hasVideo() const;
    QVariantList tracks() const { return m_tracks; }
    int currentSid() const { return m_sid; }
    int currentAid() const { return m_aid; }
    QVariantMap videoOutParams() const { return m_videoOutParams; }
    QVariantMap videoParams() const { return m_videoParams; }
    QVariantMap audioOutParams() const { return m_audioOutParams; }
    QVariantMap stats() const { return m_stats; }
    QVariantList chapters() const { return m_chapters; }
    int currentChapter() const { return m_currentChapter; }
    double speed() const { return m_speed; }
    QString mpvVersion() const { return m_mpvVersion; }

    mpv_handle *handle() const { return m_mpv; }
    mpv_render_context *renderCtx() const { return m_renderCtx; }

    bool ensureRenderCtx(QQuickWindow *window);

signals:
    void positionChanged();
    void durationChanged();
    void playingChanged();
    void volumeChanged();
    void hasVideoChanged();
    void tracksChanged();
    void sidChanged();
    void aidChanged();
    void videoOutParamsChanged();
    void videoParamsChanged();
    void audioOutParamsChanged();
    void statsChanged();
    void chaptersChanged();
    void chapterChanged();
    void endOfFile();
    void renderUpdateNeeded();
    void errorOccurred(const QString &message);
    void fileLoaded();
    void speedChanged();

private:
    void onMpvEvents();
    bool handleStatsProperty(const char *name, mpv_event_property *prop, bool &statsDirty);
    bool handleNodeProperty(const char *name, mpv_event_property *prop,
                            bool &videoOutDirty, bool &videoDirty, bool &audioOutDirty);
    void observeStatsProperties(bool observe);
    static void wakeup(void *ctx);
    static QVariant mpvNodeToVariant(const mpv_node *node);

    // ── Thread affinity ─────────────────────────────────────────────
    // ALL member variables below MUST be accessed ONLY from the main thread,
    // unless explicitly marked otherwise.
    //
    // wakeup() and the mpv render update callback run on mpv's internal
    // threads, but marshal work to the main thread via:
    //   - wakeup()      → QMetaObject::invokeMethod(Qt::QueuedConnection) → onMpvEvents()
    //   - update callback → emit renderUpdateNeeded (auto QueuedConnection) → MpvRenderItem
    //
    // m_hasVideo is the ONLY member safe to read from any thread (std::atomic).
    // m_mpv handle: mpv API functions are thread-safe; all our calls are from main thread.
    // m_renderCtx: created lazily from VideoRenderNode::prepare() (render thread),
    //   but write-once (never mutated after creation) and torn down in ~MpvController
    //   under s_renderMutex.

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_renderCtx = nullptr;
    double m_position = 0;
    double m_duration = 0;
    bool m_playing = false;
    std::atomic<bool> m_hasVideo{false};
    int m_volume = 80;
    QVariantList m_tracks;
    int m_sid = -1;
    int m_aid = -1;
    QVariantMap m_videoOutParams;
    QVariantMap m_videoParams;
    QVariantMap m_audioOutParams;
    QVariantMap m_stats;
    QVariantList m_chapters;
    int m_currentChapter = -1;
    double m_pendingStartSeconds = 0;
    double m_speed = 1.0;
    QString m_mpvVersion;
    bool m_statsObserving = false;
};
