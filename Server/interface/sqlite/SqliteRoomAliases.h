#pragma once

#include <memory>

#include "interface/Abstract/IRoomAliases.h"

class SqliteDb;

// Alias-ссылки в SQLite (таблица room_aliases). Код уникален на уровне БД;
// при удалении личной комнаты алиасы уходят каскадом (FK ON DELETE CASCADE).
class SqliteRoomAliases : public IRoomAliases
{
public:
    explicit SqliteRoomAliases(std::shared_ptr<SqliteDb> db);

    void save(RoomAlias &alias) override;
    std::optional<RoomAlias> findById(int id) const override;
    std::optional<RoomAlias> findByCode(const QString &code) const override;
    QList<RoomAlias> listByRoom(int roomId) const override;
    bool removeBy(int id) override;

private:
    std::shared_ptr<SqliteDb> m_db;
};
