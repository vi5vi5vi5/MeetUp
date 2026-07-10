#include "interface/sqlite/SqlitePersonalRooms.h"

#include "interface/sqlite/SqliteDb.h"

namespace {

PersonalRoom rowToRoom(const SqliteStmt &q)
{
    PersonalRoom r;
    r.id          = q.colInt(0);
    r.ownerId     = q.colInt(1);
    r.code        = q.colText(2);
    r.title       = q.colText(3);
    r.password    = q.colText(4);
    r.createdAtMs = q.colInt64(5);
    return r;
}

} // namespace

SqlitePersonalRooms::SqlitePersonalRooms(std::shared_ptr<SqliteDb> db)
    : m_db(std::move(db))
{
}

void SqlitePersonalRooms::save(PersonalRoom &room)
{
    if (room.id == -1) {
        SqliteStmt q(*m_db,
            "INSERT INTO personal_rooms(owner_id, code, title, password, created_at_ms) "
            "VALUES(?, ?, ?, ?, ?)");
        q.bind(1, room.ownerId);
        q.bind(2, room.code);
        q.bind(3, room.title);
        q.bind(4, room.password);
        q.bind(5, room.createdAtMs);
        q.step();
        room.id = int(m_db->lastInsertId());
    } else {
        SqliteStmt q(*m_db,
            "UPDATE personal_rooms SET owner_id = ?, code = ?, title = ?, "
            "password = ?, created_at_ms = ? WHERE id = ?");
        q.bind(1, room.ownerId);
        q.bind(2, room.code);
        q.bind(3, room.title);
        q.bind(4, room.password);
        q.bind(5, room.createdAtMs);
        q.bind(6, room.id);
        q.step();
    }
}

std::optional<PersonalRoom> SqlitePersonalRooms::findById(int id) const
{
    SqliteStmt q(*m_db, "SELECT id, owner_id, code, title, password, created_at_ms "
                        "FROM personal_rooms WHERE id = ?");
    q.bind(1, id);
    if (!q.step())
        return std::nullopt;
    return rowToRoom(q);
}

std::optional<PersonalRoom> SqlitePersonalRooms::findByCode(const QString &code) const
{
    SqliteStmt q(*m_db, "SELECT id, owner_id, code, title, password, created_at_ms "
                        "FROM personal_rooms WHERE code = ?");
    q.bind(1, code);
    if (!q.step())
        return std::nullopt;
    return rowToRoom(q);
}

std::optional<PersonalRoom> SqlitePersonalRooms::findByOwner(int ownerId) const
{
    SqliteStmt q(*m_db, "SELECT id, owner_id, code, title, password, created_at_ms "
                        "FROM personal_rooms WHERE owner_id = ?");
    q.bind(1, ownerId);
    if (!q.step())
        return std::nullopt;
    return rowToRoom(q);
}

bool SqlitePersonalRooms::removeBy(int id)
{
    SqliteStmt q(*m_db, "DELETE FROM personal_rooms WHERE id = ?");
    q.bind(1, id);
    q.step();
    return m_db->changes() > 0;
}
