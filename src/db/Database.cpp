#include "db/Database.h"

#include <sqlite3.h>
#include <algorithm>
#include <cstdio>
#include <QDate>
#include <QElapsedTimer>
#include <QFile>
#include <QHash>

static constexpr int kDbVersion = 5;

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

static void execResource(sqlite3 *db, const char *path)
{
    QFile f(QString::fromLatin1(path));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning("[db] cannot open resource %s", path);
        return;
    }
    const QByteArray sql = f.readAll();
    char *err = nullptr;
    sqlite3_exec(db, sql.constData(), nullptr, nullptr, &err);
    if (err) {
        qWarning("[db] %s: %s", path, err);
        sqlite3_free(err);
    }
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

Database::Database(const QString &path, bool readOnly)
    : m_path(path)
    , m_readOnly(readOnly)
{
    const QByteArray utf8 = path.toUtf8();
    const int flags = readOnly
        ? SQLITE_OPEN_READONLY
        : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    if (sqlite3_open_v2(utf8.constData(), &m_db, flags, nullptr) != SQLITE_OK) {
        m_lastError = QString::fromUtf8(sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    applyPragmas();
    if (!readOnly)
        initSchema();
}

Database::~Database()
{
    if (m_db)
        sqlite3_close(m_db);
}

void Database::applyPragmas()
{
    // Read-only connections inherit WAL mode from the main connection and cannot
    // change journal mode. Skip write-specific pragmas entirely.
    if (!m_readOnly) {
        execSql(m_db, "PRAGMA journal_mode=WAL;");
        // No busy_timeout: the main thread must never block waiting for a SQLite
        // lock — SQLITE_BUSY should fail immediately so the UI stays responsive.
        // The ingest worker sets its own busy_timeout separately.
    }
    execSql(m_db, "PRAGMA foreign_keys=ON;");
    execSql(m_db, "PRAGMA synchronous=NORMAL;");
    execSql(m_db, "PRAGMA temp_store=MEMORY;");
    execSql(m_db, "PRAGMA cache_size=-65536;");

    if (!m_readOnly) {
        // Per-query budget: interrupt any query that exceeds kQueryBudgetMs on
        // the UI thread. The handler is a no-op until armQueryBudget() is called
        // so DDL and schema init during construction are unaffected.
        sqlite3_progress_handler(m_db, 1000, &Database::s_queryProgressHandler, this);
    }
}

void Database::armQueryBudget() const
{
    m_queryTimer.start();
}

int Database::s_queryProgressHandler(void *ctx)
{
    const auto *db = static_cast<const Database *>(ctx);
    if (!db->m_queryTimer.isValid())
        return 0;
    const qint64 ms = db->m_queryTimer.elapsed();
    if (ms > kQueryBudgetMs) {
        qWarning("[db] query exceeded budget — %lld ms (limit %lld ms), interrupting",
                 static_cast<long long>(ms), static_cast<long long>(kQueryBudgetMs));
        return 1;
    }
    return 0;
}

void Database::initSchema()
{
    execResource(m_db, ":/db/schema.sql");

    const int version = readUserVersion(m_db);
    if (version == 0) {
        execResource(m_db, ":/db/seed.sql");
        setUserVersion(m_db, kDbVersion);
    } else if (version < kDbVersion) {
        migrate(version);
    }
}

QList<Database::WhisperRecord> Database::fetchWhispers(const QString &playerFilter, int limit, int offset) const
{
    if (!m_db) return {};
    armQueryBudget();

    sqlite3_stmt *stmt = nullptr;
    QByteArray nameBytes;

    // Fetch DESC to get the most recent rows, with optional OFFSET to skip the newest ones,
    // then reverse in memory to restore chronological order for display.
    const bool useLimit = limit > 0;
    const char *order = useLimit ? "DESC" : "ASC";

    char sql[300];
    if (playerFilter.isEmpty()) {
        if (useLimit)
            std::snprintf(sql, sizeof(sql),
                "SELECT w.direction, w.player_name, g.tag, w.message, w.occurred_at "
                "FROM whispers w LEFT JOIN guilds g ON g.id = w.guild_id "
                "ORDER BY w.occurred_at %s LIMIT %d OFFSET %d;", order, limit, offset);
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
                "WHERE w.player_name = ? ORDER BY w.occurred_at %s LIMIT %d OFFSET %d;", order, limit, offset);
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
    armQueryBudget();

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
    armQueryBudget();

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
    int limit, const QString &fromDate, const QString &toDate, int offset) const
{
    if (!m_db) return {};
    if (channels.isEmpty() && !includeDms) return {};
    armQueryBudget();

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
    if (useLimit) {
        sql += QStringLiteral(" LIMIT %1").arg(limit);
        if (offset > 0) sql += QStringLiteral(" OFFSET %1").arg(offset);
    }
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
    armQueryBudget();

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
    if (fromVersion < 5) {
        execSql(m_db,
            "CREATE TABLE IF NOT EXISTS session_alt_tabs ("
            "    id         INTEGER PRIMARY KEY AUTOINCREMENT,"
            "    session_id INTEGER NOT NULL REFERENCES sessions(id),"
            "    out_at     TEXT    NOT NULL,"
            "    in_at      TEXT,"
            "    UNIQUE(session_id, out_at)"
            ");");
        setUserVersion(m_db, 5);
        fromVersion = 5;
    }

    if (fromVersion < kDbVersion) {
        qWarning("[DB] stale schema version %d, expected %d — delete the database to start fresh",
                 fromVersion, kDbVersion);
        setUserVersion(m_db, kDbVersion);
    }
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

QList<Database::SessionRecord> Database::fetchSessions(int limit, int offset) const
{
    QList<SessionRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT s.id, s.started_at, s.ended_at, s.total_secs, s.active_secs,
               a.name, c.name, cl.name, i.path
        FROM sessions s
        JOIN installs i         ON s.install_id = i.id
        LEFT JOIN accounts a    ON s.account_id = a.id
        LEFT JOIN characters c  ON s.char_id    = c.id
        LEFT JOIN classes cl    ON c.class_id   = cl.id
        ORDER BY s.started_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
        return result;
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : -1);
    sqlite3_bind_int(stmt, 2, offset);

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
        if (auto *p = sqlite3_column_text(stmt, 8))
            r.installPath = QString::fromUtf8(reinterpret_cast<const char *>(p));
        result.append(r);
    }
    sqlite3_finalize(stmt);
    // Fetched DESC; reverse so callers receive oldest-first (chronological) order.
    std::reverse(result.begin(), result.end());
    return result;
}

QList<Database::SessionEventRecord> Database::fetchSessionEvents(int limit, int offset) const
{
    QList<SessionEventRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    // Two separate indexed queries instead of a single UNION ALL: the UNION ALL
    // forces SQLite to materialise the entire sessions table twice and sort it,
    // giving O(N) cost even for LIMIT 1.  Running one query per event type lets
    // SQLite use idx_sessions_started_at and idx_sessions_ended_at so each scan
    // is O(log N + limit).  We merge the two DESC result sets in C++.
    // Offset is handled by fetching limit+offset rows and slicing after merge.
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

    const int fetchCount = (limit > 0 && offset > 0) ? (limit + offset) : limit;
    const int bindLimit = fetchCount > 0 ? fetchCount : -1;

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

    // Merge two DESC-sorted lists into a DESC-sorted result, keeping the top `fetchCount`.
    int i = 0, j = 0;
    while ((fetchCount <= 0 || result.size() < fetchCount) && (i < starts.size() || j < stops.size())) {
        const bool takeStart = (j >= stops.size()) ||
            (i < starts.size() && starts[i].occurredAt >= stops[j].occurredAt);
        result.append(takeStart ? starts[i++] : stops[j++]);
    }
    // Apply offset by dropping the first `offset` entries (newest ones, since we're DESC).
    if (offset > 0 && offset < result.size())
        result = result.mid(offset);
    else if (offset > 0)
        result.clear();
    // Callers expect ASC (oldest first); we collected DESC above, so reverse.
    std::reverse(result.begin(), result.end());
    return result;
}

int Database::closeOrphanSessions(const QStringList &runningInstallPaths)
{
    if (!m_db) return 0;
    armQueryBudget();

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
    armQueryBudget();

    static const char *sql = R"(
        SELECT COALESCE(a.display_name, a.code), a.code, a.type, a.subtype, a.level, ats.entered_at, ats.duration_secs
        FROM area_time_spans ats
        LEFT JOIN areas a ON ats.area_id = a.id
        WHERE ats.session_id = (
            SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1
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
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.areaCode = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 2))
            r.areaType = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 3))
            r.areaSubtype = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.areaLevel = sqlite3_column_type(stmt, 4) != SQLITE_NULL
                      ? sqlite3_column_int(stmt, 4) : 0;
        if (auto *p = sqlite3_column_text(stmt, 5))
            r.enteredAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 6) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 6) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::ZoneTransitionRecord> Database::fetchZoneTransitions(int limit, int offset, qint64 sessionId) const
{
    QList<ZoneTransitionRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT COALESCE(a.display_name, a.code), a.code, a.type, a.subtype, a.level, ats.entered_at, ats.duration_secs
        FROM area_time_spans ats
        LEFT JOIN areas a ON ats.area_id = a.id
        WHERE ats.session_id = ?
        AND ats.area_id IS NOT NULL
        ORDER BY ats.entered_at DESC
        LIMIT ? OFFSET ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, sessionId);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : -1);
    sqlite3_bind_int(stmt, 3, offset);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ZoneTransitionRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.areaName = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.areaCode = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 2))
            r.areaType = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 3))
            r.areaSubtype = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.areaLevel = sqlite3_column_type(stmt, 4) != SQLITE_NULL
                      ? sqlite3_column_int(stmt, 4) : 0;
        if (auto *p = sqlite3_column_text(stmt, 5))
            r.enteredAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 6) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 6) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::AltTabRecord> Database::fetchAltTabRecords(int limit) const
{
    QList<AltTabRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT out_at, in_at,
               CAST((strftime('%s', in_at) - strftime('%s', out_at)) AS INTEGER)
        FROM session_alt_tabs
        WHERE session_id = (
            SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1
        )
        ORDER BY out_at DESC
        LIMIT ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : -1);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AltTabRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.outAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.inAt  = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 2) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::AltTabRecord> Database::fetchAltTabRecords(int limit, qint64 sessionId) const
{
    QList<AltTabRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT out_at, in_at,
               CAST((strftime('%s', in_at) - strftime('%s', out_at)) AS INTEGER)
        FROM session_alt_tabs
        WHERE session_id = ?
        ORDER BY out_at DESC
        LIMIT ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, sessionId);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : -1);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AltTabRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.outAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.inAt  = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 2) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::ClientScreenEventRecord> Database::fetchClientScreenEvents() const
{
    QList<ClientScreenEventRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT cse.event_type, cse.occurred_at
        FROM client_screen_events cse
        JOIN sessions s ON cse.install_id = s.install_id
        WHERE s.ended_at IS NULL
          AND s.id = (SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1)
          AND cse.occurred_at >= s.started_at
        ORDER BY cse.occurred_at DESC
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClientScreenEventRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.eventType  = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.occurredAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::ClientScreenEventRecord> Database::fetchClientScreenEvents(qint64 sessionId) const
{
    QList<ClientScreenEventRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT cse.event_type, cse.occurred_at
        FROM client_screen_events cse
        JOIN sessions s ON cse.install_id = s.install_id
        WHERE s.id = ?
          AND cse.occurred_at >= s.started_at
          AND (s.ended_at IS NULL OR cse.occurred_at <= s.ended_at)
        ORDER BY cse.occurred_at DESC
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, sessionId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ClientScreenEventRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.eventType  = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.occurredAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::AfkRecord> Database::fetchAfkRecords(int limit) const
{
    QList<AfkRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT afk_on_at, afk_off_at,
               CAST((strftime('%s', afk_off_at) - strftime('%s', afk_on_at)) AS INTEGER)
        FROM session_afk
        WHERE session_id = (
            SELECT id FROM sessions WHERE ended_at IS NULL ORDER BY started_at DESC LIMIT 1
        )
        ORDER BY afk_on_at DESC
        LIMIT ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, limit > 0 ? limit : -1);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AfkRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.afkOnAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.afkOffAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 2) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

QList<Database::AfkRecord> Database::fetchAfkRecords(int limit, qint64 sessionId) const
{
    QList<AfkRecord> result;
    if (!m_db) return result;
    armQueryBudget();

    static const char *sql = R"(
        SELECT afk_on_at, afk_off_at,
               CAST((strftime('%s', afk_off_at) - strftime('%s', afk_on_at)) AS INTEGER)
        FROM session_afk
        WHERE session_id = ?
        ORDER BY afk_on_at DESC
        LIMIT ?
    )";

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int64(stmt, 1, sessionId);
    sqlite3_bind_int(stmt, 2, limit > 0 ? limit : -1);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        AfkRecord r;
        if (auto *p = sqlite3_column_text(stmt, 0))
            r.afkOnAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        if (auto *p = sqlite3_column_text(stmt, 1))
            r.afkOffAt = QString::fromUtf8(reinterpret_cast<const char *>(p));
        r.durationSecs = sqlite3_column_type(stmt, 2) != SQLITE_NULL
                         ? sqlite3_column_int(stmt, 2) : -1;
        result.append(r);
    }
    sqlite3_finalize(stmt);
    return result;
}
