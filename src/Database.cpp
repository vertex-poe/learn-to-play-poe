#include "Database.h"

#include <sqlite3.h>

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

Database::Database(const QString &path)
    : m_path(path)
{
    const QByteArray utf8 = path.toUtf8();
    if (sqlite3_open(utf8.constData(), &m_db) != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    applyPragmas();
    initSchema();
}

Database::~Database()
{
    if (m_db)
        sqlite3_close(m_db);
}

void Database::applyPragmas()
{
    execSql(m_db, "PRAGMA journal_mode=WAL;");
    execSql(m_db, "PRAGMA synchronous=NORMAL;");
    execSql(m_db, "PRAGMA temp_store=MEMORY;");
    execSql(m_db, "PRAGMA cache_size=-65536;");
}

void Database::initSchema()
{
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS installs (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            path             TEXT    NOT NULL UNIQUE,
            file_created_at  INTEGER NOT NULL DEFAULT 0,
            file_modified_at INTEGER NOT NULL DEFAULT 0,
            file_size        INTEGER NOT NULL DEFAULT 0,
            last_byte_offset INTEGER NOT NULL DEFAULT 0
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS logs (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            install_id INTEGER NOT NULL REFERENCES installs(id),
            logged_at  TEXT    NOT NULL,
            line       TEXT    NOT NULL,
            line_hash  INTEGER NOT NULL,
            UNIQUE (install_id, logged_at, line_hash)
        );
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_logs_install_time
        ON logs(install_id, logged_at);
    )");
}

Database::InstallState Database::upsertInstall(const QString &installPath)
{
    if (!m_db) return {};

    const QByteArray pathBytes = installPath.toUtf8();

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT OR IGNORE INTO installs(path) VALUES(?);",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pathBytes.constData(), pathBytes.size(), SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    sqlite3_prepare_v2(m_db,
        "SELECT id, file_created_at, file_modified_at, file_size, last_byte_offset "
        "FROM installs WHERE path = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, pathBytes.constData(), pathBytes.size(), SQLITE_STATIC);

    InstallState state;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        state.id             = sqlite3_column_int64(stmt, 0);
        state.fileCreatedAt  = sqlite3_column_int64(stmt, 1);
        state.fileModifiedAt = sqlite3_column_int64(stmt, 2);
        state.fileSize       = sqlite3_column_int64(stmt, 3);
        state.lastByteOffset = sqlite3_column_int64(stmt, 4);
    }
    sqlite3_finalize(stmt);
    return state;
}
