#pragma once

#include <optional>

#include <QList>

#include "models/RoomAlias.h"

// Хранилище alias-ссылок личных комнат. Как и остальные репозитории:
// сервисы работают с интерфейсом, реализация подменяется.
class IRoomAliases
{
public:
    virtual ~IRoomAliases() = default;

    // id == -1 — создать (id присваивается), иначе обновить существующий.
    virtual void save(RoomAlias &alias) = 0;

    virtual std::optional<RoomAlias> findById(int id) const = 0;

    // code должен быть уже нормализован (PersonalRoomService::normalizeCode).
    virtual std::optional<RoomAlias> findByCode(const QString &code) const = 0;

    virtual QList<RoomAlias> listByRoom(int roomId) const = 0;

    virtual bool removeBy(int id) = 0;
};
