#include "iconprovider.h"
#include <QMutexLocker>
#include <QFile>
#include <QUrl>
#include <QUrlQuery>
#include <QColor>
#include <QSvgRenderer>
#include <QPainter>
#include <QRegularExpression>

IconProvider::IconProvider(const QString &resourcePrefix)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_resourcePrefix(resourcePrefix)
{
}

QImage IconProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize);

    // Fast path: cache hit — lock only for lookup
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_iconCache.find(id);
        if (it != m_iconCache.end()) {
            if (size) *size = it.value().size();
            return it.value();
        }
    }

    // Slow path: disk I/O + SVG rendering outside lock
    int queryIdx = id.indexOf('?');
    QString name = (queryIdx >= 0) ? id.left(queryIdx) : id;

    QUrl url("image://icons/" + id);
    QUrlQuery query(url);

    QString colorStr = query.queryItemValue("color", QUrl::FullyDecoded);
    QColor color(colorStr);
    if (!color.isValid())
        color = QColor(Qt::white);

    static const int kSize = 25;
    static const int kSuperSample = 4;
    int renderSize = kSize * kSuperSample;

    QString svgPath = m_resourcePrefix + "/" + name + ".svg";
    QFile file(svgPath);
    if (!file.open(QIODevice::ReadOnly)) {
        QImage fallback(kSize, kSize, QImage::Format_ARGB32_Premultiplied);
        fallback.fill(Qt::transparent);
        return fallback;
    }

    QString svgContent = QString::fromUtf8(file.readAll());
    file.close();

    static const QRegularExpression kFillRE(R"(fill="[^"]*")");
    svgContent.replace(kFillRE, "fill=\"" + color.name() + "\"");

    QSvgRenderer renderer(svgContent.toUtf8());
    QImage image(renderSize, renderSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&painter, QRectF(0, 0, renderSize, renderSize));
    painter.end();

    QImage finalImage = image.scaled(kSize, kSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    if (size)
        *size = finalImage.size();

    // Insert into cache — lock only for the write
    {
        QMutexLocker lock(&m_mutex);
        m_iconCache[id] = finalImage;
    }
    return finalImage;
}
