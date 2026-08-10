#pragma once
#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QHash>

class MediaModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantMap alphaIndex READ alphaIndex NOTIFY alphaIndexChanged FINAL)

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
        SeriesIdRole,
        SeasonIdRole,
    };

    explicit MediaModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setItems(const QJsonArray &items);
    void appendItems(const QJsonArray &items);
    void clear();

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantList getAllItems() const;
    QVariantMap alphaIndex() const { return m_alphaIndex; }
    Q_INVOKABLE QVariantMap buildAlphaIndex() const;  // retained for compat
    Q_INVOKABLE void updateItemField(const QString &itemId, int role, const QVariant &value);
    Q_INVOKABLE void updateItemByRoleName(const QString &itemId, const QString &roleName, const QVariant &value);
    Q_INVOKABLE bool removeItem(const QString &itemId);

signals:
    void alphaIndexChanged();

private:
    struct Item {
        QString id, name, type, imageUrl, overview, parentId, seriesName, sortName;
        // 卡片直接起播要用这两个拉同季播放列表。**别拿 parentId 当季 id** ——
        // Resume/收藏这些接口压根不返回 ParentId, Emby 给的是独立的 SeasonId 字段
        QString seriesId, seasonId;
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
    // Dedup fingerprint of the last setItems() source — 全量 Id 的滚动哈希,
    // 只占一个标量, 不 pin 住整个 QJsonArray (大库要几 MB)。
    // 去重是为了首屏缓存预显示不闪烁, 别去掉; 但指纹必须覆盖全部内容,
    // 只看 size + firstId 会漏掉"中部增删"(见 .cpp 说明)
    size_t m_lastSourceFingerprint = 0;
    bool m_fingerprintValid = false;
    QVariantMap m_alphaIndex;          // incrementally maintained A-Z→row map

    void rebuildAlphaIndex();
    void extendAlphaIndex(int fromRow);

    static Item fromJson(const QJsonObject &obj);
};
