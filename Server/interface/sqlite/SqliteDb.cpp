#include "interface/sqlite/SqliteDb.h"

#include <QDebug>

#include "sqlite3.h"

namespace {

// Схема согласована в дорожной карте: users / sessions / personal_rooms.
// Новые таблицы и колонки добавляются сюда же (IF NOT EXISTS переживает
// повторный старт на уже созданной базе).
const char *kSchema = R"sql(
CREATE TABLE IF NOT EXISTS users(
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  login         TEXT    NOT NULL UNIQUE,
  display_name  TEXT    NOT NULL,
  pass_algo     TEXT    NOT NULL,
  pass_iters    INTEGER NOT NULL,
  pass_salt     BLOB    NOT NULL,
  pass_hash     BLOB    NOT NULL,
  avatar_ver    INTEGER NOT NULL DEFAULT 0,
  created_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions(
  token         TEXT    PRIMARY KEY,
  user_id       INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  created_at_ms INTEGER NOT NULL,
  expires_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_sessions_user    ON sessions(user_id);
CREATE INDEX IF NOT EXISTS idx_sessions_expires ON sessions(expires_at_ms);
CREATE TABLE IF NOT EXISTS personal_rooms(
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  owner_id      INTEGER NOT NULL UNIQUE REFERENCES users(id) ON DELETE CASCADE,
  code          TEXT    NOT NULL UNIQUE,
  title         TEXT    NOT NULL,
  password      TEXT    NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS room_aliases(
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  room_id       INTEGER NOT NULL REFERENCES personal_rooms(id) ON DELETE CASCADE,
  code          TEXT    NOT NULL UNIQUE,
  password      TEXT    NOT NULL DEFAULT '',
  uses_left     INTEGER NOT NULL DEFAULT -1,
  logins        TEXT    NOT NULL DEFAULT '',
  enabled       INTEGER NOT NULL DEFAULT 1,
  created_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_room_aliases_room ON room_aliases(room_id);
)sql";

} // namespace

SqliteDb::SqliteDb(const QString &path)
{
    if (sqlite3_open(path.toUtf8().constData(), &m_db) != SQLITE_OK) {
        qCritical().noquote() << QStringLiteral("SQLite: failed to open %1: %2")
                                     .arg(path, QString::fromUtf8(sqlite3_errmsg(m_db)));
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }

    // WAL: читатели не блокируют писателя, база переживает падение процесса.
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec(kSchema);

    qInfo().noquote() << QStringLiteral("SQLite: %1 (%2)")
                             .arg(path, QString::fromUtf8(sqlite3_libversion()));
}

SqliteDb::~SqliteDb()
{
    if (m_db)
        sqlite3_close(m_db);
}

void SqliteDb::exec(const char *sql)
{
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        qWarning().noquote() << QStringLiteral("SQLite exec: %1").arg(QString::fromUtf8(err));
        sqlite3_free(err);
    }
}

qint64 SqliteDb::lastInsertId() const
{
    return sqlite3_last_insert_rowid(m_db);
}

int SqliteDb::changes() const
{
    return sqlite3_changes(m_db);
}

// ---------- SqliteStmt ----------

SqliteStmt::SqliteStmt(const SqliteDb &db, const char *sql)
{
    if (sqlite3_prepare_v2(db.handle(), sql, -1, &m_stmt, nullptr) != SQLITE_OK)
        qWarning().noquote() << QStringLiteral("SQLite prepare '%1': %2")
                                    .arg(QString::fromUtf8(sql),
                                         QString::fromUtf8(sqlite3_errmsg(db.handle())));
}

SqliteStmt::~SqliteStmt()
{
    sqlite3_finalize(m_stmt);
}

void SqliteStmt::bind(int i, int v)      { sqlite3_bind_int(m_stmt, i, v); }
void SqliteStmt::bind(int i, qint64 v)   { sqlite3_bind_int64(m_stmt, i, v); }

void SqliteStmt::bind(int i, const QString &v)
{
    const QByteArray utf8 = v.toUtf8();
    sqlite3_bind_text(m_stmt, i, utf8.constData(), int(utf8.size()), SQLITE_TRANSIENT);
}

void SqliteStmt::bind(int i, const QByteArray &v)
{
    sqlite3_bind_blob(m_stmt, i, v.constData(), int(v.size()), SQLITE_TRANSIENT);
}

bool SqliteStmt::step()
{
    const int rc = sqlite3_step(m_stmt);
    if (rc == SQLITE_ROW)
        return true;
    if (rc != SQLITE_DONE)
        qWarning().noquote() << QStringLiteral("SQLite step: rc=%1").arg(rc);
    return false;
}

int SqliteStmt::colInt(int i) const      { return sqlite3_column_int(m_stmt, i); }
qint64 SqliteStmt::colInt64(int i) const { return sqlite3_column_int64(m_stmt, i); }

QString SqliteStmt::colText(int i) const
{
    return QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(m_stmt, i)),
                             sqlite3_column_bytes(m_stmt, i));
}

QByteArray SqliteStmt::colBlob(int i) const
{
    return QByteArray(reinterpret_cast<const char *>(sqlite3_column_blob(m_stmt, i)),
                      sqlite3_column_bytes(m_stmt, i));
}
