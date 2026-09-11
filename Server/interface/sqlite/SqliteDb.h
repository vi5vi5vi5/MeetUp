#pragma once

#include <QByteArray>
#include <QString>

struct sqlite3;
struct sqlite3_stmt;

// Одно соединение SQLite (third_party/sqlite, амальгамация). Открывает файл,
// накатывает схему (CREATE TABLE IF NOT EXISTS), при необходимости мигрирует её
// и раздаёт handle репозиториям. Все обращения — только из главного потока
// (как и к InMemory-хранилищам), поэтому SQLite собран с SQLITE_THREADSAFE=0.
//
// Версия схемы живёт в PRAGMA user_version — это встроенное в файл число,
// отдельная таблица для него не нужна. 0 — база, созданная до появления
// миграций.
class SqliteDb
{
public:
    explicit SqliteDb(const QString &path);
    ~SqliteDb();

    SqliteDb(const SqliteDb &) = delete;
    SqliteDb &operator=(const SqliteDb &) = delete;

    bool isOpen() const { return m_db != nullptr; }
    sqlite3 *handle() const { return m_db; }

    // DDL и другие запросы без результата; ошибки уходят в qWarning.
    void exec(const char *sql);

    // То же, но провал — это провал: сообщение уходит в qCritical, а
    // вызывающий обязан на него отреагировать. Для миграций, где «не вышло и
    // ладно» означает разъехавшуюся схему.
    bool execChecked(const char *sql);

    // Миграция не удалась — база осталась нетронутой (всё в транзакции), но
    // работать на ней в новом режиме нельзя. main на это не запускается.
    bool migrationFailed() const { return m_migrationFailed; }

    qint64 lastInsertId() const;
    int changes() const;

private:
    // Накатывает миграции до kSchemaVersion. Зовётся в конструкторе.
    void migrate();
    // Снять ограничение «одна личная комната на владельца».
    bool dropUniqueOwner();
    // Осталось ли в таблице personal_rooms UNIQUE по owner_id. Смотрим не в
    // текст DDL (там есть и UNIQUE у code — легко перепутать), а в список
    // индексов: точнее и не зависит от форматирования.
    bool hasUniqueOwner() const;

    int userVersion() const;
    void setUserVersion(int v);

    sqlite3 *m_db = nullptr;
    bool m_migrationFailed = false;

    // Версии схемы:
    //   0 — до миграций (личная комната одна на владельца, UNIQUE в таблице);
    //   1 — зарезервирована: ею помечались базы, созданные схемой с UNIQUE;
    //   2 — UNIQUE снят, комнат может быть несколько.
    static constexpr int kSchemaVersion = 2;
};

// Подготовленный запрос на время одного вызова репозитория: bind по номерам
// (с 1, как в SQLite), step() до false, колонки по номерам (с 0).
class SqliteStmt
{
public:
    SqliteStmt(const SqliteDb &db, const char *sql);
    ~SqliteStmt();

    SqliteStmt(const SqliteStmt &) = delete;
    SqliteStmt &operator=(const SqliteStmt &) = delete;

    void bind(int i, int v);
    void bind(int i, qint64 v);
    void bind(int i, const QString &v);
    void bind(int i, const QByteArray &v);

    // true — есть строка результата; false — запрос выполнен до конца.
    bool step();

    int        colInt(int i) const;
    qint64     colInt64(int i) const;
    QString    colText(int i) const;
    QByteArray colBlob(int i) const;

private:
    sqlite3_stmt *m_stmt = nullptr;
};
