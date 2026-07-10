#include "interface/sqlite/SqliteUsers.h"

#include "interface/sqlite/SqliteDb.h"

namespace {

User rowToUser(const SqliteStmt &q)
{
    User u;
    u.id          = q.colInt(0);
    u.login       = q.colText(1);
    u.displayName = q.colText(2);
    u.passAlgo    = q.colText(3);
    u.passIters   = q.colInt(4);
    u.passSalt    = q.colBlob(5);
    u.passHash    = q.colBlob(6);
    u.avatarVer   = q.colInt(7);
    u.createdAtMs = q.colInt64(8);
    return u;
}

} // namespace

SqliteUsers::SqliteUsers(std::shared_ptr<SqliteDb> db)
    : m_db(std::move(db))
{
}

void SqliteUsers::save(User &user)
{
    if (user.id == -1) {
        SqliteStmt q(*m_db,
            "INSERT INTO users(login, display_name, pass_algo, pass_iters, "
            "pass_salt, pass_hash, avatar_ver, created_at_ms) "
            "VALUES(?, ?, ?, ?, ?, ?, ?, ?)");
        q.bind(1, user.login);
        q.bind(2, user.displayName);
        q.bind(3, user.passAlgo);
        q.bind(4, user.passIters);
        q.bind(5, user.passSalt);
        q.bind(6, user.passHash);
        q.bind(7, user.avatarVer);
        q.bind(8, user.createdAtMs);
        q.step();
        user.id = int(m_db->lastInsertId());
    } else {
        SqliteStmt q(*m_db,
            "UPDATE users SET login = ?, display_name = ?, pass_algo = ?, "
            "pass_iters = ?, pass_salt = ?, pass_hash = ?, avatar_ver = ?, "
            "created_at_ms = ? WHERE id = ?");
        q.bind(1, user.login);
        q.bind(2, user.displayName);
        q.bind(3, user.passAlgo);
        q.bind(4, user.passIters);
        q.bind(5, user.passSalt);
        q.bind(6, user.passHash);
        q.bind(7, user.avatarVer);
        q.bind(8, user.createdAtMs);
        q.bind(9, user.id);
        q.step();
    }
}

std::optional<User> SqliteUsers::findById(int id) const
{
    SqliteStmt q(*m_db, "SELECT id, login, display_name, pass_algo, pass_iters, "
                        "pass_salt, pass_hash, avatar_ver, created_at_ms "
                        "FROM users WHERE id = ?");
    q.bind(1, id);
    if (!q.step())
        return std::nullopt;
    return rowToUser(q);
}

std::optional<User> SqliteUsers::findByLogin(const QString &login) const
{
    SqliteStmt q(*m_db, "SELECT id, login, display_name, pass_algo, pass_iters, "
                        "pass_salt, pass_hash, avatar_ver, created_at_ms "
                        "FROM users WHERE login = ?");
    q.bind(1, login);
    if (!q.step())
        return std::nullopt;
    return rowToUser(q);
}

bool SqliteUsers::removeBy(int id)
{
    SqliteStmt q(*m_db, "DELETE FROM users WHERE id = ?");
    q.bind(1, id);
    q.step();
    return m_db->changes() > 0;
}
