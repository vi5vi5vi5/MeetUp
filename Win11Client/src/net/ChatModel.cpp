#include "ChatModel.h"

int ChatModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ChatModel::data(const QModelIndex& index, int role) const {
    if (index.row() < 0 || index.row() >= m_rows.size()) return {};
    const Row& r = m_rows.at(index.row());
    switch (role) {
    case AuthorRole:       return r.author;
    case TextRole:         return r.text;
    case TimeRole:         return r.time;
    case SelfRole:         return r.self;
    case LockedRole:       return r.locked;
    case ImageRole:        return r.image;
    case ImageLockedRole:  return r.imageLocked;
    case ImageDroppedRole: return r.imageDropped;
    case ImageWidthRole:   return r.imageW;
    case ImageHeightRole:  return r.imageH;
    default:               return {};
    }
}

QHash<int, QByteArray> ChatModel::roleNames() const {
    return {
        { AuthorRole,       "author" },
        { TextRole,         "text" },
        { TimeRole,         "time" },
        { SelfRole,         "self" },
        { LockedRole,       "locked" },
        { ImageRole,        "image" },
        { ImageLockedRole,  "imageLocked" },
        { ImageDroppedRole, "imageDropped" },
        { ImageWidthRole,   "imageWidth" },
        { ImageHeightRole,  "imageHeight" },
    };
}

void ChatModel::reset() {
    if (m_rows.isEmpty()) return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
}

void ChatModel::setAll(QList<Row> rows) {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

void ChatModel::prepend(const Row& row) {
    beginInsertRows({}, 0, 0);
    m_rows.prepend(row);
    endInsertRows();
}

void ChatModel::reRead(const std::function<void(Row&)>& render) {
    if (m_rows.isEmpty()) return;
    for (Row& r : m_rows) render(r);
    // Одним разом на всю ленту: строки меняются все сразу (сменился ключ), а
    // построчные сигналы здесь только добавили бы работы отрисовке.
    emit dataChanged(index(0), index(int(m_rows.size()) - 1),
        { TextRole, LockedRole, ImageRole, ImageLockedRole,
          ImageWidthRole, ImageHeightRole });
}
