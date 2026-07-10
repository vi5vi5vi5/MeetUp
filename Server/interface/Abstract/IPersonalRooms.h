#pragma once

#include <optional>

#include "models/PersonalRoom.h"

// Хранилище личных комнат. Как и IUsers: сервисы работают с интерфейсом,
// реализация подменяется (InMemory сейчас, SQLite позже).
class IPersonalRooms
{
public:
    virtual ~IPersonalRooms() = default;

    // id == -1 — создать (id присваивается), иначе обновить существующую.
    virtual void save(PersonalRoom &room) = 0;

    virtual std::optional<PersonalRoom> findById(int id) const = 0;

    // code должен быть уже нормализован (PersonalRoomService::normalizeCode).
    virtual std::optional<PersonalRoom> findByCode(const QString &code) const = 0;

    // Комната одна на пользователя, поэтому поиск по владельцу однозначен.
    virtual std::optional<PersonalRoom> findByOwner(int ownerId) const = 0;

    virtual bool removeBy(int id) = 0;
};
