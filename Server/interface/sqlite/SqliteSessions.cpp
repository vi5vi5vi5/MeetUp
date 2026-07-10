#include "interface/sqlite/SqliteSessions.h"

#include "interface/sqlite/SqliteDb.h"

SqliteSessions::SqliteSessions(std::shared_ptr<SqliteDb> db)
    : m_db(std::move(db))
{
}

void SqliteSessions::save(const Session &session)
{
    // Вставка или продление: токен — первичный ключ, REPLACE обновляет срок.
    SqliteStmt q(*m_db,
        "INSERT OR REPLACE INTO sessions(token, user_id, created_at_ms, expires_at_ms) "
        "VALUES(?, ?, ?, ?)");
    q.bind(1, session.token);
    q.bind(2, session.userId);
    q.bind(3, session.createdAtMs);
    q.bind(4, session.expiresAtMs);
    q.step();
}

std::optional<Session> SqliteSessions::findByToken(const QString &token) const
{
    SqliteStmt q(*m_db, "SELECT token, user_id, created_at_ms, expires_at_ms "
                        "FROM sessions WHERE token = ?");
    q.bind(1, token);
    if (!q.step())
        return std::nullopt;

    Session s;
    s.token       = q.colText(0);
    s.userId      = q.colInt(1);
    s.createdAtMs = q.colInt64(2);
    s.expiresAtMs = q.colInt64(3);
    return s;
}

bool SqliteSessions::removeByToken(const QString &token)
{
    SqliteStmt q(*m_db, "DELETE FROM sessions WHERE token = ?");
    q.bind(1, token);
    q.step();
    return m_db->changes() > 0;
}

int SqliteSessions::removeByUser(int userId)
{
    SqliteStmt q(*m_db, "DELETE FROM sessions WHERE user_id = ?");
    q.bind(1, userId);
    q.step();
    return m_db->changes();
}

int SqliteSessions::removeExpired(qint64 nowMs)
{
    SqliteStmt q(*m_db, "DELETE FROM sessions WHERE expires_at_ms <= ?");
    q.bind(1, nowMs);
    q.step();
    return m_db->changes();
}
