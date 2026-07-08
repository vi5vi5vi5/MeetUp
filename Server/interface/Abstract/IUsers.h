#pragma once

#include <optional>

#include "models/User.h"

// Хранилище пользователей. Сервисы работают только с этим интерфейсом,
// реализация подменяется: InMemory сейчас, SQLite позже (приём из MedFlow:
// interface/Abstract → InMemory / sqlite).
class IUsers
{
public:
    virtual ~IUsers() = default;

    // id == -1 — создать (id присваивается), иначе обновить существующего.
    virtual void save(User &user) = 0;

    virtual std::optional<User> findById(int id) const = 0;

    // login должен быть уже нормализован (AuthService::normalizeLogin).
    // Выделен в отдельный метод, а не предикат: в SQLite это индекс.
    virtual std::optional<User> findByLogin(const QString &login) const = 0;

    virtual bool removeBy(int id) = 0;
};
