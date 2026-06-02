#pragma once
#include <QQuickImageProvider>
#include <QHash>
#include <QMutex>

class IconProvider : public QQuickImageProvider {
public:
    explicit IconProvider(const QString &resourcePrefix);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    QString m_resourcePrefix;
    mutable QMutex m_mutex;
    QHash<QString, QImage> m_iconCache;
};
