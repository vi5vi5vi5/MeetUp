#include "interface/sqlite/SqliteRoomAliases.h"

#include "interface/sqlite/SqliteDb.h"

namespace {

RoomAlias rowToAlias(const SqliteStmt &q)
{
    RoomAlias a;
    a.id          = q.colInt(0);
    a.roomId      = q.colInt(1);
    a.code        = q.colText(2);
    a.password    = q.colText(3);
    a.usesLeft    = q.colInt(4);
    // Логины в одной TEXT-колонке через запятую: список маленький (≤50),
    // отдельная таблица не окупается.
    const QString logins = q.colText(5);
    a.logins      = logins.isEmpty() ? QStringList{}
                                     : logins.split(QLatin1Char(','));
    a.enabled     = q.colInt(6) != 0;
    a.createdAtMs = q.colInt64(7);
    return a;
}

} // namespace

SqliteRoomAliases::SqliteRoomAliases(std::shared_ptr<SqliteDb> db)
    : m_db(std::move(db))
{
}

void SqliteRoomAliases::save(RoomAlias &alias)
{
    const QString logins = alias.logins.join(QLatin1Char(','));
    if (alias.id == -1) {
        SqliteStmt q(*m_db,
            "INSERT INTO room_aliases(room_id, code, password, uses_left, logins, enabled, created_at_ms) "
            "VALUES(?, ?, ?, ?, ?, ?, ?)");
        q.bind(1, alias.roomId);
        q.bind(2, alias.code);
        q.bind(3, alias.password);
        q.bind(4, alias.usesLeft);
        q.bind(5, logins);
        q.bind(6, alias.enabled ? 1 : 0);
        q.bind(7, alias.createdAtMs);
        q.step();
        alias.id = int(m_db->lastInsertId());
    } else {
        SqliteStmt q(*m_db,
            "UPDATE room_aliases SET room_id = ?, code = ?, password = ?, "
            "uses_left = ?, logins = ?, enabled = ?, created_at_ms = ? WHERE id = ?");
        q.bind(1, alias.roomId);
        q.bind(2, alias.code);
        q.bind(3, alias.password);
        q.bind(4, alias.usesLeft);
        q.bind(5, logins);
        q.bind(6, alias.enabled ? 1 : 0);
        q.bind(7, alias.createdAtMs);
        q.bind(8, alias.id);
        q.step();
    }
}

std::optional<RoomAlias> SqliteRoomAliases::findById(int id) const
{
    SqliteStmt q(*m_db, "SELECT id, room_id, code, password, uses_left, logins, enabled, created_at_ms "
                        "FROM room_aliases WHERE id = ?");
    q.bind(1, id);
    if (!q.step())
        return std::nullopt;
    return rowToAlias(q);
}

std::optional<RoomAlias> SqliteRoomAliases::findByCode(const QString &code) const
{
    SqliteStmt q(*m_db, "SELECT id, room_id, code, password, uses_left, logins, enabled, created_at_ms "
                        "FROM room_aliases WHERE code = ?");
    q.bind(1, code);
    if (!q.step())
        return std::nullopt;
    return rowToAlias(q);
}

QList<RoomAlias> SqliteRoomAliases::listByRoom(int roomId) const
{
    SqliteStmt q(*m_db, "SELECT id, room_id, code, password, uses_left, logins, enabled, created_at_ms "
                        "FROM room_aliases WHERE room_id = ? ORDER BY id");
    q.bind(1, roomId);
    QList<RoomAlias> out;
    while (q.step())
        out.append(rowToAlias(q));
    return out;
}

bool SqliteRoomAliases::removeBy(int id)
{
    SqliteStmt q(*m_db, "DELETE FROM room_aliases WHERE id = ?");
    q.bind(1, id);
    q.step();
    return m_db->changes() > 0;
}
