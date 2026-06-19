#include "Database.h"

#include <sqlite3.h>
#include <cstdio>

static constexpr int kDbVersion = 10;

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

static int readUserVersion(sqlite3 *db)
{
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, nullptr);
    int v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        v = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return v;
}

static void setUserVersion(sqlite3 *db, int version)
{
    char sql[48];
    std::snprintf(sql, sizeof(sql), "PRAGMA user_version = %d;", version);
    execSql(db, sql);
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
        CREATE TABLE IF NOT EXISTS areas (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            code         TEXT    NOT NULL UNIQUE,
            level        INTEGER,
            display_name TEXT
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS area_moves (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            install_id INTEGER NOT NULL REFERENCES installs(id),
            area_id    INTEGER NOT NULL REFERENCES areas(id),
            entered_at TEXT    NOT NULL,
            UNIQUE (install_id, entered_at)
        );
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_area_moves_install_time
        ON area_moves(install_id, entered_at);
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS accounts (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            name       TEXT    NOT NULL UNIQUE,
            guild_name TEXT
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS chat_channels (
            id     INTEGER PRIMARY KEY AUTOINCREMENT,
            number INTEGER NOT NULL UNIQUE,
            lang   TEXT,
            name   TEXT
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS classes (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS characters (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            name     TEXT    NOT NULL UNIQUE,
            class_id INTEGER NOT NULL REFERENCES classes(id),
            level    INTEGER
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS character_level_events (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            install_id INTEGER NOT NULL REFERENCES installs(id),
            char_id    INTEGER NOT NULL REFERENCES characters(id),
            level      INTEGER NOT NULL,
            occurred_at TEXT   NOT NULL,
            UNIQUE (install_id, char_id, level)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS chat_channel_joins (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            install_id INTEGER NOT NULL REFERENCES installs(id),
            channel_id INTEGER NOT NULL REFERENCES chat_channels(id),
            joined_at  TEXT    NOT NULL,
            UNIQUE (install_id, joined_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS sessions (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            install_id  INTEGER NOT NULL REFERENCES installs(id),
            started_at  TEXT    NOT NULL,
            ended_at    TEXT,
            total_secs  INTEGER,
            active_secs INTEGER,
            afk_secs    INTEGER,
            account_id  INTEGER REFERENCES accounts(id),
            char_id     INTEGER REFERENCES characters(id),
            area_id     INTEGER REFERENCES areas(id),
            UNIQUE(install_id, started_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS session_afk (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id INTEGER NOT NULL REFERENCES sessions(id),
            afk_on_at  TEXT    NOT NULL,
            afk_off_at TEXT,
            UNIQUE(session_id, afk_on_at)
        );
    )");

    // One row per contiguous period spent in a single area (area_id NULL = character select).
    // char_id is the most recently seen character at the time the span opened, updated on
    // level-up while the span is open.  Duration is computed in SQL on close so we never
    // have to carry entered_at in memory.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS area_time_spans (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id    INTEGER NOT NULL REFERENCES sessions(id),
            area_id       INTEGER REFERENCES areas(id),
            char_id       INTEGER REFERENCES characters(id),
            entered_at    TEXT    NOT NULL,
            exited_at     TEXT,
            duration_secs INTEGER,
            afk_secs      INTEGER NOT NULL DEFAULT 0,
            UNIQUE(session_id, entered_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS passive_skills (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            code       TEXT    NOT NULL UNIQUE,
            name       TEXT    NOT NULL,
            is_mastery INTEGER NOT NULL DEFAULT 0
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS passive_skill_allocations (
            id               INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id       INTEGER NOT NULL REFERENCES sessions(id),
            char_id          INTEGER REFERENCES characters(id),
            passive_skill_id INTEGER NOT NULL REFERENCES passive_skills(id),
            action           TEXT    NOT NULL DEFAULT 'allocated',
            allocated_at     TEXT    NOT NULL,
            UNIQUE(session_id, passive_skill_id, action, allocated_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS whispers (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            direction   TEXT    NOT NULL CHECK(direction IN ('from', 'to')),
            player_name TEXT    NOT NULL,
            message     TEXT    NOT NULL,
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at, direction, player_name)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS quest_events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            area_id     INTEGER REFERENCES areas(id),
            event_type  TEXT    NOT NULL,
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at, event_type)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS character_deaths (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            char_id     INTEGER NOT NULL REFERENCES characters(id),
            area_id     INTEGER REFERENCES areas(id),
            level       INTEGER,
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, char_id, occurred_at)
        );
    )");

    // Players seen only in public chat — name only, no class/level info required.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS public_chars (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS character_played_events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            span_id     INTEGER REFERENCES area_time_spans(id),
            played_secs INTEGER NOT NULL,
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS achievements (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            code TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS achievement_events (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id     INTEGER NOT NULL REFERENCES sessions(id),
            achievement_id INTEGER NOT NULL REFERENCES achievements(id),
            occurred_at    TEXT    NOT NULL,
            UNIQUE(session_id, achievement_id, occurred_at)
        );
    )");

    // Snapshot of /passives output: totals + per-quest breakdown at a point in time.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS passive_point_snapshots (
            id                   INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id           INTEGER NOT NULL REFERENCES sessions(id),
            char_id              INTEGER REFERENCES characters(id),
            occurred_at          TEXT    NOT NULL,
            total_points         INTEGER NOT NULL DEFAULT 0,
            allocated_points     INTEGER NOT NULL DEFAULT 0,
            ascendancy_total     INTEGER NOT NULL DEFAULT 0,
            ascendancy_allocated INTEGER NOT NULL DEFAULT 0,
            level_points         INTEGER NOT NULL DEFAULT 0,
            quest_points         INTEGER NOT NULL DEFAULT 0,
            UNIQUE(session_id, occurred_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS passive_quest_sources (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS passive_snapshot_quests (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            snapshot_id INTEGER NOT NULL REFERENCES passive_point_snapshots(id),
            quest_id    INTEGER NOT NULL REFERENCES passive_quest_sources(id),
            points      INTEGER NOT NULL DEFAULT 1,
            UNIQUE(snapshot_id, quest_id)
        );
    )");

    // channel stores the raw prefix character: '#' global, '$' trade, '%' party, '&' guild.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS chats (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id     INTEGER NOT NULL REFERENCES sessions(id),
            public_char_id INTEGER NOT NULL REFERENCES public_chars(id),
            channel        TEXT    NOT NULL,
            message        TEXT    NOT NULL,
            occurred_at    TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at, public_char_id, channel)
        );
    )");

    const int version = readUserVersion(m_db);
    if (version == 0)
        setUserVersion(m_db, kDbVersion);
    else if (version < kDbVersion)
        migrate(version);
}

void Database::migrate(int fromVersion)
{
    // v1→v2: quest_events; v2→v3: passive_skills + passive_skill_allocations;
    // v3→v4: whispers; v4→v5: passive_skills.is_mastery, passive_skill_allocations.action;
    // v5→v6: character_deaths; v6→v7: public_chars + chats; v7→v8: achievements + achievement_events;
    // v8→v9: character_played_events + characters.played_secs;
    // v9→v10: passive_point_snapshots + passive_quest_sources + passive_snapshot_quests.
    if (fromVersion < 5) {
        execSql(m_db, "ALTER TABLE passive_skills ADD COLUMN is_mastery INTEGER NOT NULL DEFAULT 0;");
        execSql(m_db, "ALTER TABLE passive_skill_allocations ADD COLUMN action TEXT NOT NULL DEFAULT 'allocated';");
    }
    if (fromVersion < 6) {
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS character_deaths (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id  INTEGER NOT NULL REFERENCES sessions(id),
                char_id     INTEGER NOT NULL REFERENCES characters(id),
                area_id     INTEGER REFERENCES areas(id),
                level       INTEGER,
                occurred_at TEXT    NOT NULL,
                UNIQUE(session_id, char_id, occurred_at)
            );
        )");
    }
    if (fromVersion < 7) {
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS public_chars (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT    NOT NULL UNIQUE
            );
        )");
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS chats (
                id             INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id     INTEGER NOT NULL REFERENCES sessions(id),
                public_char_id INTEGER NOT NULL REFERENCES public_chars(id),
                channel        TEXT    NOT NULL,
                message        TEXT    NOT NULL,
                occurred_at    TEXT    NOT NULL,
                UNIQUE(session_id, occurred_at, public_char_id, channel)
            );
        )");
    }
    if (fromVersion < 8) {
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS achievements (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                code TEXT    NOT NULL UNIQUE
            );
        )");
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS achievement_events (
                id             INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id     INTEGER NOT NULL REFERENCES sessions(id),
                achievement_id INTEGER NOT NULL REFERENCES achievements(id),
                occurred_at    TEXT    NOT NULL,
                UNIQUE(session_id, achievement_id, occurred_at)
            );
        )");
    }
    if (fromVersion < 9) {
        execSql(m_db, "ALTER TABLE characters ADD COLUMN played_secs INTEGER NOT NULL DEFAULT 0;");
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS character_played_events (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id  INTEGER NOT NULL REFERENCES sessions(id),
                span_id     INTEGER REFERENCES area_time_spans(id),
                played_secs INTEGER NOT NULL,
                occurred_at TEXT    NOT NULL,
                UNIQUE(session_id, occurred_at)
            );
        )");
    }
    if (fromVersion < 10) {
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS passive_point_snapshots (
                id                   INTEGER PRIMARY KEY AUTOINCREMENT,
                session_id           INTEGER NOT NULL REFERENCES sessions(id),
                char_id              INTEGER REFERENCES characters(id),
                occurred_at          TEXT    NOT NULL,
                total_points         INTEGER NOT NULL DEFAULT 0,
                allocated_points     INTEGER NOT NULL DEFAULT 0,
                ascendancy_total     INTEGER NOT NULL DEFAULT 0,
                ascendancy_allocated INTEGER NOT NULL DEFAULT 0,
                level_points         INTEGER NOT NULL DEFAULT 0,
                quest_points         INTEGER NOT NULL DEFAULT 0,
                UNIQUE(session_id, occurred_at)
            );
        )");
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS passive_quest_sources (
                id   INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT    NOT NULL UNIQUE
            );
        )");
        execSql(m_db, R"(
            CREATE TABLE IF NOT EXISTS passive_snapshot_quests (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                snapshot_id INTEGER NOT NULL REFERENCES passive_point_snapshots(id),
                quest_id    INTEGER NOT NULL REFERENCES passive_quest_sources(id),
                points      INTEGER NOT NULL DEFAULT 1,
                UNIQUE(snapshot_id, quest_id)
            );
        )");
    }
    setUserVersion(m_db, kDbVersion);
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
