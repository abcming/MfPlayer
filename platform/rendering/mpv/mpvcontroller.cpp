#include "common/version.h"
#include "platform/rendering/mpv/mpvcontroller.h"
#include "platform/rendering/mpv/mpvrenderitem.h"

extern "C" {
#include <mpv/client.h>
#include <mpv/render_gl.h>
#ifdef Q_OS_WIN
#include <mpv/render_d3d11.h>
#endif
}

#include <QOpenGLContext>
#include <QQuickWindow>
#include <QGuiApplication>
#ifndef Q_OS_WIN
#include <qpa/qplatformnativeinterface.h>
#endif
#ifdef Q_OS_WIN
#include <rhi/qrhi_platform.h>
#endif
#include <QDebug>
#include <QMutex>
#include <QMutexLocker>
#include <cstring>

MpvController::MpvController(QObject *parent)
    : QObject(parent)
{
    m_mpv = mpv_create();
    if (!m_mpv) {
        qWarning() << "MpvController: mpv_create failed";
        return;
    }

    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "profile", "high-quality");
    mpv_set_option_string(m_mpv, "cscale", "catmull_rom");
    mpv_set_option_string(m_mpv, "target-trc", "pq");
    mpv_set_option_string(m_mpv, "target-prim", "bt.2020");
    mpv_set_option_string(m_mpv, "target-peak", "1000");
    mpv_set_option_string(m_mpv, "hwdec", "no");
    mpv_set_option_string(m_mpv, "video-sync", "display-resample");
    mpv_set_option_string(m_mpv, "interpolation", "yes");
    mpv_set_option_string(m_mpv, "tscale", "oversample");
    mpv_set_option_string(m_mpv, "deband", "yes");
    mpv_set_option_string(m_mpv, "blend-subtitles", "video");
    mpv_set_option_string(m_mpv, "sub-auto", "fuzzy");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "demuxer-max-bytes", "500MiB");
    mpv_set_option_string(m_mpv, "demuxer-max-back-bytes", "250MiB");
    mpv_set_option_string(
        m_mpv,
        "stream-lavf-o",
        "timeout=30000000,reconnect=1,reconnect_at_eof=1,reconnect_streamed=1,reconnect_delay_max=5"
    );
    mpv_set_option_string(m_mpv, "force-seekable", "yes");

    mpv_set_option_string(m_mpv, "user-agent", MFPLAYER_USER_AGENT);

    if (mpv_initialize(m_mpv) < 0) {
        qWarning() << "MpvController: mpv_initialize failed";
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }

    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, 0, "volume", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "sid", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "aid", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "video-out-params", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "video-params", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "video-params/colormatrix", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "video-params/colorprim", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "video-params/colortransfer", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "audio-out-params", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "video-codec", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "audio-codec", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "container-fps", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "cache-total", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "demuxer-cache-duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "hwdec-current", MPV_FORMAT_STRING);
    mpv_observe_property(m_mpv, 0, "chapter-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, 0, "chapter", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE);

    m_mpvVersion = QString::fromUtf8(mpv_get_property_string(m_mpv, "mpv-version"));
    mpv_set_wakeup_callback(m_mpv, wakeup, this);
}

MpvController::~MpvController() {
    // Serialize with VideoRenderNode::render() on the render thread.
    // The lock ensures no render thread is mid-frame when we free the context.
    {
        QMutexLocker lock(&VideoRenderNode::renderMutex());
        VideoRenderNode::detachController(this);
        if (m_renderCtx) {
            mpv_render_context_set_update_callback(m_renderCtx, nullptr, nullptr);
            mpv_render_context_free(m_renderCtx);
            m_renderCtx = nullptr;
        }
    }
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
}

bool MpvController::ensureRenderCtx(QQuickWindow *window) {
    if (m_renderCtx)
        return true;
    if (!m_mpv || !window)
        return false;

#ifdef Q_OS_WIN
    // D3D11 render API backend
    QRhi *rhi = window->rhi();
    if (!rhi) {
        qWarning() << "MpvController: no QRhi available on window";
        return false;
    }
    auto *native = static_cast<const QRhiD3D11NativeHandles *>(rhi->nativeHandles());
    if (!native || !native->dev) {
        qWarning() << "MpvController: failed to get D3D11 device from QRhi";
        return false;
    }

    qDebug() << "MpvController: creating D3D11 render context...";
    mpv_d3d11_init_params d3d11p{};
    d3d11p.device = native->dev;

    mpv_render_param params[5];
    int i = 0;
    params[i++] = {MPV_RENDER_PARAM_API_TYPE,
                   const_cast<char *>(MPV_RENDER_API_TYPE_D3D11)};
    params[i++] = {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &d3d11p};
    params[i++] = {MPV_RENDER_PARAM_BACKEND,
                   const_cast<char *>("gpu-next")};
    params[i++] = {MPV_RENDER_PARAM_INVALID, nullptr};

    int ret = mpv_render_context_create(&m_renderCtx, m_mpv, params);
    if (ret >= 0) {
        mpv_render_context_set_update_callback(m_renderCtx, [](void *ctx) {
            auto *ctrl = static_cast<MpvController *>(ctx);
            emit ctrl->renderUpdateNeeded();
        }, this);
        qDebug() << "MpvController: D3D11 render context OK";
        return true;
    }
    qWarning() << "MpvController: D3D11 render context failed:" << ret;
    return false;

#else
    // OpenGL render API backend
    qDebug() << "MpvController: creating render context...";
    auto glAddr = [](void *ctx, const char *name) -> void * {
        Q_UNUSED(ctx);
        return reinterpret_cast<void *>(
            QOpenGLContext::currentContext()->getProcAddress(name));
    };
    mpv_opengl_init_params glp{glAddr, nullptr};

    mpv_render_param params[5];
    int i = 0;
    params[i++] = {MPV_RENDER_PARAM_API_TYPE,
                   const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)};
    params[i++] = {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glp};

    // Pass X11/Wayland display for hardware decoding interop and color management.
#ifndef Q_OS_WIN
    auto *pn = QGuiApplication::platformNativeInterface();
    void *x11Display = pn ? pn->nativeResourceForIntegration("display") : nullptr;
    void *wlDisplay = pn ? pn->nativeResourceForIntegration("wldisplay") : nullptr;
    if (x11Display) {
        params[i++] = {MPV_RENDER_PARAM_X11_DISPLAY, x11Display};
        qDebug() << "MpvController: using X11 display for render context";
    } else if (wlDisplay) {
        params[i++] = {MPV_RENDER_PARAM_WL_DISPLAY, wlDisplay};
        qDebug() << "MpvController: using Wayland display for render context";
    }
#endif

    params[i++] = {MPV_RENDER_PARAM_INVALID, nullptr};
    int ret = mpv_render_context_create(&m_renderCtx, m_mpv, params);
    if (ret >= 0) {
        mpv_render_context_set_update_callback(m_renderCtx, [](void *ctx) {
            auto *ctrl = static_cast<MpvController *>(ctx);
            emit ctrl->renderUpdateNeeded();
        }, this);
        qDebug() << "MpvController: render context OK";
        return true;
    }
    qWarning() << "MpvController: render context failed:" << ret;
    return false;
#endif
}

double MpvController::position() const { return m_position; }
double MpvController::duration() const { return m_duration; }
bool MpvController::playing() const { return m_playing; }
int MpvController::volume() const { return m_volume; }
bool MpvController::hasVideo() const { return m_hasVideo; }

void MpvController::play(const QString &url, const QString &referrer, double startSeconds) {
    if (!m_mpv) return;
    qDebug() << "MpvController: loading" << url << "startSeconds:" << startSeconds;
    if (!referrer.isEmpty()) {
        const QByteArray refUtf8 = referrer.toUtf8();
        const char *refData = refUtf8.constData();
        mpv_set_property_async(m_mpv, 0, "referrer", MPV_FORMAT_STRING, &refData);
    }
    m_pendingStartSeconds = startSeconds;
    const QByteArray urlUtf8 = url.toUtf8();
    const char *args[] = {"loadfile", urlUtf8.constData(), nullptr};
    mpv_command_async(m_mpv, 0, args);
    // keep-open=yes leaves pause=yes after EOF — explicitly unpause
    int flag = 0;
    mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
    m_hasVideo = true;
    m_playing = true;
    m_position = 0;
    m_duration = 0;
    emit hasVideoChanged();
    emit playingChanged();
    emit positionChanged();
    emit durationChanged();
}

void MpvController::setChapter(int ch) {
    if (!m_mpv) return;
    int64_t v = ch;
    mpv_set_property_async(m_mpv, 0, "chapter", MPV_FORMAT_INT64, &v);
}

void MpvController::setSpeed(double speed) {
    if (!m_mpv || speed == m_speed) return;
    mpv_set_property_async(m_mpv, 0, "speed", MPV_FORMAT_DOUBLE, &speed);
}

void MpvController::setTargetPeak(int nits) {
    if (!m_mpv) return;
    QByteArray val = QByteArray::number(nits);
    const char *data = val.constData();
    mpv_set_property_async(m_mpv, 0, "target-peak", MPV_FORMAT_STRING, &data);
}

void MpvController::pause() {
    if (m_mpv) {
        int flag = 1;
        mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
    }
}

void MpvController::resume() {
    if (m_mpv) {
        int flag = 0;
        mpv_set_property_async(m_mpv, 0, "pause", MPV_FORMAT_FLAG, &flag);
    }
}

void MpvController::stop() {
    if (!m_mpv) return;
    const char *args[] = {"stop", nullptr};
    mpv_command_async(m_mpv, 0, args);
    m_hasVideo = false;
    m_playing = false;
    m_position = 0;
    m_duration = 0;
    m_pendingStartSeconds = 0;
    m_tracks.clear();
    m_sid = -1;
    m_aid = -1;
    m_videoOutParams.clear();
    m_videoParams.clear();
    m_audioOutParams.clear();
    m_stats.clear();
    m_chapters.clear();
    m_currentChapter = -1;
    emit hasVideoChanged();
    emit playingChanged();
    emit positionChanged();
    emit durationChanged();
    emit tracksChanged();
    emit sidChanged();
    emit aidChanged();
    emit chaptersChanged();
    emit chapterChanged();
    emit videoOutParamsChanged();
    emit videoParamsChanged();
    emit audioOutParamsChanged();
    emit statsChanged();
}

void MpvController::seek(double pos) {
    if (!m_mpv) return;
    const QByteArray target = QByteArray::number(pos, 'f', 3);
    const char *args[] = {"seek", target.constData(), "absolute", nullptr};
    mpv_command_async(m_mpv, 0, args);
}

void MpvController::setVolume(int vol) {
    if (!m_mpv || vol == m_volume) return;
    int64_t v = vol;
    // Async: first call may trigger WASAPI lazy init (~100ms); must not block UI.
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_INT64, &v);
}

void MpvController::setSid(int sid) {
    if (!m_mpv) return;
    std::string s = (sid == -2) ? "no" : std::to_string(sid);
    const char *v = s.c_str();
    mpv_set_property_async(m_mpv, 0, "sid", MPV_FORMAT_STRING, &v);
}

void MpvController::setAid(int aid) {
    if (!m_mpv) return;
    std::string s = std::to_string(aid);
    const char *v = s.c_str();
    mpv_set_property_async(m_mpv, 0, "aid", MPV_FORMAT_STRING, &v);
}

void MpvController::setSlang(const QString &lang) {
    if (!m_mpv) return;
    QByteArray utf8 = lang.toUtf8();
    const char *data = utf8.constData();
    mpv_set_property_async(m_mpv, 0, "slang", MPV_FORMAT_STRING, &data);
}

void MpvController::setAlang(const QString &lang) {
    if (!m_mpv) return;
    QByteArray utf8 = lang.toUtf8();
    const char *data = utf8.constData();
    mpv_set_property_async(m_mpv, 0, "alang", MPV_FORMAT_STRING, &data);
}

void MpvController::addSubtitleFile(const QString &url,
                                     const QString &title,
                                     const QString &lang,
                                     bool select) {
    if (!m_mpv) return;
    QByteArray urlUtf8  = url.toUtf8();
    QByteArray titleUtf8 = title.toUtf8();
    QByteArray langUtf8  = lang.toUtf8();

    const char *args[7];
    int n = 0;
    args[n++] = "sub-add";
    args[n++] = urlUtf8.constData();
    args[n++] = select ? "select" : "";
    if (!title.isEmpty()) {
        args[n++] = titleUtf8.constData();
        if (!lang.isEmpty())
            args[n++] = langUtf8.constData();
    }
    args[n] = nullptr;
    mpv_command_async(m_mpv, 0, args);
}

void MpvController::toggleStats() {
    if (!m_mpv) return;
    mpv_command_string(m_mpv, "script-binding stats/display-stats-toggle");
}

void MpvController::observeStatsProperties(bool observe) {
    if (!m_mpv || observe == m_statsObserving) return;
    m_statsObserving = observe;
    // Use unique reply_userdata (0x50xx) so we can unobserve selectively
    constexpr uint64_t STATS_BASE = 0x5000;
    if (observe) {
        mpv_observe_property(m_mpv, STATS_BASE + 0, "estimated-vf-fps", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_mpv, STATS_BASE + 1, "display-fps", MPV_FORMAT_DOUBLE);
        mpv_observe_property(m_mpv, STATS_BASE + 2, "decoder-frame-drop-count", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 3, "decoder-frame-delayed-count", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 4, "cache-speed", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 5, "cache-used", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 6, "packet-video-bitrate", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 7, "packet-audio-bitrate", MPV_FORMAT_INT64);
        mpv_observe_property(m_mpv, STATS_BASE + 8, "avsync", MPV_FORMAT_DOUBLE);
    } else {
        for (int i = 0; i < 9; ++i)
            mpv_unobserve_property(m_mpv, STATS_BASE + i);
    }
}

void MpvController::onMpvEvents() {
    if (!m_mpv) return;
    bool statsDirty = false;
    bool videoOutDirty = false;
    bool videoDirty = false;
    bool audioOutDirty = false;
    while (true) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
            break;

        switch (event->event_id) {
        case MPV_EVENT_PROPERTY_CHANGE: {
            auto *prop = static_cast<mpv_event_property *>(event->data);
            const char *name = prop->name;
            if (!strcmp(name, "time-pos") && prop->format == MPV_FORMAT_DOUBLE) {
                m_position = *static_cast<double *>(prop->data);
                emit positionChanged();
            } else if (!strcmp(name, "duration") && prop->format == MPV_FORMAT_DOUBLE) {
                m_duration = *static_cast<double *>(prop->data);
                emit durationChanged();
            } else if (!strcmp(name, "pause") && prop->format == MPV_FORMAT_FLAG) {
                m_playing = !*static_cast<int *>(prop->data);
                emit playingChanged();
            } else if (!strcmp(name, "volume") && prop->format == MPV_FORMAT_INT64) {
                m_volume = static_cast<int>(*static_cast<int64_t *>(prop->data));
                emit volumeChanged();
            } else if (!strcmp(name, "sid") && prop->format == MPV_FORMAT_INT64) {
                m_sid = static_cast<int>(*static_cast<int64_t *>(prop->data));
                emit sidChanged();
            } else if (!strcmp(name, "aid") && prop->format == MPV_FORMAT_INT64) {
                m_aid = static_cast<int>(*static_cast<int64_t *>(prop->data));
                emit aidChanged();
            } else if (!strcmp(name, "speed") && prop->format == MPV_FORMAT_DOUBLE) {
                m_speed = *static_cast<double *>(prop->data);
                emit speedChanged();
            } else if (!handleNodeProperty(name, prop, videoOutDirty, videoDirty, audioOutDirty)
                       && !handleStatsProperty(name, prop, statsDirty)) {
                // unhandled property
            }
            break;
        }
        case MPV_EVENT_FILE_LOADED: {
            m_playing = true;
            m_hasVideo = true;
            emit playingChanged();
            emit hasVideoChanged();
            emit fileLoaded();
            if (m_pendingStartSeconds > 0.001) {
                double pos = m_pendingStartSeconds;
                m_pendingStartSeconds = 0;
                QByteArray target = QByteArray::number(pos, 'f', 3);
                const char *args[] = {"seek", target.constData(), "absolute", nullptr};
                mpv_command_async(m_mpv, 0, args);
            }
            break;
        }
        case MPV_EVENT_END_FILE: {
            auto *ef = static_cast<mpv_event_end_file *>(event->data);
            if (ef->reason == MPV_END_FILE_REASON_EOF) {
                m_playing = false;
                m_hasVideo = false;
                emit playingChanged();
                emit hasVideoChanged();
                emit endOfFile();
            } else if (ef->error < 0) {
                m_playing = false;
                m_hasVideo = false;
                emit playingChanged();
                emit hasVideoChanged();
                emit errorOccurred(QString::fromUtf8(mpv_error_string(ef->error)));
            }
            break;
        }
        case MPV_EVENT_IDLE:
            m_playing = false;
            emit playingChanged();
            break;
        default:
            break;
        }
    }
    if (videoOutDirty) emit videoOutParamsChanged();
    if (videoDirty) emit videoParamsChanged();
    if (audioOutDirty) emit audioOutParamsChanged();
    if (statsDirty) emit statsChanged();
}

bool MpvController::handleStatsProperty(const char *name, mpv_event_property *prop, bool &statsDirty) {
    if (!strcmp(name, "video-codec") && prop->format == MPV_FORMAT_STRING) {
        m_stats["videoCodec"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "audio-codec") && prop->format == MPV_FORMAT_STRING) {
        m_stats["audioCodec"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "container-fps") && prop->format == MPV_FORMAT_DOUBLE) {
        m_stats["containerFps"] = *static_cast<double *>(prop->data);
    } else if (!m_statsObserving) {
        return false;
    } else if (!strcmp(name, "estimated-vf-fps") && prop->format == MPV_FORMAT_DOUBLE) {
        m_stats["estimatedFps"] = *static_cast<double *>(prop->data);
    } else if (!strcmp(name, "display-fps") && prop->format == MPV_FORMAT_DOUBLE) {
        m_stats["displayFps"] = *static_cast<double *>(prop->data);
    } else if (!strcmp(name, "decoder-frame-drop-count") && prop->format == MPV_FORMAT_INT64) {
        m_stats["frameDrops"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "decoder-frame-delayed-count") && prop->format == MPV_FORMAT_INT64) {
        m_stats["frameDelayed"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "cache-speed") && prop->format == MPV_FORMAT_INT64) {
        m_stats["cacheSpeed"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "cache-used") && prop->format == MPV_FORMAT_INT64) {
        m_stats["cacheUsed"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "cache-total") && prop->format == MPV_FORMAT_INT64) {
        m_stats["cacheTotal"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "demuxer-cache-duration") && prop->format == MPV_FORMAT_DOUBLE) {
        m_stats["cacheDuration"] = *static_cast<double *>(prop->data);
    } else if (!strcmp(name, "packet-video-bitrate") && prop->format == MPV_FORMAT_INT64) {
        m_stats["videoBitrate"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "packet-audio-bitrate") && prop->format == MPV_FORMAT_INT64) {
        m_stats["audioBitrate"] = static_cast<qint64>(*static_cast<int64_t *>(prop->data));
    } else if (!strcmp(name, "hwdec-current") && prop->format == MPV_FORMAT_STRING) {
        m_stats["hwdec"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "video-params/colormatrix") && prop->format == MPV_FORMAT_STRING) {
        m_stats["srcColorMatrix"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "video-params/colorprim") && prop->format == MPV_FORMAT_STRING) {
        m_stats["srcColorPrim"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "video-params/colortransfer") && prop->format == MPV_FORMAT_STRING) {
        m_stats["srcColorTransfer"] = QString::fromUtf8(*static_cast<char **>(prop->data));
    } else if (!strcmp(name, "avsync") && prop->format == MPV_FORMAT_DOUBLE) {
        m_stats["avsync"] = *static_cast<double *>(prop->data);
    } else {
        return false;
    }
    statsDirty = true;
    return true;
}

bool MpvController::handleNodeProperty(const char *name, mpv_event_property *prop,
                                        bool &videoOutDirty, bool &videoDirty, bool &audioOutDirty) {
    if (!strcmp(name, "track-list") && prop->format == MPV_FORMAT_NODE) {
        auto *node = static_cast<mpv_node *>(prop->data);
        QVariantList all = mpvNodeToVariant(node).toList();
        m_tracks.clear();
        for (const QVariant &t : all) {
            QVariantMap m = t.toMap();
            QString ty = m["type"].toString();
            if (ty == "audio" || ty == "sub")
                m_tracks.append(m);
        }
        emit tracksChanged();
    } else if (!strcmp(name, "video-out-params") && prop->format == MPV_FORMAT_NODE) {
        auto *node = static_cast<mpv_node *>(prop->data);
        m_videoOutParams = mpvNodeToVariant(node).toMap();
        videoOutDirty = true;
    } else if (!strcmp(name, "video-params") && prop->format == MPV_FORMAT_NODE) {
        auto *node = static_cast<mpv_node *>(prop->data);
        m_videoParams = mpvNodeToVariant(node).toMap();
        videoDirty = true;
    } else if (!strcmp(name, "audio-out-params") && prop->format == MPV_FORMAT_NODE) {
        auto *node = static_cast<mpv_node *>(prop->data);
        m_audioOutParams = mpvNodeToVariant(node).toMap();
        audioOutDirty = true;
    } else if (!strcmp(name, "chapter-list") && prop->format == MPV_FORMAT_NODE) {
        auto *node = static_cast<mpv_node *>(prop->data);
        QVariantList raw = mpvNodeToVariant(node).toList();
        m_chapters.clear();
        for (const QVariant &v : raw) {
            QVariantMap c = v.toMap();
            m_chapters.append(c);
        }
        emit chaptersChanged();
    } else if (!strcmp(name, "chapter") && prop->format == MPV_FORMAT_INT64) {
        m_currentChapter = static_cast<int>(*static_cast<int64_t *>(prop->data));
        emit chapterChanged();
    } else {
        return false;
    }
    return true;
}

QVariant MpvController::mpvNodeToVariant(const mpv_node *node) {
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_INT64:
        return QVariant::fromValue(static_cast<qint64>(node->u.int64));
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_FLAG:
        return QVariant::fromValue(static_cast<bool>(node->u.flag));
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; ++i)
            list.append(mpvNodeToVariant(&node->u.list->values[i]));
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; ++i) {
            QString key = QString::fromUtf8(node->u.list->keys[i]);
            map.insert(key, mpvNodeToVariant(&node->u.list->values[i]));
        }
        return map;
    }
    default:
        return {};
    }
}

void MpvController::wakeup(void *ctx) {
    QMetaObject::invokeMethod(static_cast<MpvController *>(ctx),
                              &MpvController::onMpvEvents,
                              Qt::QueuedConnection);
}
