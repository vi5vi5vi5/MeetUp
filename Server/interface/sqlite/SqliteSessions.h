#pragma once

#include <memory>

#include "interface/Abstract/ISessions.h"

class SqliteDb;

// Сессии входа в SQLite (таблица sessions, ключ — токен). Благодаря этому
// вход переживает перезапуск сервера — кука в браузере остаётся валидной.
class SqliteSessions : public ISessions
{
public:
    explicit SqliteSessions(std::shared_ptr<SqliteDb> db);

    void save(const Session &session) override;
    std::optional<Session> findByToken(const QString &token) const override;
    bool removeByToken(const QString &token) override;
    int removeByUser(int userId) override;
    int removeExpired(qint64 nowMs) override;

private:
    std::shared_ptr<SqliteDb> m_db;
};
