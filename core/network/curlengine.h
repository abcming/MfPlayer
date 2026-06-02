#pragma once

#include <QObject>
#include <QTimer>
#include <QHash>
#include <QPointer>
#include <functional>
#include <curl/curl.h>

struct CurlResponse {
    CURLcode curlResult = CURLE_OK;
    long     httpStatus = 0;
    QByteArray contentType;
    QByteArray body;

    bool ok() const { return curlResult == CURLE_OK && httpStatus >= 200 && httpStatus < 300; }
    QString errorString() const;
};

class CurlHandle;

class CurlEngine : public QObject
{
    Q_OBJECT

public:
    using Headers  = QList<QPair<QByteArray, QByteArray>>;
    using Callback = std::function<void(CurlResponse)>;

    explicit CurlEngine(QObject *parent = nullptr);
    ~CurlEngine() override;

    CurlEngine(const CurlEngine &) = delete;
    CurlEngine &operator=(const CurlEngine &) = delete;

    void setMaxConnections(long n);
    void setSkipSslVerify(bool skip);

    CurlHandle get (const QString &url, const Headers &headers, Callback cb, long timeoutSecs = 30);
    CurlHandle post(const QString &url, const Headers &headers, const QByteArray &body, Callback cb, long timeoutSecs = 30);
    CurlHandle del (const QString &url, const Headers &headers, Callback cb, long timeoutSecs = 30);

    void cancel(CURL *easy);

private:
    struct Task {
        Callback   callback;
        QByteArray urlData;
        QByteArray postData;
        QByteArray responseBody;
        curl_slist *headerList = nullptr;
    };

    CurlHandle send(const QByteArray &method, const QString &url,
                    const Headers &headers, const QByteArray &body,
                    Callback cb, long timeoutSecs);

    static size_t writeCb(char *ptr, size_t size, size_t nmemb, void *userdata);

    void scheduleTick();
    void tick();

    CURLM          *m_multi = nullptr;
    QTimer          m_timer;
    QHash<CURL*, Task*> m_tasks;
    QByteArray      m_caPath;
    bool            m_skipSslVerify = false;
};

class CurlHandle
{
public:
    void cancel();
    bool isActive() const;

private:
    friend class CurlEngine;
    explicit CurlHandle(CurlEngine *engine, CURL *easy);

    QPointer<CurlEngine> m_engine;
    CURL *m_easy = nullptr;
};