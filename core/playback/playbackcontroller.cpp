#include "core/playback/playbackcontroller.h"
#include "common/constants.h"
#include "core/io_pool.h"
#include <QDir>
#include <QFileInfo>
#include <QQuickWindow>

PlaybackController::PlaybackController(EmbyClient *emby, CacheStore *cache,
                                       SettingsStore *settings, QObject *parent)
    : QObject(parent)
    , m_emby(emby)
    , m_cache(cache)
    , m_settings(settings)
    , m_mpv(new MpvController(this))
    , m_progressTimer(new QTimer(this))
{
    connectMpvSignals();

    m_mpv->setVolume(m_settings->volume());
    m_mpv->setTargetPeak(m_settings->hdrPeakBrightness());
    m_volumeSaveTimer = new QTimer(this);
    m_volumeSaveTimer->setSingleShot(true);
    m_volumeSaveTimer->setInterval(500);
    connect(m_volumeSaveTimer, &QTimer::timeout, this, [this]() {
        m_settings->setVolume(m_mpv->volume());
    });
    connect(this, &PlaybackController::volumeChanged, this, [this]() {
        m_volumeSaveTimer->start();
    });

    initProgressTimer();
}

PlaybackController::~PlaybackController() {
    stop();
}

double PlaybackController::position() const { return m_mpv->position(); }
double PlaybackController::duration() const { return m_mpv->duration(); }
bool PlaybackController::playing() const { return m_mpv->playing(); }
int PlaybackController::volume() const { return m_mpv->volume(); }

void PlaybackController::connectMpvSignals() {
    connect(m_mpv, &MpvController::positionChanged, this, &PlaybackController::positionChanged);
    connect(m_mpv, &MpvController::durationChanged, this, &PlaybackController::durationChanged);
    connect(m_mpv, &MpvController::playingChanged, this, &PlaybackController::playingChanged);
    connect(m_mpv, &MpvController::volumeChanged, this, &PlaybackController::volumeChanged);
    connect(m_mpv, &MpvController::fileLoaded, this, [this]() { m_fileLoaded = true; });
    connect(m_mpv, &MpvController::endOfFile, this, &PlaybackController::endOfFile);
    connect(m_mpv, &MpvController::errorOccurred, this, &PlaybackController::playError);
    connect(m_mpv, &MpvController::speedChanged, this, &PlaybackController::speedChanged);
    connect(m_mpv, &MpvController::tracksChanged, this, &PlaybackController::tracksChanged);
    connect(m_mpv, &MpvController::sidChanged, this, &PlaybackController::sidChanged);
    connect(m_mpv, &MpvController::chaptersChanged, this, &PlaybackController::chaptersChanged);
    connect(m_mpv, &MpvController::chapterChanged, this, &PlaybackController::chapterChanged);

    // Load external subtitles after file is ready
    connect(m_mpv, &MpvController::fileLoaded, this, [this]() {
        QJsonArray streams = streamsForSelectedSource();

        // Add ALL external subtitles up front so the player menu is complete
        // and switching is instant. At most one carries the select flag —
        // bulk-adding with select on each would let the last-added track win.
        for (const QJsonValue &s : streams) {
            QJsonObject stream = s.toObject();
            if (stream["Type"].toString() != Constants::kStreamTypeSubtitle) continue;
            if (!stream["IsExternal"].toBool()) continue;

            QString deliveryUrl = stream["DeliveryUrl"].toString();
            QString fullUrl;
            if (!deliveryUrl.isEmpty() && deliveryUrl != "null") {
                fullUrl = deliveryUrl.startsWith('/')
                    ? m_emby->serverUrl() + deliveryUrl
                    : deliveryUrl;
            } else if (m_currentMediaSourceId.isEmpty()) {
                continue;  // no media source, can't construct subtitle URL
            } else {
                // m_currentPlayItemId 而非 m_currentItemDetail["Id"]: 详情未预缓存时
                // 后者整个是空的 (MediaSources 现在单独放 m_currentMediaSources)
                QString idx = QString::number(stream["Index"].toInt());
                QString codec = stream["Codec"].toString().toLower();
                fullUrl = m_emby->serverUrl() +
                    "/Videos/" + m_currentPlayItemId + "/" + m_currentMediaSourceId +
                    "/Subtitles/" + idx + "/0/Stream." + codec;
            }
            if (!fullUrl.contains("api_key"))
                fullUrl += QString(fullUrl.contains('?') ? "&" : "?") +
                           "api_key=" + m_emby->accessToken();
            const bool select = stream["Index"].toInt() == m_pendingSubIdx;
            m_mpv->addSubtitleFile(fullUrl,
                stream["DisplayTitle"].toString(),
                stream["Language"].toString(), select);
        }

        // Fuzzy subtitle matching: when mpv's exact slang code match fails
        // (e.g. file has "chi" but user prefers "chs"), fall back to
        // Jaro-Winkler similarity on human-readable language names.
        if (m_pendingSubIdx == -1)
            fuzzySelectSubtitle();
    });
}

void PlaybackController::initProgressTimer() {
    m_progressTimer->setInterval(10000);
    connect(m_progressTimer, &QTimer::timeout, this, &PlaybackController::onProgressTimer);
    connect(this, &PlaybackController::endOfFile, this, [this]() {
        // 和 onProgressTimer 用同一道守卫。换片窗口里 (playItem 已把 id 切到 B,
        // 但 B 的 PlaybackInfo 还没回来、还没 loadfile) 到来的 EOF 一定是**A 的** ——
        // A 在等待期间自己播完了。而 A 已经在 playItem 开头结算过。
        //
        // 不拦的话: 用 B 的 itemId + A 的 sessionId + B 的起播位置发一个 stop,
        // 然后 clear() 掉 itemId —— 之后 B 整场的周期进度和最终 stop 全部静默跳过,
        // 表现是"这一集看完了但进度没记住"。
        //
        // endOfFile 只从两条"正常播完"的路径发出 (eof-reached 边沿 /
        // END_FILE reason==EOF), 加载失败走的是 errorOccurred, 所以这道守卫
        // 不会吞掉本该上报的结算。
        if (!m_fileLoaded) return;
        reportStopForCurrent();
        m_progressTimer->stop();
    });
}

// 上报当前 Emby 条目的最终进度并结束其 PlaySession。所有会替换/结束当前
// 播放的入口 (stop / playItem 换片 / playLocalFile / endOfFile) 都必须先走
// 这里, 否则上一集的观看进度不落 Emby。
void PlaybackController::reportStopForCurrent() {
    if (m_currentPlayItemId.isEmpty()) return;
    // 加载失败/加载中就退出时 mpv 从未打开过文件, position 是 0 —— 用它上报会
    // 把服务端和本地缓存里的续播进度一起抹成 0。这种情况按起播位置上报:
    // 进度写回原值 (等于没动), PlaySession 也能正常关闭。
    qint64 finalTicks = m_fileLoaded
        ? static_cast<qint64>(m_mpv->position() * Constants::kTicksPerSecond)
        : m_startTimeTicks;
    m_emby->reportPlaybackStop(m_currentPlayItemId, finalTicks,
        m_currentPlaySessionId, m_currentMediaSourceId);
    updateCachedProgress(m_currentPlayItemId, finalTicks);
    m_currentPlayItemId.clear();
}

void PlaybackController::playItem(const QString &itemId, qint64 startTimeTicks,
                                  const QString &mediaSourceId,
                                  int audioIndex, int subtitleIndex) {
    // 手动切集/切版本时结束上一个 PlaySession, 观看进度才会落 Emby
    // (自动连播路径无影响: endOfFile 处理器已上报并清空了 itemId)
    reportStopForCurrent();
    // 顺序要紧: 上面结算的是"上一个" item, 重置必须排在它后面
    m_fileLoaded = false;
    m_startTimeTicks = startTimeTicks;
    m_currentPlayItemId = itemId;
    m_currentItemDetail = m_cache->getItemDetail(itemId);
    m_currentMediaSources = m_currentItemDetail["MediaSources"].toArray();
    int gen = ++m_playGeneration;

    // Resolve MediaSourceId from current item if not provided and available
    QString srcId = mediaSourceId;
    if (srcId.isEmpty() && !m_currentMediaSources.isEmpty())
        srcId = m_currentMediaSources.first().toObject()["Id"].toString();
    m_currentMediaSourceId = srcId;
    m_pendingSubIdx = subtitleIndex;

    m_emby->fetchPlaybackInfo(itemId,
        [this, itemId, startTimeTicks, gen, audioIndex, subtitleIndex]
        (const QString &streamUrl, const QString &playSessionId, const QJsonArray &mediaSources) {
        if (gen != m_playGeneration) return;
        if (streamUrl.isEmpty()) {
            m_currentPlayItemId.clear();
            emit playError("无法获取播放地址或解析重定向失败");
            return;
        }
        // Populate MediaSources from fresh PlaybackInfo response so the
        // PlayerPage version selector works even when itemData was not
        // pre-cached (e.g. episodes played from a series page).
        //
        // 存进 m_currentMediaSources 而**不是** m_currentItemDetail —— 详情没预缓存时
        // 后者是空的, 往里塞 MediaSources 就得到一个没有 Id/Name/RunTimeTicks 的壳,
        // 而 updateCachedProgress() 会把它原样 putItemDetail() 写回, 整体覆盖掉
        // 缓存里的完整详情。分开放, "m_currentItemDetail 要么完整要么空"才立得住
        if (!mediaSources.isEmpty()) {
            m_currentMediaSources = mediaSources;
            // 详情未预缓存时 (播放页直接切集) playItem 里解析不到 srcId —
            // 用 PlaybackInfo 的第一个 source 补上。否则 streamsForSelectedSource()
            // 永远匹配不到, 外挂字幕全被跳过, 字幕菜单也退化成 mpv 原始轨道名
            if (m_currentMediaSourceId.isEmpty())
                m_currentMediaSourceId = mediaSources.first().toObject()["Id"].toString();
            emit currentMediaSourcesChanged();
        }
        m_currentPlaySessionId = playSessionId;
        // 上报与 loadfile 并行 — 起播不依赖 Emby 回执, 省一个网络往返
        m_emby->reportPlaybackStart(itemId, startTimeTicks, playSessionId, m_currentMediaSourceId);

        QString fullUrl = streamUrl.startsWith('/')
            ? m_emby->serverUrl() + streamUrl
            : streamUrl;

        // ── Language preferences BEFORE play() so mpv auto-selects correctly ──
        QJsonArray streams = streamsForSelectedSource();

        // Audio: look up language from Emby Index → set alang
        if (audioIndex >= 0) {
            for (const QJsonValue &v : streams) {
                QJsonObject st = v.toObject();
                if (st["Type"].toString() == Constants::kStreamTypeAudio
                    && st["Index"].toInt() == audioIndex) {
                    QString lang = st["Language"].toString();
                    if (!lang.isEmpty()) m_mpv->setAlang(lang);
                    break;
                }
            }
        }

        // Subtitle: precise pre-selection by mpv track id. Internal sub tracks
        // keep container order, so the Nth internal subtitle stream in
        // MediaStreams is mpv sid N+1 — slang can't distinguish e.g.
        // "English Forced" from "English SDH" (both "eng"). External picks are
        // selected via the sub-add select flag in the fileLoaded handler; sid
        // stays "no" until then so mpv doesn't flash an auto-picked track.
        if (subtitleIndex >= 0) {
            int internalPos = 0;
            bool matched = false, external = false;
            for (const QJsonValue &v : streams) {
                QJsonObject st = v.toObject();
                if (st["Type"].toString() != Constants::kStreamTypeSubtitle) continue;
                if (st["Index"].toInt() == subtitleIndex) {
                    matched = true;
                    external = st["IsExternal"].toBool();
                    break;
                }
                if (!st["IsExternal"].toBool()) ++internalPos;
            }
            if (matched)
                m_mpv->setSid(external ? -2 : internalPos + 1);
            else
                m_mpv->setSid(-1);  // stale index — fall back to auto
        } else {
            m_mpv->setSid(subtitleIndex == -2 ? -2 : -1);  // "no" / "auto"
        }

        m_mpv->play(fullUrl, m_emby->serverUrl(), startTimeTicks / static_cast<double>(Constants::kTicksPerSecond));
        m_progressTimer->start();
    }, m_currentMediaSourceId, m_pendingSubIdx);
}

void PlaybackController::playLocalFile(const QString &filePath) {
    ++m_playGeneration;  // cancel in-flight playItem callbacks (fetchPlaybackInfo)
    reportStopForCurrent();  // 若正在播 Emby 条目, 先结束其 PlaySession
    // Clear stale Emby playback state
    m_currentItemDetail = {};
    m_currentMediaSources = {};
    m_pendingSubIdx = -1;  // auto: let fuzzy matching decide

    // Reset sid to auto — a previous Emby play may have left a numeric sid
    m_mpv->setSid(-1);

    // Apply user language preferences so mpv auto-selects matching tracks
    QString al = m_settings->audioLanguage();
    if (!al.isEmpty()) m_mpv->setAlang(al);
    QString sl = m_settings->subtitleLanguage();
    if (!sl.isEmpty()) m_mpv->setSlang(sl);

    // External subtitles with matching names are loaded automatically by mpv
    // via sub-auto=fuzzy — no manual sub-add needed. Drag-and-drop subtitles
    // are handled by PlayerPage's DropArea calling addSubtitleFile() directly.

    m_mpv->play(filePath);
    // Don't report local file playback to Emby
    m_currentPlayItemId.clear();
    m_progressTimer->stop();
}

void PlaybackController::pause() { m_mpv->pause(); }
void PlaybackController::resume() { m_mpv->resume(); }
void PlaybackController::stop() {
    ++m_playGeneration;  // cancel any pending playItem callbacks
    m_currentItemDetail = {};
    m_currentMediaSources = {};
    reportStopForCurrent();
    m_progressTimer->stop();
    m_mpv->stop();
}

void PlaybackController::stopPlayback() {
    ++m_playGeneration;
    m_progressTimer->stop();
    m_mpv->stop();
    m_currentPlayItemId.clear();
}

void PlaybackController::seek(double pos) {
    m_mpv->seek(pos);
}

void PlaybackController::updateCachedProgress(const QString &itemId, qint64 finalTicks) {
    QJsonObject cached = m_currentItemDetail;
    if (cached.isEmpty()) cached = m_cache->getItemDetail(itemId);
    if (cached.isEmpty()) return;
    double totalTicks = cached["RunTimeTicks"].toDouble();
    double playedPct = totalTicks > 0 ? (static_cast<double>(finalTicks) / totalTicks * 100.0) : 0;
    // Merge into existing UserData — wholesale replacement would drop IsFavorite etc.
    QJsonObject ud = cached["UserData"].toObject();
    ud["PlaybackPositionTicks"] = static_cast<double>(finalTicks);
    ud["PlayedPercentage"] = playedPct;
    ud["Played"] = playedPct > 90;
    cached["UserData"] = ud;
    m_cache->putItemDetail(itemId, cached);
    emit resumeProgressUpdated();
}

void PlaybackController::setVolume(int vol) { m_mpv->setVolume(vol); }

void PlaybackController::setHdrPeakBrightness(int nits) {
    m_settings->setHdrPeakBrightness(nits);
    m_mpv->setTargetPeak(nits);
}

bool PlaybackController::fullscreen() const {
    return m_rootWindow && m_rootWindow->visibility() == QWindow::FullScreen;
}

void PlaybackController::setRootWindow(QWindow *window) {
    m_rootWindow = window;
    connect(window, &QWindow::visibilityChanged, this, &PlaybackController::fullscreenChanged);
}

void PlaybackController::toggleFullscreen() {
    if (!m_rootWindow) return;
    if (m_rootWindow->visibility() == QWindow::FullScreen)
        m_rootWindow->showNormal();
    else
        m_rootWindow->showFullScreen();
}

void PlaybackController::onProgressTimer() {
    // m_fileLoaded: play() 一发出 loadfile 就把 playing 置 true, 加载还没成功时
    // position 是 0 —— 少了这道守卫, 加载卡过 10 秒就会把进度上报成 0
    if (m_currentPlayItemId.isEmpty() || !m_fileLoaded || !m_mpv->playing()) return;
    qint64 ticks = static_cast<qint64>(m_mpv->position() * Constants::kTicksPerSecond);
    m_emby->reportPlaybackProgress(m_currentPlayItemId, ticks,
                                    m_currentPlaySessionId, m_currentMediaSourceId);
}

QString PlaybackController::subtitleTrackTitle(int sid) const {
    // Maps an mpv sub track id to the Emby DisplayTitle so the player menu
    // shows the same names as the detail page. mpv numbers internal sub
    // tracks in container order (1..N), externals follow in add order —
    // which matches MediaStreams order in both passes.
    if (sid < 1) return {};
    const QJsonArray streams = streamsForSelectedSource();
    int pos = 0;
    for (int pass = 0; pass < 2; ++pass) {
        for (const QJsonValue &v : streams) {
            QJsonObject st = v.toObject();
            if (st["Type"].toString() != Constants::kStreamTypeSubtitle) continue;
            if (st["IsExternal"].toBool() != (pass == 1)) continue;
            if (++pos == sid)
                return st["DisplayTitle"].toString();
        }
    }
    return {};
}

QJsonArray PlaybackController::streamsForSelectedSource() const {
    for (const QJsonValue &sv : m_currentMediaSources) {
        QJsonObject src = sv.toObject();
        if (src["Id"].toString() == m_currentMediaSourceId) {
            QJsonArray ms = src["MediaStreams"].toArray();
            if (!ms.isEmpty()) return ms;
        }
    }
    // 回落到详情自带的顶层 MediaStreams (详情没预缓存时为空, 和改动前一致)
    return m_currentItemDetail["MediaStreams"].toArray();
}

void PlaybackController::scanFolderForLocalPlaylistAsync(const QString &filePath) {
    // Offload filesystem enumeration to the I/O pool to avoid blocking UI thread.
    QPointer<PlaybackController> guard(this);
    ioPool().start([this, guard, filePath]() {
        QFileInfo fi(filePath);
        QDir dir = fi.absoluteDir();

        QString inputCanonical = fi.canonicalFilePath();
        if (inputCanonical.isEmpty())
            inputCanonical = fi.absoluteFilePath();

        QStringList videoFilters = {
            "*.mp4", "*.mkv", "*.avi", "*.mov", "*.wmv", "*.flv",
            "*.webm", "*.mpg", "*.mpeg", "*.m2ts", "*.ts", "*.m4v",
            "*.3gp", "*.ogv"
        };
        dir.setNameFilters(videoFilters);
        dir.setSorting(QDir::Name | QDir::LocaleAware);
        QFileInfoList files = dir.entryInfoList(QDir::Files);

        QVariantList playlist;
        for (int i = 0; i < files.size(); ++i) {
            QVariantMap item;
            item["localFile"] = files[i].absoluteFilePath();
            item["itemName"] = files[i].fileName();
            item["indexNumber"] = i + 1;

            QString fCanonical = files[i].canonicalFilePath();
            if (fCanonical.isEmpty())
                fCanonical = files[i].absoluteFilePath();
            if (fCanonical == inputCanonical)
                item["isCurrent"] = true;

            playlist.append(item);
        }

        // Deliver result back to main thread
        QMetaObject::invokeMethod(this, [this, guard, playlist]() {
            if (!guard) return;
            emit localPlaylistReady(playlist);
        }, Qt::QueuedConnection);
    });
}

// ── Jaro-Winkler fuzzy string matching ──
double PlaybackController::jaroWinkler(const QString &a, const QString &b) {
    if (a == b) return 1.0;
    int la = a.length(), lb = b.length();
    if (la == 0 || lb == 0) return 0.0;

    int matchDist = (std::max)(la, lb) / 2 - 1;
    if (matchDist < 0) matchDist = 0;

    QVector<bool> ma(la, false), mb(lb, false);
    int matches = 0;
    for (int i = 0; i < la; i++) {
        int start = (std::max)(0, i - matchDist);
        int end   = (std::min)(lb, i + matchDist + 1);
        for (int j = start; j < end; j++) {
            if (mb[j]) continue;
            if (a[i].toLower() != b[j].toLower()) continue;
            ma[i] = mb[j] = true;
            matches++;
            break;
        }
    }
    if (matches == 0) return 0.0;

    double trans = 0;
    int k = 0;
    for (int i = 0; i < la; i++) {
        if (!ma[i]) continue;
        while (!mb[k]) k++;
        if (a[i].toLower() != b[k].toLower()) trans++;
        k++;
    }

    double jaro = (matches / static_cast<double>(la)
                   + matches / static_cast<double>(lb)
                   + (matches - trans / 2.0) / matches) / 3.0;

    int prefix = 0;
    for (int i = 0; i < (std::min)(4, (std::min)(la, lb)); i++) {
        if (a[i].toLower() == b[i].toLower()) prefix++;
        else break;
    }
    return jaro + prefix * 0.1 * (1.0 - jaro);
}

// ── ISO 639 code → human-readable language name ──
static const QHash<QString, QString> &langMap() {
    static const QHash<QString, QString> m = {
        {"chs","Chinese Simplified"}, {"zho","Chinese Simplified"},
        {"zh","Chinese Simplified"},  {"zh-cn","Chinese Simplified"},
        {"zh-hans","Chinese Simplified"},
        {"cht","Chinese Traditional"}, {"zh-tw","Chinese Traditional"},
        {"zh-hant","Chinese Traditional"},
        {"chi","Chinese"},
        {"eng","English"}, {"en","English"},
        {"jpn","Japanese"}, {"ja","Japanese"},
        {"kor","Korean"}, {"ko","Korean"},
        {"fre","French"}, {"fra","French"}, {"fr","French"},
        {"ger","German"}, {"deu","German"}, {"de","German"},
        {"spa","Spanish"}, {"es","Spanish"},
        {"ita","Italian"}, {"it","Italian"},
        {"por","Portuguese"}, {"pt","Portuguese"},
        {"rus","Russian"}, {"ru","Russian"},
        {"ara","Arabic"}, {"ar","Arabic"},
        {"nob","Norwegian"}, {"nor","Norwegian"}, {"no","Norwegian"},
        {"swe","Swedish"}, {"sv","Swedish"},
        {"dan","Danish"}, {"da","Danish"},
        {"fin","Finnish"}, {"fi","Finnish"},
        {"dut","Dutch"}, {"nld","Dutch"}, {"nl","Dutch"},
        {"pol","Polish"}, {"pl","Polish"},
        {"tur","Turkish"}, {"tr","Turkish"},
        {"hin","Hindi"}, {"hi","Hindi"},
        {"tha","Thai"}, {"th","Thai"},
        {"vie","Vietnamese"}, {"vi","Vietnamese"},
        {"ind","Indonesian"}, {"id","Indonesian"},
        {"may","Malay"}, {"msa","Malay"}, {"ms","Malay"},
        {"cze","Czech"}, {"ces","Czech"}, {"cs","Czech"},
        {"rum","Romanian"}, {"ron","Romanian"}, {"ro","Romanian"},
        {"hun","Hungarian"}, {"hu","Hungarian"},
        {"ukr","Ukrainian"}, {"uk","Ukrainian"},
        {"bul","Bulgarian"}, {"bg","Bulgarian"},
        {"gre","Greek"}, {"ell","Greek"}, {"el","Greek"},
        {"heb","Hebrew"}, {"he","Hebrew"},
        {"cat","Catalan"}, {"ca","Catalan"},
    };
    return m;
}

QString PlaybackController::langCodeToName(const QString &code) {
    QString c = code.trimmed().toLower();
    if (c.isEmpty()) return {};
    if (auto it = langMap().find(c); it != langMap().end())
        return it.value();
    // Already a human-readable name (longer than a code) — capitalize and return
    if (c.length() > 3)
        return c.left(1).toUpper() + c.mid(1);
    return c;  // unknown short code
}

void PlaybackController::fuzzySelectSubtitle() {
    QString preferred = m_settings->subtitleLanguage();
    if (preferred.isEmpty()) return;

    // If mpv already selected a subtitle via exact slang match, don't override
    if (m_mpv->currentSid() >= 0) return;

    QString query = langCodeToName(preferred);
    if (query.isEmpty()) return;

    const QVariantList &tracks = m_mpv->tracks();
    int bestId = -1;
    double bestScore = 0.80;  // Jaro-Winkler threshold

    for (const QVariant &t : tracks) {
        QVariantMap m = t.toMap();
        if (m["type"].toString() != "sub") continue;
        QString lang = m["lang"].toString();
        if (lang.isEmpty()) continue;

        QString name = langCodeToName(lang);
        if (name.isEmpty()) continue;

        double score = jaroWinkler(query, name);
        if (score > bestScore) {
            bestScore = score;
            bestId = m["id"].toInt();
        }
    }

    if (bestId >= 0) {
        qDebug() << "PlaybackController: fuzzy sub" << preferred << "→ track" << bestId
                 << "(" << langCodeToName(preferred) << "~" << bestScore << ")";
        m_mpv->setSid(bestId);
    }
}
