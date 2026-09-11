#include "interface/sqlite/SqliteDb.h"

#include <QByteArray>
#include <QDebug>

#include "config/Log.h"

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
  owner_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  code          TEXT    NOT NULL UNIQUE,
  title         TEXT    NOT NULL,
  password      TEXT    NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_personal_rooms_owner ON personal_rooms(owner_id);
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
    migrate();

    qCInfo(lcApp).noquote() << QStringLiteral("SQLite: %1 (%2)")
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

bool SqliteDb::execChecked(const char *sql)
{
    char *err = nullptr;
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, &err) == SQLITE_OK)
        return true;
    qCritical().noquote() << QStringLiteral("SQLite: %1 — на запросе: %2")
                                 .arg(QString::fromUtf8(err), QString::fromUtf8(sql));
    sqlite3_free(err);
    return false;
}

int SqliteDb::userVersion() const
{
    SqliteStmt q(*this, "PRAGMA user_version;");
    return q.step() ? q.colInt(0) : 0;
}

void SqliteDb::setUserVersion(int v)
{
    // PRAGMA не принимает подстановку параметров — число подставляем в текст.
    // Оно наше собственное, не из внешнего мира.
    exec(QByteArray("PRAGMA user_version=" + QByteArray::number(v) + ';').constData());
}

bool SqliteDb::hasUniqueOwner() const
{
    SqliteStmt idx(*this, "PRAGMA index_list('personal_rooms');");
    while (idx.step()) {
        // Колонки: seq, name, unique, origin, partial. origin='u' — индекс
        // создан ограничением UNIQUE в объявлении таблицы (а не CREATE INDEX).
        if (idx.colInt(2) != 1 || idx.colText(3) != QLatin1String("u"))
            continue;
        const QString name = idx.colText(1);
        SqliteStmt cols(*this, QByteArray("PRAGMA index_info('" + name.toUtf8() + "');")
                                   .constData());
        while (cols.step()) {
            if (cols.colText(2) == QLatin1String("owner_id"))
                return true;
        }
    }
    return false;
}

// Снятие ограничения «одна личная комната на владельца».
//
// В SQLite ограничение нельзя изменить на месте — таблицу пересоздают. Три
// вещи здесь обязательны, и все три из-за room_aliases, которые ссылаются на
// personal_rooms(id) с ON DELETE CASCADE:
//
//   * foreign_keys выключаем ДО транзакции: внутри неё PRAGMA не действует;
//   * id переносим как есть — иначе ссылки алиасов уедут в пустоту;
//   * DROP старой таблицы делаем при выключенных ключах, иначе каскад снесёт
//     ВСЕ ссылки-приглашения на сервере.
//
// Всё, кроме PRAGMA, — в одной транзакции: сорвалось на середине, и база
// осталась ровно такой, какой была.
bool SqliteDb::dropUniqueOwner()
{
    static const char *kRebuild = R"sql(
CREATE TABLE personal_rooms_new(
  id            INTEGER PRIMARY KEY AUTOINCREMENT,
  owner_id      INTEGER NOT NULL REFERENCES users(id) ON DELETE CASCADE,
  code          TEXT    NOT NULL UNIQUE,
  title         TEXT    NOT NULL,
  password      TEXT    NOT NULL DEFAULT '',
  created_at_ms INTEGER NOT NULL
);
INSERT INTO personal_rooms_new(id, owner_id, code, title, password, created_at_ms)
  SELECT id, owner_id, code, title, password, created_at_ms FROM personal_rooms;
DROP TABLE personal_rooms;
ALTER TABLE personal_rooms_new RENAME TO personal_rooms;
CREATE INDEX IF NOT EXISTS idx_personal_rooms_owner ON personal_rooms(owner_id);
)sql";

    exec("PRAGMA foreign_keys=OFF;");
    bool ok = execChecked("BEGIN;") && execChecked(kRebuild);
    ok = execChecked(ok ? "COMMIT;" : "ROLLBACK;") && ok;
    exec("PRAGMA foreign_keys=ON;");
    if (!ok)
        return false;

    // Ссылки-приглашения должны по-прежнему указывать на живые комнаты.
    // Проверка дешёвая и делается один раз в жизни базы — не жалко.
    SqliteStmt check(*this, "PRAGMA foreign_key_check;");
    if (check.step()) {
        qCritical().noquote()
            << QStringLiteral("SQLite: после миграции остались висячие ссылки — "
                              "восстановите базу из копии");
        return false;
    }
    return true;
}

void SqliteDb::migrate()
{
    if (userVersion() >= kSchemaVersion)
        return;

    // Свежая база уже создана новой схемой — мигрировать нечего, просто
    // помечаем версию. Старая узнаётся по UNIQUE на owner_id.
    if (hasUniqueOwner()) {
        qCInfo(lcApp).noquote()
            << QStringLiteral("SQLite: миграция схемы — снимаем «одна личная комната "
                              "на владельца»");
        if (!dropUniqueOwner()) {
            // База цела (транзакция откатилась), но работать с ней как с
            // новой нельзя: вставка второй комнаты упрётся в ограничение и
            // молча ничего не сделает. Пусть лучше сервер не поднимется —
            // это видно сразу, а тихо испорченные данные не видно никогда.
            m_migrationFailed = true;
            qCritical().noquote()
                << QStringLiteral("SQLite: миграция не удалась, база не тронута. "
                                  "Проверьте место на диске и права на %1")
                       .arg(QString::fromUtf8(sqlite3_db_filename(m_db, "main")));
            return;
        }
        qCInfo(lcApp).noquote() << QStringLiteral("SQLite: миграция прошла");
    }
    setUserVersion(kSchemaVersion);
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
