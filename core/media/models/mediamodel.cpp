#include "mediamodel.h"

MediaModel::MediaModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int MediaModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : m_items.size();
}

QVariant MediaModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_items.size())
        return {};

    const auto &item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:        return item.name;
    case IdRole:          return item.id;
    case TypeRole:        return item.type;
    case ImageUrlRole:    return item.imageUrl;
    case YearRole:        return item.year;
    case OverviewRole:    return item.overview;
    case ParentIdRole:    return item.parentId;
    case IndexNumberRole: return item.indexNumber;
    case ChildCountRole:  return item.childCount;
    case SeriesNameRole: return item.seriesName;
    case SortNameRole:   return item.sortName;
    case PlaybackPositionTicksRole: return item.playbackPositionTicks;
    case PlayedPercentageRole: return item.playedPercentage;
    case RunTimeTicksRole: return item.runTimeTicks;
    case PlayedRole:     return item.played;
    case BackdropUrlRole: return item.backdropUrl;
    case IsFavoriteRole: return item.isFavorite;
    }
    return {};
}

QHash<int, QByteArray> MediaModel::roleNames() const {
    return {
        {IdRole, "itemId"},
        {NameRole, "itemName"},
        {TypeRole, "itemType"},
        {ImageUrlRole, "imageUrl"},
        {YearRole, "year"},
        {OverviewRole, "overview"},
        {ParentIdRole, "parentId"},
        {IndexNumberRole, "indexNumber"},
        {ChildCountRole, "childCount"},
        {SeriesNameRole, "seriesName"},
        {SortNameRole, "sortName"},
        {PlaybackPositionTicksRole, "playbackPositionTicks"},
        {PlayedPercentageRole, "playedPercentage"},
        {RunTimeTicksRole, "runTimeTicks"},
        {PlayedRole, "played"},
        {BackdropUrlRole, "backdropUrl"},
        {IsFavoriteRole, "isFavorite"},
    };
}

void MediaModel::setItems(const QJsonArray &items) {
    if (items == m_lastSourceJson) return;
    m_lastSourceJson = items;
    beginResetModel();
    m_items.clear();
    m_idToIndex.clear();
    int i = 0;
    for (const auto &val : items) {
        Item item = fromJson(val.toObject());
        m_idToIndex.insert(item.id, i++);
        m_items.append(std::move(item));
    }
    endResetModel();
}

void MediaModel::appendItems(const QJsonArray &items) {
    if (items.isEmpty()) return;
    int start = m_items.size();
    beginInsertRows({}, start, start + items.size() - 1);
    int i = start;
    for (const auto &val : items) {
        Item item = fromJson(val.toObject());
        m_idToIndex.insert(item.id, i++);
        m_items.append(std::move(item));
    }
    endInsertRows();
}

void MediaModel::clear() {
    beginResetModel();
    m_items.clear();
    m_idToIndex.clear();
    m_lastSourceJson = QJsonArray();
    endResetModel();
}

QVariantMap MediaModel::get(int row) const {
    if (row < 0 || row >= m_items.size())
        return {};
    const auto &item = m_items.at(row);
    return {
        {"itemId", item.id},
        {"itemName", item.name},
        {"itemType", item.type},
        {"imageUrl", item.imageUrl},
        {"year", item.year},
        {"overview", item.overview},
        {"parentId", item.parentId},
        {"indexNumber", item.indexNumber},
        {"childCount", item.childCount},
        {"seriesName", item.seriesName},
        {"sortName", item.sortName},
        {"playbackPositionTicks", item.playbackPositionTicks},
        {"playedPercentage", item.playedPercentage},
        {"runTimeTicks", item.runTimeTicks},
        {"played", item.played},
        {"backdropUrl", item.backdropUrl},
        {"isFavorite", item.isFavorite},
    };
}

QVariantMap MediaModel::buildAlphaIndex() const {
    QVariantMap map;
    for (int i = 0; i < m_items.size(); ++i) {
        const QString &name = !m_items[i].sortName.isEmpty()
            ? m_items[i].sortName : m_items[i].name;
        if (name.isEmpty()) continue;
        QChar ch = name[0].toUpper();
        QString key = (ch >= 'A' && ch <= 'Z') ? QString(ch) : QStringLiteral("#");
        if (!map.contains(key))
            map.insert(key, i);
    }
    return map;
}

MediaModel::Item MediaModel::fromJson(const QJsonObject &obj) {
    Item item;
    item.id = obj["Id"].toString();
    item.name = obj["Name"].toString();
    item.type = obj["Type"].toString();
    int py = obj["ProductionYear"].toInt();
    if (py > 0) {
        QString endDate = obj["EndDate"].toString();
        if (!endDate.isEmpty()) {
            int ey = endDate.left(4).toInt();
            if (ey > py)
                item.year = QString::number(py) + " - " + QString::number(ey);
            else
                item.year = QString::number(py);
        } else {
            QString status = obj["Status"].toString();
            if (status == "Continuing")
                item.year = QString::number(py) + " - 现在";
            else
                item.year = QString::number(py);
        }
    }
    item.overview = obj["Overview"].toString();
    item.parentId = obj["ParentId"].toString();
    item.seriesName = obj["SeriesName"].toString();
    item.sortName = obj["SortName"].toString();
    if (item.sortName.isEmpty())
        item.sortName = item.name;
    item.indexNumber = obj["IndexNumber"].toInt();
    item.childCount = obj["ChildCount"].toInt();

    // Parse UserData for resume/progress
    const auto ud = obj["UserData"].toObject();
    item.playbackPositionTicks = static_cast<qint64>(ud["PlaybackPositionTicks"].toDouble());
    item.playedPercentage = ud["PlayedPercentage"].toDouble();
    item.played = ud["Played"].toBool();
    item.isFavorite = ud["IsFavorite"].toBool();
    item.runTimeTicks = static_cast<qint64>(obj["RunTimeTicks"].toDouble());

    // Build image URL with correct type and tag
    const auto tags = obj["ImageTags"].toObject();
    QString imageType, imageTag;
    if (tags.contains("Primary")) {
        imageType = "Primary";
        imageTag = tags["Primary"].toString();
    } else if (tags.contains("Thumb")) {
        imageType = "Thumb";
        imageTag = tags["Thumb"].toString();
    }
    // Fallback to backdrop
    if (imageTag.isEmpty()) {
        const auto bdTags = obj["BackdropImageTags"].toArray();
        if (!bdTags.isEmpty()) {
            imageTag = bdTags.first().toString();
            imageType = "Backdrop";
        }
    }

    if (!imageTag.isEmpty()) {
        if (imageType == "Backdrop")
            item.imageUrl = QString("/emby/Items/%1/Images/Backdrop/0?tag=%2&maxWidth=600&quality=80").arg(item.id, imageTag);
        else
            item.imageUrl = QString("/emby/Items/%1/Images/%2?tag=%3&maxWidth=360&quality=80").arg(item.id, imageType, imageTag);
    }

    // Always extract backdrop URL for resume cards etc.
    const auto bdTags = obj["BackdropImageTags"].toArray();
    if (!bdTags.isEmpty())
        item.backdropUrl = QString("/emby/Items/%1/Images/Backdrop/0?tag=%2").arg(item.id, bdTags.first().toString());

    return item;
}

void MediaModel::updateItemField(const QString &itemId, int role, const QVariant &value) {
    int i = m_idToIndex.value(itemId, -1);
    if (i < 0 || i >= m_items.size()) return;
    switch (role) {
    case IsFavoriteRole: m_items[i].isFavorite = value.toBool(); break;
    case PlayedRole:     m_items[i].played = value.toBool(); break;
    default: return;
    }
    QModelIndex idx = index(i);
    emit dataChanged(idx, idx, {role});
}

void MediaModel::updateItemByRoleName(const QString &itemId, const QString &roleName, const QVariant &value) {
    static const QHash<QString, int> nameToRole = {
        {"isFavorite", IsFavoriteRole},
        {"played", PlayedRole},
    };
    auto it = nameToRole.find(roleName);
    if (it != nameToRole.end())
        updateItemField(itemId, it.value(), value);
}

bool MediaModel::removeItem(const QString &itemId) {
    int i = m_idToIndex.value(itemId, -1);
    if (i < 0 || i >= m_items.size()) return false;
    beginRemoveRows({}, i, i);
    m_items.removeAt(i);
    m_idToIndex.remove(itemId);
    // Update indices for all items after the removed one
    for (auto it = m_idToIndex.begin(); it != m_idToIndex.end(); ++it) {
        if (it.value() > i)
            --it.value();
    }
    endRemoveRows();
    return true;
}
