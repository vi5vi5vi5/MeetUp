#pragma once

#include <QByteArray>
#include <QString>

struct sqlite3;
struct sqlite3_stmt;

// Одно соединение SQLite (third_party/sqlite, амальгамация). Открывает файл,
// накатывает схему (CREATE TABLE IF NOT EXISTS) и раздаёт handle репозиториям.
// Все обращения — только из главного потока (как и к InMemory-хранилищам),
// поэтому SQLite собран с SQLITE_THREADSAFE=0.
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

    qint64 lastInsertId() const;
    int changes() const;

private:
    sqlite3 *m_db = nullptr;
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
