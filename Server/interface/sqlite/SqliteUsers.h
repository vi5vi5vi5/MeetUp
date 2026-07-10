#pragma once

#include <memory>

#include "interface/Abstract/IUsers.h"

class SqliteDb;

// Пользователи в SQLite (таблица users). Логин уникален на уровне БД —
// UNIQUE-индекс страхует проверки AuthService.
class SqliteUsers : public IUsers
{
public:
    explicit SqliteUsers(std::shared_ptr<SqliteDb> db);

    void save(User &user) override;
    std::optional<User> findById(int id) const override;
    std::optional<User> findByLogin(const QString &login) const override;
    bool removeBy(int id) override;

private:
    std::shared_ptr<SqliteDb> m_db;
};
