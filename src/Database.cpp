#include "Database.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstdio>
#include <QDate>
#include <QDebug>
#include <QElapsedTimer>
#include <QHash>

static constexpr int kDbVersion = 3;

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
    // No busy_timeout here: the main thread must never block waiting for
    // a SQLite lock — SQLITE_BUSY should fail immediately so the UI stays
    // responsive.  The ingest worker sets its own busy_timeout separately.
}

// Keep docs/schema.md in sync with any changes made here.
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
            guild_id    INTEGER REFERENCES guilds(id),
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

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS guilds (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            tag  TEXT    NOT NULL UNIQUE,
            name TEXT    NOT NULL DEFAULT ''
        );
    )");

    // Players seen only in public chat or whispers — name only, no class/level info required.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS public_chars (
            id       INTEGER PRIMARY KEY AUTOINCREMENT,
            name     TEXT    NOT NULL UNIQUE,
            guild_id INTEGER REFERENCES guilds(id)
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

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS hideouts (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS hideout_discovered_events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            hideout_id  INTEGER NOT NULL REFERENCES hideouts(id),
            area_id     INTEGER REFERENCES areas(id),
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, hideout_id, occurred_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS pvp_matches (
            id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT    NOT NULL UNIQUE
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS pvp_queue_events (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id   INTEGER NOT NULL REFERENCES sessions(id),
            match_id     INTEGER NOT NULL REFERENCES pvp_matches(id),
            player_count INTEGER NOT NULL DEFAULT 0,
            occurred_at  TEXT    NOT NULL,
            cancelled_at TEXT,
            UNIQUE(session_id, occurred_at)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS guild_members (
            id         INTEGER PRIMARY KEY AUTOINCREMENT,
            guild_name TEXT    NOT NULL,
            account_id INTEGER NOT NULL REFERENCES accounts(id),
            UNIQUE(guild_name, account_id)
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS zone_ruleset_failed_events (
            id           INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id   INTEGER NOT NULL REFERENCES sessions(id),
            area_id      INTEGER REFERENCES areas(id),
            ruleset_name TEXT    NOT NULL,
            occurred_at  TEXT    NOT NULL,
            UNIQUE(session_id, ruleset_name, occurred_at)
        );
    )");

    // channel stores the raw prefix character: '#' global, '$' trade, '%' party, '&' guild.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS chats (
            id             INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id     INTEGER NOT NULL REFERENCES sessions(id),
            public_char_id INTEGER NOT NULL REFERENCES public_chars(id),
            channel        TEXT    NOT NULL,
            guild_id       INTEGER REFERENCES guilds(id),
            message        TEXT    NOT NULL,
            occurred_at    TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at, public_char_id, channel)
        );
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_chats_by_time
        ON chats(occurred_at DESC, channel);
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_whispers_by_time
        ON whispers(occurred_at DESC);
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS general_events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id  INTEGER NOT NULL REFERENCES sessions(id),
            area_id     INTEGER REFERENCES areas(id),
            event_type  TEXT    NOT NULL,
            occurred_at TEXT    NOT NULL,
            UNIQUE(session_id, occurred_at, event_type)
        );
    )");

    // Operational metadata: arbitrary key→value pairs for the app's own bookkeeping
    // (maintenance timestamps, etc.). Not user configuration; not game data.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS npc_dialog_entries (
            message_hash  TEXT NOT NULL PRIMARY KEY,
            npc_name      TEXT NOT NULL,
            npc_name_hash TEXT NOT NULL,
            label         TEXT
        );
    )");

    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS app_state (
            key   TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
    )");

    // Chronological spine for the historical events panel. One row per event regardless
    // of type; source_id is the rowid in the type-specific table. Indexed on occurred_at
    // so paginated queries never scan the full union of event tables.
    // UNIQUE(event_type, source_id) makes INSERT OR IGNORE safe on re-ingest.
    execSql(m_db, R"(
        CREATE TABLE IF NOT EXISTS events (
            id          INTEGER PRIMARY KEY AUTOINCREMENT,
            occurred_at TEXT    NOT NULL,
            event_type  TEXT    NOT NULL,
            source_id   INTEGER NOT NULL,
            UNIQUE(event_type, source_id)
        );
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_events_by_time ON events (occurred_at DESC);
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_sessions_started_at ON sessions (started_at DESC);
    )");

    execSql(m_db, R"(
        CREATE INDEX IF NOT EXISTS idx_sessions_ended_at
        ON sessions (ended_at DESC) WHERE ended_at IS NOT NULL;
    )");

    const int version = readUserVersion(m_db);
    if (version == 0)
        setUserVersion(m_db, kDbVersion);
    else if (version < kDbVersion)
        migrate(version);
}

QList<Database::WhisperRecord> Database::fetchWhispers(const QString &playerFilter, int limit) const
{
    if (!m_db) return {};

    sqlite3_stmt *stmt = nullptr;
    QByteArray nameBytes;

    // When a limit is requested, fetch DESC so we get the most recent N rows,
    // then reverse in memory to restore chronological order for display.
    const bool useLimit = limit > 0;
    const char *order = useLimit ? "DESC" : "ASC";

    char sql[256];
    if (playerFilter.isEmpty()) {
        if (useLimit)
            std::snprintf(sql, sizeof(sql),
                "SELECT w.direction, w.player_name, g.tag, w.message, w.occurred_at "
                "FROM whispers w LEFT JOIN guilds g ON g.id = w.guild_id "
                "ORDER BY w.occurred_at %s LIMIT %d;", order, limit);
        else
            std::snprintf(sql, sizeof(sql),
                "SELECT w.direction, w.player_name, g.tag, w.message, w.occurred_at "
                "FROM whispers w LEFT JOIN guilds g ON g.id = w.guild_id "
                "ORDER BY w.occurred_at ASC;");
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
    } else {
        nameBytes = playerFilter.toUtf8();
        if (useLimit)
            std::snprintf(sql, sizeof(sql),
                "SELECT w.direction, w.player_name, g.tag, w.message, w.occurred_at "
                "FROM whispers w LEFT JOIN guilds g ON g.id = w.guild_id "
                "WHERE w.player_name = ? ORDER BY w.occurred_at %s LIMIT %d;", order, limit);
        else
            std::snprintf(sql, sizeof(sql),
                "SELECT w.direction, w.player_name, g.tag, w.message, w.occurred_at "
                "FROM whispers w LEFT JOIN guilds g ON g.id = w.guild_id "
                "WHERE w.player_name = ? ORDER BY w.occurred_at ASC;");
        sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        sqlite3_bind_text(stmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
    }

    QList<WhisperRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        WhisperRecord r;
        r.direction  = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        r.playerName = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
        if (const auto *g = sqlite3_column_text(stmt, 2))
            r.guildTag = QString::fromUtf8(reinterpret_cast<const char *>(g));
        r.message    = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
        r.occurredAt = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4)));
        result.append(r);
    }
    sqlite3_finalize(stmt);

    if (useLimit)
        std::reverse(result.begin(), result.end());

    return result;
}

QStringList Database::fetchWhisperPartners() const
{
    if (!m_db) return {};

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT player_name FROM whispers "
        "GROUP BY player_name ORDER BY MAX(occurred_at) DESC;",
        -1, &stmt, nullptr);

    QStringList result;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::PartnerRecord> Database::fetchWhisperPartnersWithDates() const
{
    if (!m_db) return {};

    // Pass 1: partners in most-recently-active order.
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "SELECT player_name FROM whispers "
        "GROUP BY player_name ORDER BY MAX(occurred_at) DESC;",
        -1, &stmt, nullptr);

    QStringList ordered;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        ordered << QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    sqlite3_finalize(stmt);

    // Pass 2: all distinct (player, date) pairs, most-recent date first per player.
    sqlite3_prepare_v2(m_db,
        "SELECT DISTINCT player_name, date(occurred_at) "
        "FROM whispers ORDER BY player_name ASC, date(occurred_at) DESC;",
        -1, &stmt, nullptr);

    QHash<QString, QStringList> dateMap;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString nm = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        const QString dt = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1)));
        dateMap[nm] << dt;
    }
    sqlite3_finalize(stmt);

    QList<PartnerRecord> result;
    result.reserve(ordered.size());
    for (const QString &nm : ordered)
        result << PartnerRecord{nm, dateMap.value(nm)};
    return result;
}

// Safely builds a SQL IN clause from a set of known single-character values.
static QString buildInClause(const QSet<QChar> &chars)
{
    QString result;
    for (QChar ch : chars) {
        if (!result.isEmpty()) result += ',';
        result += '\'';
        result += ch;
        result += '\'';
    }
    return result;
}

static QString colStr(sqlite3_stmt *stmt, int col)
{
    const auto *p = sqlite3_column_text(stmt, col);
    return p ? QString::fromUtf8(reinterpret_cast<const char *>(p)) : QString{};
}

QList<Database::ChatRecord> Database::fetchChats(
    const QSet<QChar> &channels, bool includeDms,
    int limit, const QString &fromDate, const QString &toDate) const
{
    if (!m_db) return {};
    if (channels.isEmpty() && !includeDms) return {};

    const bool useLimit  = limit > 0;
    const bool hasRange  = !fromDate.isEmpty();
    const bool hasChans  = !channels.isEmpty();

    // Date bounds: "YYYY-MM-DD HH:MM:SS" strings, safe to embed (ISO format, DB-sourced).
    const QString fromTs = hasRange ? (fromDate + " 00:00:00") : QString{};
    const QString toTs   = hasRange
        ? (QDate::fromString(toDate, Qt::ISODate).addDays(1).toString(Qt::ISODate) + " 00:00:00")
        : QString{};

    // Build UNION ALL from the required sources.
    QString sql = "SELECT source,channel,player_name,guild_tag,message,occurred_at FROM (";

    bool first = true;
    if (hasChans) {
        const QString inClause = buildInClause(channels);
        sql += "SELECT 'chat' AS source,c.channel,pc.name AS player_name,"
               "COALESCE(g.tag,'') AS guild_tag,c.message,c.occurred_at "
               "FROM chats c "
               "JOIN public_chars pc ON pc.id=c.public_char_id "
               "LEFT JOIN guilds g ON g.id=c.guild_id "
               "WHERE c.channel IN (" + inClause + ")";
        if (hasRange)
            sql += " AND c.occurred_at>='" + fromTs + "' AND c.occurred_at<'" + toTs + "'";
        first = false;
    }

    if (includeDms) {
        if (!first) sql += " UNION ALL ";
        sql += "SELECT 'dm',"
               "CASE direction WHEN 'from' THEN '@from' ELSE '@to' END,"
               "w.player_name,COALESCE(g.tag,''),w.message,w.occurred_at "
               "FROM whispers w LEFT JOIN guilds g ON g.id=w.guild_id";
        if (hasRange)
            sql += " WHERE w.occurred_at>='" + fromTs + "' AND w.occurred_at<'" + toTs + "'";
    }

    sql += ") ORDER BY occurred_at DESC";
    if (useLimit) sql += QStringLiteral(" LIMIT %1").arg(limit);
    sql += ";";

    sqlite3_stmt *stmt = nullptr;
    const QByteArray sqlBytes = sql.toUtf8();
    sqlite3_prepare_v2(m_db, sqlBytes.constData(), -1, &stmt, nullptr);

    QList<ChatRecord> result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ChatRecord r;
        r.source     = colStr(stmt, 0);
        r.channel    = colStr(stmt, 1);
        r.playerName = colStr(stmt, 2);
        r.guildTag   = colStr(stmt, 3);
        r.message    = colStr(stmt, 4);
        r.occurredAt = colStr(stmt, 5);
        result.append(r);
    }
    sqlite3_finalize(stmt);

    if (useLimit)
        std::reverse(result.begin(), result.end());

    return result;
}

QStringList Database::fetchChatDates(const QSet<QChar> &channels, bool includeDms) const
{
    if (!m_db) return {};
    if (channels.isEmpty() && !includeDms) return {};

    QString sql;
    if (!channels.isEmpty() && includeDms) {
        const QString inClause = buildInClause(channels);
        sql = "SELECT DISTINCT date(occurred_at) FROM ("
              "SELECT occurred_at FROM chats WHERE channel IN (" + inClause + ")"
              " UNION ALL SELECT occurred_at FROM whispers) ORDER BY 1 DESC;";
    } else if (!channels.isEmpty()) {
        const QString inClause = buildInClause(channels);
        sql = "SELECT DISTINCT date(occurred_at) FROM chats"
              " WHERE channel IN (" + inClause + ") ORDER BY 1 DESC;";
    } else {
        sql = "SELECT DISTINCT date(occurred_at) FROM whispers ORDER BY 1 DESC;";
    }

    sqlite3_stmt *stmt = nullptr;
    const QByteArray sqlBytes = sql.toUtf8();
    sqlite3_prepare_v2(m_db, sqlBytes.constData(), -1, &stmt, nullptr);

    QStringList result;
    while (sqlite3_step(stmt) == SQLITE_ROW)
        result << colStr(stmt, 0);
    sqlite3_finalize(stmt);
    return result;
}

void Database::migrate(int fromVersion)
{
    // Only add migrations here once the "Public release" item in ROADMAP.md is checked off.
    // Until then the app is unreleased and all users start from a fresh database.
    QElapsedTimer t; t.start();
    qDebug() << "[DB] migrate from" << fromVersion;
    if (fromVersion < 3) {
        // guilds table is created by initSchema via CREATE TABLE IF NOT EXISTS.
        // guild_tag columns in old databases are left in place but unused.
        qDebug() << "[DB] ALTER whispers +guild_id";
        execSql(m_db, "ALTER TABLE whispers ADD COLUMN guild_id INTEGER REFERENCES guilds(id);");
        qDebug() << "[DB] ALTER public_chars +guild_id";
        execSql(m_db, "ALTER TABLE public_chars ADD COLUMN guild_id INTEGER REFERENCES guilds(id);");
        qDebug() << "[DB] ALTER chats +guild_id";
        execSql(m_db, "ALTER TABLE chats ADD COLUMN guild_id INTEGER REFERENCES guilds(id);");
    }
    setUserVersion(m_db, kDbVersion);
    qDebug() << "[DB] migrate done in" << t.elapsed() << "ms";
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

int Database::upsertNpcDialogEntries(const QList<NpcDialogEntry> &entries)
{
    if (!m_db || entries.isEmpty()) return 0;

    execSql(m_db, "BEGIN;");

    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT OR IGNORE INTO npc_dialog_entries (message_hash, npc_name, npc_name_hash) "
        "VALUES (?, ?, ?);",
        -1, &stmt, nullptr);

    int inserted = 0;
    for (const NpcDialogEntry &e : entries) {
        const QByteArray mh  = e.messageHash.toUtf8();
        const QByteArray nm  = e.npcName.toUtf8();
        const QByteArray nmh = e.npcNameHash.toUtf8();
        sqlite3_bind_text(stmt, 1, mh.constData(),  mh.size(),  SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, nm.constData(),  nm.size(),  SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, nmh.constData(), nmh.size(), SQLITE_STATIC);
        if (sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(m_db) > 0)
            ++inserted;
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    execSql(m_db, "COMMIT;");
    return inserted;
}

QList<Database::SessionRecord> Database::fetchSessions() const
{
    QList<SessionRecord> result;
    if (!m_db) return result;

    static const char *sql = R"(
        SELECT s.id, s.started_at, s.ended_at, s.total_secs, s.active_secs,
               a.name, c.name, cl.name
        FROM sessions s
        LEFT JOIN accounts a  ON s.account_id = a.id
        LEFT JOIN characters c ON s.char_id   = c.id
        LEFT JOIN classes cl   ON c.class_id  = cl.id
        ORDER BY s.started_at ASC
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionRecord r;
        r.id = sqlite3_column_int64(stmt, 0);
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.startedAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 2))
            r.endedAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.totalSecs  = sqlite3_column_type(stmt, 3) != SQLITE_NULL
                       ? sqlite3_column_int(stmt, 3) : -1;
        r.activeSecs = sqlite3_column_type(stmt, 4) != SQLITE_NULL
                       ? sqlite3_column_int(stmt, 4) : -1;
        if (auto *p = sqlite3_column_text(stmt, 5))
            r.accountName = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 6))
            r.charName = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 7))
            r.charClass = QString::fromUtf8(reinterpret_cast<const char *>(p));
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::SessionEventRecord> Database::fetchSessionEvents(int limit) const
{
    QList<SessionEventRecord> result;
    if (!m_db) return result;

    // Two separate indexed queries instead of a single UNION ALL: the UNION ALL
    // forces SQLite to materialise the entire sessions table twice and sort it,
    // giving O(N) cost even for LIMIT 1.  Running one query per event type lets
    // SQLite use idx_sessions_started_at and idx_sessions_ended_at so each scan
    // is O(log N + limit).  We merge the two DESC result sets in C++.
    static const char *startSql = R"(
        SELECT 'start'       AS event_type,
               s.started_at  AS occurred_at,
               c.name        AS char_name,
               cl.name       AS char_class,
               i.path        AS install_path,
               s.active_secs,
               s.total_secs
        FROM sessions s
        JOIN installs i        ON s.install_id = i.id
        LEFT JOIN characters c ON s.char_id    = c.id
        LEFT JOIN classes cl   ON c.class_id   = cl.id
        ORDER BY s.started_at DESC
        LIMIT ?
    )";

    static const char *stopSql = R"(
        SELECT 'stop'      AS event_type,
               s.ended_at  AS occurred_at,
               c.name      AS char_name,
               cl.name     AS char_class,
               i.path      AS install_path,
               s.active_secs,
               s.total_secs
        FROM sessions s
        JOIN installs i        ON s.install_id = i.id
        LEFT JOIN characters c ON s.char_id    = c.id
        LEFT JOIN classes cl   ON c.class_id   = cl.id
        WHERE s.ended_at IS NOT NULL
        ORDER BY s.ended_at DESC
        LIMIT ?
    )";

    const int bindLimit = limit > 0 ? limit : -1;

    auto runQuery = [&](const char *sql, QList<SessionEventRecord> &out) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return;
        sqlite3_bind_int(stmt, 1, bindLimit);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            SessionEventRecord r;
            if (auto *p = sqlite3_column_text(stmt, 0))
                r.eventType   = QString::fromUtf8(reinterpret_cast<const char *>(p));
            if (auto *p = sqlite3_column_text(stmt, 1))
                r.occurredAt  = QString::fromUtf8(reinterpret_cast<const char *>(p));
            if (auto *p = sqlite3_column_text(stmt, 2))
                r.charName    = QString::fromUtf8(reinterpret_cast<const char *>(p));
            if (auto *p = sqlite3_column_text(stmt, 3))
                r.charClass   = QString::fromUtf8(reinterpret_cast<const char *>(p));
            if (auto *p = sqlite3_column_text(stmt, 4))
                r.installPath = QString::fromUtf8(reinterpret_cast<const char *>(p));
            r.activeSecs = sqlite3_column_type(stmt, 5) != SQLITE_NULL
                           ? sqlite3_column_int(stmt, 5) : -1;
            r.totalSecs  = sqlite3_column_type(stmt, 6) != SQLITE_NULL
                           ? sqlite3_column_int(stmt, 6) : -1;
            out.append(r);
        }
        sqlite3_finalize(stmt);
    };

    QList<SessionEventRecord> starts, stops;
    runQuery(startSql, starts);
    runQuery(stopSql,  stops);

    // Merge two DESC-sorted lists into a DESC-sorted result, keeping the top `limit`.
    int i = 0, j = 0;
    while ((limit <= 0 || result.size() < limit) && (i < starts.size() || j < stops.size())) {
        const bool takeStart = (j >= stops.size()) ||
            (i < starts.size() && starts[i].occurredAt >= stops[j].occurredAt);
        result.append(takeStart ? starts[i++] : stops[j++]);
    }
    // Callers expect ASC (oldest first); we collected DESC above, so reverse.
    std::reverse(result.begin(), result.end());
    return result;
}

int Database::closeOrphanSessions(const QStringList &runningInstallPaths)
{
    if (!m_db) return 0;

    // Collect sessions that have no ended_at, along with their install path.
    struct Dangling { qint64 id; QString installPath; };
    QList<Dangling> dangling;

    static const char *selectSql = R"(
        SELECT s.id, i.path
        FROM sessions s
        JOIN installs i ON s.install_id = i.id
        WHERE s.ended_at IS NULL
    )";

    sqlite3_stmt *sel = nullptr;
    if (sqlite3_prepare_v2(m_db, selectSql, -1, &sel, nullptr) != SQLITE_OK)
        return 0;
    while (sqlite3_step(sel) == SQLITE_ROW) {
        Dangling d;
        d.id = sqlite3_column_int64(sel, 0);
        if (auto *p = sqlite3_column_text(sel, 1))
            d.installPath = QString::fromUtf8(reinterpret_cast<const char *>(p));
        dangling.append(d);
    }
    sqlite3_finalize(sel);

    // Filter: skip sessions whose install is currently running.
    QList<qint64> toClose;
    for (const auto &d : dangling)
        if (!runningInstallPaths.contains(d.installPath))
            toClose.append(d.id);

    if (toClose.isEmpty()) return 0;

    // Close each orphaned session. We don't know the exact stop time, so we
    // use the current local time and leave total_secs/active_secs as NULL.
    // The AND ended_at IS NULL guard makes the update safe against concurrent writers.
    static const char *updateSql = R"(
        UPDATE sessions
        SET ended_at = datetime('now', 'localtime')
        WHERE id = ? AND ended_at IS NULL
    )";

    sqlite3_stmt *upd = nullptr;
    if (sqlite3_prepare_v2(m_db, updateSql, -1, &upd, nullptr) != SQLITE_OK)
        return 0;

    int closed = 0;
    for (qint64 sid : toClose) {
        sqlite3_bind_int64(upd, 1, sid);
        if (sqlite3_step(upd) == SQLITE_DONE && sqlite3_changes(m_db) > 0)
            ++closed;
        sqlite3_reset(upd);
    }
    sqlite3_finalize(upd);
    return closed;
}

QList<Database::ZoneTransitionRecord> Database::fetchZoneTransitions(int limit, int offset) const
{
    QList<ZoneTransitionRecord> result;
    if (!m_db) return result;

    static const char *sql = R"(
        SELECT COALESCE(a.display_name, a.code), a.level, ats.entered_at, ats.duration_secs
        FROM area_time_spans ats
        LEFT JOIN areas a ON ats.area_id = a.id
        WHERE ats.session_id = (
            SELECT id FROM sessions ORDER BY started_at DESC LIMIT 1
        )
        AND ats.area_id IS NOT NULL
        ORDER BY ats.entered_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : -1);
    sqlite3_bind_int(stmt, 2, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ZoneTransitionRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.areaName = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.areaLevel = sqlite3_column_type(stmt, 1) != SQLITE_NULL
                      ? sqlite3_column_int(stmt, 1) : 0;
        if (auto *p = sqlite3_column_text(stmt, 2))
            r.enteredAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 3) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 3) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}
