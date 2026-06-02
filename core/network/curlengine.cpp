#include "curlengine.h"
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QStandardPaths>

// ─────────────────────────────────────────────
// CurlResponse
// ─────────────────────────────────────────────

QString CurlResponse::errorString() const
{
    if (curlResult != CURLE_OK)
        return QString("CURL error %1: %2").arg(curlResult).arg(curl_easy_strerror(curlResult));
    if (httpStatus < 200 || httpStatus >= 300)
        return QString("HTTP %1").arg(httpStatus);
    return {};
}

// ─────────────────────────────────────────────
// CurlHandle
// ─────────────────────────────────────────────

CurlHandle::CurlHandle(CurlEngine *engine, CURL *easy)
    : m_engine(engine), m_easy(easy)
{}

void CurlHandle::cancel()
{
    if (m_engine && m_easy)
        m_engine->cancel(m_easy);
    m_easy = nullptr;
}

bool CurlHandle::isActive() const
{
    return m_engine && m_easy;
}

// ─────────────────────────────────────────────
// CurlEngine
// ─────────────────────────────────────────────

CurlEngine::CurlEngine(QObject *parent)
    : QObject(parent)
{
    m_multi = curl_multi_init();
    curl_multi_setopt(m_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, 8L);

    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &CurlEngine::tick);

#ifdef Q_OS_WIN
    // Extract CA bundle from Qt resources for non-Schannel SSL backends (e.g. OpenSSL).
    // CURLSSLOPT_NATIVE_CA only works with Schannel; if vcpkg compiled curl with
    // OpenSSL, we need an explicit CA file. Ship one baked into the exe.
    QFile caRes(":/mfplayer/resources/cacert.pem");
    if (caRes.open(QIODevice::ReadOnly)) {
        QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        QString path = dir + "/cacert.pem";
        QFile out(path);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(caRes.readAll());
            m_caPath = path.toUtf8();
        }
    }
#endif
}

CurlEngine::~CurlEngine()
{
    m_timer.stop();
    for (auto it = m_tasks.begin(); it != m_tasks.end(); ++it) {
        curl_multi_remove_handle(m_multi, it.key());
        curl_easy_cleanup(it.key());
        if (it.value()->headerList)
            curl_slist_free_all(it.value()->headerList);
        delete it.value();
    }
    curl_multi_cleanup(m_multi);
}

void CurlEngine::setMaxConnections(long n)
{
    curl_multi_setopt(m_multi, CURLMOPT_MAX_TOTAL_CONNECTIONS, n);
}

// ── 公开方法 ──────────────────────────────────

CurlHandle CurlEngine::get(const QString &url, const Headers &headers, Callback cb, long timeoutSecs)
{
    return send("GET", url, headers, {}, std::move(cb), timeoutSecs);
}

CurlHandle CurlEngine::post(const QString &url, const Headers &headers, const QByteArray &body, Callback cb, long timeoutSecs)
{
    return send("POST", url, headers, body, std::move(cb), timeoutSecs);
}

CurlHandle CurlEngine::del(const QString &url, const Headers &headers, Callback cb, long timeoutSecs)
{
    return send("DELETE", url, headers, {}, std::move(cb), timeoutSecs);
}

void CurlEngine::cancel(CURL *easy)
{
    Task *task = m_tasks.take(easy);
    if (!task) return;  // 已完成或不存在

    curl_multi_remove_handle(m_multi, easy);
    curl_easy_cleanup(easy);
    if (task->headerList)
        curl_slist_free_all(task->headerList);
    delete task;
}

// ── 内部实现 ──────────────────────────────────

CurlHandle CurlEngine::send(const QByteArray &method, const QString &url,
                             const Headers &headers, const QByteArray &body,
                             Callback cb, long timeoutSecs)
{
    CURL *easy = curl_easy_init();

    // Task 持有所有需要在请求期间保活的数据
    auto *task = new Task;
    task->callback  = std::move(cb);
    task->urlData   = url.toUtf8();
    task->postData  = body;          // POST body 生命周期与 task 绑定

    curl_easy_setopt(easy, CURLOPT_URL, task->urlData.constData());

    // HTTP/2 仅对 TLS 升级，对 http:// 自动回落到 HTTP/1.1
    curl_easy_setopt(easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2TLS);
    curl_easy_setopt(easy, CURLOPT_TIMEOUT,        timeoutSecs);
    curl_easy_setopt(easy, CURLOPT_CONNECTTIMEOUT, 10L);

#ifdef Q_OS_WIN
    curl_easy_setopt(easy, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
    if (!m_caPath.isEmpty())
        curl_easy_setopt(easy, CURLOPT_CAINFO, m_caPath.constData());
#else
    curl_easy_setopt(easy, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
#endif

    curl_easy_setopt(easy, CURLOPT_PRIVATE,       task);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, writeCb);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA,     task);

    // Headers
    curl_slist *hlist = nullptr;
    for (const auto &h : headers) {
        QByteArray line = h.first + ": " + h.second;
        hlist = curl_slist_append(hlist, line.constData());
    }
    task->headerList = hlist;
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, hlist);

    // Method
    if (method == "POST") {
        curl_easy_setopt(easy, CURLOPT_POST,          1L);
        // POSTFIELDS 指向 task->postData，task 在请求完成前不会释放
        curl_easy_setopt(easy, CURLOPT_POSTFIELDS,    task->postData.constData());
        curl_easy_setopt(easy, CURLOPT_POSTFIELDSIZE, static_cast<long>(task->postData.size()));
    } else if (method != "GET") {
        curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.constData());
    }

    curl_multi_add_handle(m_multi, easy);
    m_tasks.insert(easy, task);

    scheduleTick();
    return CurlHandle(this, easy);
}

size_t CurlEngine::writeCb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    auto *task = static_cast<Task *>(userdata);
    task->responseBody.append(ptr, size * nmemb);
    return size * nmemb;
}

void CurlEngine::scheduleTick()
{
    // 已经在计时就不重置，避免连续 send() 把 tick 不断推迟
    if (m_timer.isActive()) return;

    long timeout_ms = 0;
    curl_multi_timeout(m_multi, &timeout_ms);
    if (timeout_ms < 0 || timeout_ms > 100) timeout_ms = 10;
    if (timeout_ms == 0)                    timeout_ms = 1;

    m_timer.start(timeout_ms);
}

void CurlEngine::tick()
{
    int running = 0;
    curl_multi_perform(m_multi, &running);

    CURLMsg *msg;
    int msgs_left;
    QPointer<CurlEngine> guard(this);

    while ((msg = curl_multi_info_read(m_multi, &msgs_left))) {
        if (msg->msg != CURLMSG_DONE) continue;

        CURL  *easy = msg->easy_handle;
        Task  *task = m_tasks.take(easy);
        curl_multi_remove_handle(m_multi, easy);

        // 构造响应
        CurlResponse resp;
        resp.curlResult = msg->data.result;
        curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &resp.httpStatus);
        char *ct = nullptr;
        curl_easy_getinfo(easy, CURLINFO_CONTENT_TYPE, &ct);
        resp.contentType = ct ? QByteArray(ct) : QByteArray();
        resp.body        = std::move(task->responseBody);

        // 清理
        curl_easy_cleanup(easy);
        if (task->headerList)
            curl_slist_free_all(task->headerList);
        auto cb = std::move(task->callback);
        delete task;

        // 回调可能销毁 CurlEngine 本身
        cb(resp);
        if (!guard) return;
    }

    if (running > 0) {
        // 还有进行中的请求，继续驱动（此时 timer 已停止，重新调度）
        scheduleTick();
    }
}