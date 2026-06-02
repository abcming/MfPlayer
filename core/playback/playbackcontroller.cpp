#include "core/playback/playbackcontroller.h"
#include "common/constants.h"
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
    connect(m_mpv, &MpvController::endOfFile, this, &PlaybackController::endOfFile);
    connect(m_mpv, &MpvController::errorOccurred, this, &PlaybackController::playError);

    // Load external subtitles after file is ready
    connect(m_mpv, &MpvController::fileLoaded, this, [this]() {
        const QJsonObject &item = m_currentItemDetail;
        QJsonArray streams = streamsForSelectedSource();

        // Only add the external subtitle the user selected (Tsukimi-style).
        // mpv's sub-add select flag is deprecated — bulk-adding would cause
        // the last-added track to always win regardless of user choice.
        for (const QJsonValue &s : streams) {
            QJsonObject stream = s.toObject();
            if (stream["Type"].toString() != Constants::kStreamTypeSubtitle) continue;
            if (!stream["IsExternal"].toBool()) continue;
            if (stream["Index"].toInt() != m_pendingSubIdx) continue;

            QString deliveryUrl = stream["DeliveryUrl"].toString();
            QString fullUrl;
            if (!deliveryUrl.isEmpty() && deliveryUrl != "null") {
                fullUrl = deliveryUrl.startsWith('/')
                    ? m_emby->serverUrl() + deliveryUrl
                    : deliveryUrl;
            } else if (m_currentMediaSourceId.isEmpty()) {
                break;  // no media source, can't construct subtitle URL
            } else {
                QString itemId = item["Id"].toString();
                QString idx = QString::number(stream["Index"].toInt());
                QString codec = stream["Codec"].toString().toLower();
                fullUrl = m_emby->serverUrl() +
                    "/Videos/" + itemId + "/" + m_currentMediaSourceId +
                    "/Subtitles/" + idx + "/0/Stream." + codec;
            }
            if (!fullUrl.contains("api_key"))
                fullUrl += QString(fullUrl.contains('?') ? "&" : "?") +
                           "api_key=" + m_emby->accessToken();
            qDebug() << "PlaybackController: adding external subtitle:" << stream["DisplayTitle"].toString();
            m_mpv->addSubtitleFile(fullUrl,
                stream["DisplayTitle"].toString(),
                stream["Language"].toString());
            break;
        }

        if (m_pendingSubIdx == -2)
            m_mpv->setSid(-2);  // now maps to "no" in MpvController

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
        if (!m_currentPlayItemId.isEmpty()) {
            qint64 finalTicks = static_cast<qint64>(m_mpv->position() * Constants::kTicksPerSecond);
            m_emby->reportPlaybackStop(m_currentPlayItemId, finalTicks,
                m_currentPlaySessionId, m_currentMediaSourceId);
            updateCachedProgress(m_currentPlayItemId, finalTicks);
            m_currentPlayItemId.clear();
        }
        m_progressTimer->stop();
    });
}

void PlaybackController::playItem(const QString &itemId, qint64 startTimeTicks,
                                  const QString &mediaSourceId,
                                  int audioIndex, int subtitleIndex) {
    m_currentPlayItemId = itemId;
    m_currentItemDetail = m_cache->getItemDetail(itemId);
    int gen = ++m_playGeneration;

    // Resolve MediaSourceId from current item if not provided and available
    QString srcId = mediaSourceId;
    if (srcId.isEmpty()) {
        QJsonArray sources = m_currentItemDetail["MediaSources"].toArray();
        if (!sources.isEmpty())
            srcId = sources.first().toObject()["Id"].toString();
    }
    m_currentMediaSourceId = srcId;
    m_pendingSubIdx = subtitleIndex;

    m_emby->fetchPlaybackInfo(itemId,
        [this, itemId, startTimeTicks, gen, audioIndex, subtitleIndex](const QString &streamUrl, const QString &playSessionId) {
        if (gen != m_playGeneration) return;
        if (streamUrl.isEmpty()) {
            m_currentPlayItemId.clear();
            emit playError("无法获取播放地址或解析重定向失败");
            return;
        }
        m_currentPlaySessionId = playSessionId;
        m_emby->reportPlaybackStart(itemId, startTimeTicks, playSessionId, m_currentMediaSourceId,
            [this, streamUrl, startTimeTicks, gen, audioIndex, subtitleIndex]() {
            if (gen != m_playGeneration) return;
            QString fullUrl = streamUrl.startsWith('/')
                ? m_emby->serverUrl() + streamUrl
                : streamUrl;

            // ── Language preferences BEFORE play() so mpv auto-selects correctly ──
            // Audio: look up language from Emby Index → set alang
            if (audioIndex >= 0) {
                QJsonArray streams = streamsForSelectedSource();
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

            // Subtitle: look up language from Emby Index → set slang
            if (subtitleIndex >= 0) {
                QJsonArray streams = streamsForSelectedSource();
                for (const QJsonValue &v : streams) {
                    QJsonObject st = v.toObject();
                    if (st["Type"].toString() == Constants::kStreamTypeSubtitle
                        && st["Index"].toInt() == subtitleIndex) {
                        QString lang = st["Language"].toString();
                        if (!lang.isEmpty()) m_mpv->setSlang(lang);
                        break;
                    }
                }
            } else if (subtitleIndex == -2) {
                // "Off": clear slang so mpv won't auto-select; fileLoaded also calls setSid(-2→"no")
                m_mpv->setSlang(QString());
            }

            m_mpv->play(fullUrl, m_emby->serverUrl(), startTimeTicks / static_cast<double>(Constants::kTicksPerSecond));
            m_progressTimer->start();
        });
    }, m_currentMediaSourceId, m_pendingSubIdx);
}

void PlaybackController::playLocalFile(const QString &filePath) {
    // Clear stale Emby playback state
    m_currentItemDetail = {};
    m_pendingSubIdx = -1;  // auto: let fuzzy matching decide

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
    if (!m_currentPlayItemId.isEmpty()) {
        qint64 finalTicks = static_cast<qint64>(m_mpv->position() * Constants::kTicksPerSecond);
        m_emby->reportPlaybackStop(m_currentPlayItemId, finalTicks,
            m_currentPlaySessionId, m_currentMediaSourceId);
        updateCachedProgress(m_currentPlayItemId, finalTicks);
        m_currentPlayItemId.clear();
    }
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
    QJsonObject ud;
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
    if (m_currentPlayItemId.isEmpty() || !m_mpv->playing()) return;
    qint64 ticks = static_cast<qint64>(m_mpv->position() * Constants::kTicksPerSecond);
    m_emby->reportPlaybackProgress(m_currentPlayItemId, ticks,
                                    m_currentPlaySessionId, m_currentMediaSourceId);
}

QJsonArray PlaybackController::streamsForSelectedSource() const {
    const QJsonObject &item = m_currentItemDetail;
    QJsonArray sources = item["MediaSources"].toArray();
    for (const QJsonValue &sv : sources) {
        QJsonObject src = sv.toObject();
        if (src["Id"].toString() == m_currentMediaSourceId) {
            QJsonArray ms = src["MediaStreams"].toArray();
            if (!ms.isEmpty()) return ms;
        }
    }
    return item["MediaStreams"].toArray();
}

QVariantList PlaybackController::scanFolderForLocalPlaylist(const QString &filePath) const {
    QFileInfo fi(filePath);
    QDir dir = fi.absoluteDir();

    // Canonical path for robust matching (resolves symlinks, normalizes separators)
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

        // Mark the entry that matches the file we're about to play
        QString fCanonical = files[i].canonicalFilePath();
        if (fCanonical.isEmpty())
            fCanonical = files[i].absoluteFilePath();
        if (fCanonical == inputCanonical)
            item["isCurrent"] = true;

        playlist.append(item);
    }
    return playlist;
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
QString PlaybackController::langCodeToName(const QString &code) {
    QString c = code.trimmed().toLower();
    if (c.isEmpty()) return {};

    // Matroska / Emby variant codes
    if (c == "chs" || c == "zho" || c == "zh" || c == "zh-cn" || c == "zh-hans")
        return "Chinese Simplified";
    if (c == "cht" || c == "zh-tw" || c == "zh-hant")
        return "Chinese Traditional";
    if (c == "chi")
        return "Chinese";   // generic / Traditional (639-2/B)

    if (c == "eng" || c == "en")       return "English";
    if (c == "jpn" || c == "ja")       return "Japanese";
    if (c == "kor" || c == "ko")       return "Korean";
    if (c == "fre" || c == "fra" || c == "fr") return "French";
    if (c == "ger" || c == "deu" || c == "de") return "German";
    if (c == "spa" || c == "es")       return "Spanish";
    if (c == "ita" || c == "it")       return "Italian";
    if (c == "por" || c == "pt")       return "Portuguese";
    if (c == "rus" || c == "ru")       return "Russian";
    if (c == "ara" || c == "ar")       return "Arabic";
    if (c == "nob" || c == "nor" || c == "no") return "Norwegian";
    if (c == "swe" || c == "sv")       return "Swedish";
    if (c == "dan" || c == "da")       return "Danish";
    if (c == "fin" || c == "fi")       return "Finnish";
    if (c == "dut" || c == "nld" || c == "nl") return "Dutch";
    if (c == "pol" || c == "pl")       return "Polish";
    if (c == "tur" || c == "tr")       return "Turkish";
    if (c == "hin" || c == "hi")       return "Hindi";
    if (c == "tha" || c == "th")       return "Thai";
    if (c == "vie" || c == "vi")       return "Vietnamese";
    if (c == "ind" || c == "id")       return "Indonesian";
    if (c == "may" || c == "msa" || c == "ms") return "Malay";
    if (c == "cze" || c == "ces" || c == "cs") return "Czech";
    if (c == "rum" || c == "ron" || c == "ro") return "Romanian";
    if (c == "hun" || c == "hu")       return "Hungarian";
    if (c == "ukr" || c == "uk")       return "Ukrainian";
    if (c == "bul" || c == "bg")       return "Bulgarian";
    if (c == "gre" || c == "ell" || c == "el") return "Greek";
    if (c == "heb" || c == "he")       return "Hebrew";
    if (c == "cat" || c == "ca")       return "Catalan";

    // Already a human-readable name (longer than a code) — capitalize and return
    if (c.length() > 3) {
        c = c.left(1).toUpper() + c.mid(1);
        return c;
    }
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
