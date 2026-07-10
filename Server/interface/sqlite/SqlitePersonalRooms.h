#pragma once

#include <memory>

#include "interface/Abstract/IPersonalRooms.h"

class SqliteDb;

// Личные комнаты в SQLite (таблица personal_rooms). Код и владелец уникальны
// на уровне БД — UNIQUE-индексы страхуют проверки PersonalRoomService.
class SqlitePersonalRooms : public IPersonalRooms
{
public:
    explicit SqlitePersonalRooms(std::shared_ptr<SqliteDb> db);

    void save(PersonalRoom &room) override;
    std::optional<PersonalRoom> findById(int id) const override;
    std::optional<PersonalRoom> findByCode(const QString &code) const override;
    std::optional<PersonalRoom> findByOwner(int ownerId) const override;
    bool removeBy(int id) override;

private:
    std::shared_ptr<SqliteDb> m_db;
};
