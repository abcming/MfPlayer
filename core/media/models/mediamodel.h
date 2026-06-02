#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

class MediaModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        TypeRole,
        ImageUrlRole,
        YearRole,
        OverviewRole,
        ParentIdRole,
        IndexNumberRole,
        ChildCountRole,
        SeriesNameRole,
        SortNameRole,
        PlaybackPositionTicksRole,
        PlayedPercentageRole,
        RunTimeTicksRole,
        PlayedRole,
        BackdropUrlRole,
        IsFavoriteRole,
    };

    explicit MediaModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QJsonArray &items);
    void appendItems(const QJsonArray &items);
    void clear();

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap buildAlphaIndex() const;
    Q_INVOKABLE void updateItemField(const QString &itemId, int role, const QVariant &value);
    Q_INVOKABLE void updateItemByRoleName(const QString &itemId, const QString &roleName, const QVariant &value);
    Q_INVOKABLE bool removeItem(const QString &itemId);

private:
    struct Item {
        QString id, name, type, imageUrl, overview, parentId, seriesName, sortName;
        QString year;
        QString backdropUrl;
        int indexNumber = 0;
        int childCount = 0;
        qint64 playbackPositionTicks = 0;
        double playedPercentage = 0;
        qint64 runTimeTicks = 0;
        bool played = false;
        bool isFavorite = false;
    };
    QList<Item> m_items;
    QHash<QString, int> m_idToIndex;  // O(1) lookup by itemId
    QJsonArray m_lastSourceJson;

    static Item fromJson(const QJsonObject &obj);
};
