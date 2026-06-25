#include "db/DatabaseHealth.h"

#include <QDateTime>
#include <cstdio>
#include <sqlite3.h>

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

// ── construction ─────────────────────────────────────────────────────────────

DatabaseHealth::DatabaseHealth(const QString &dbPath)
{
    const QByteArray utf8 = dbPath.toUtf8();
    if (sqlite3_open(utf8.constData(), &m_db) != SQLITE_OK) {
        sqlite3_close(m_db);
        m_db = nullptr;
        return;
    }
    execSql(m_db, "PRAGMA journal_mode=WAL;");
    execSql(m_db, "PRAGMA synchronous=NORMAL;");
}

DatabaseHealth::~DatabaseHealth()
{
    if (m_db)
        sqlite3_close(m_db);
}

// ── app_state helpers ─────────────────────────────────────────────────────────

QDateTime DatabaseHealth::readStateTs(const char *key) const
{
    if (!m_db) return {};
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT value FROM app_state WHERE key=?;", -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
    QDateTime dt;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        dt = QDateTime::fromString(
            QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0))),
            Qt::ISODate);
    sqlite3_finalize(stmt);
    return dt;
}

void DatabaseHealth::writeStateNow(const char *key)
{
    if (!m_db) return;
    const QByteArray keyBytes = QByteArray(key);
    const QByteArray val = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8();
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db,
        "INSERT OR REPLACE INTO app_state(key, value) VALUES(?,?);",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, keyBytes.constData(), keyBytes.size(), SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, val.constData(),      val.size(),      SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// ── public run methods ────────────────────────────────────────────────────────

QVector<DatabaseHealth::CheckResult> DatabaseHealth::runRoutine()
{
    QVector<CheckResult> results;
    if (!m_db) return results;
    results.append(repairEventIndex());
    results.append(optimizeStats());
    results.append(checkpointWal());
    writeStateNow("last_routine_check");
    return results;
}

QVector<DatabaseHealth::CheckResult> DatabaseHealth::runFull(int minIntervalDays)
{
    QVector<CheckResult> results;
    if (!m_db) return results;

    const QDateTime last = lastFullRun();
    if (last.isValid() && last.daysTo(QDateTime::currentDateTimeUtc()) < minIntervalDays)
        return results;

    results.append(repairEventIndex());
    results.append(optimizeStats());
    results.append(checkpointWal());
    results.append(quickIntegrityCheck());
    results.append(checkForeignKeys());
    writeStateNow("last_full_check");
    return results;
}

QVector<DatabaseHealth::CheckResult> DatabaseHealth::runRepair()
{
    QVector<CheckResult> results;
    if (!m_db) return results;
    results.append(fullIntegrityCheck());
    results.append(reindexAll());
    results.append(rebuildEventSpine());
    results.append(checkForeignKeys());
    results.append(vacuum());
    writeStateNow("last_repair");
    return results;
}

// ── individual checks ─────────────────────────────────────────────────────────

DatabaseHealth::CheckResult DatabaseHealth::repairEventIndex()
{
    CheckResult r;
    r.name = QStringLiteral("Event index consistency");

    struct Source {
        const char *eventType;
        const char *table;
        const char *tsCol;
    };
    static constexpr Source kSources[] = {
        { "whisper",        "whispers",                  "occurred_at"  },
        { "death",          "character_deaths",           "occurred_at"  },
        { "level_up",       "character_level_events",     "occurred_at"  },
        { "achievement",    "achievement_events",          "occurred_at"  },
        { "hideout",        "hideout_discovered_events",  "occurred_at"  },
        { "quest",          "quest_events",               "occurred_at"  },
        { "pvp_queue",      "pvp_queue_events",           "occurred_at"  },
        { "ruleset_failed", "zone_ruleset_failed_events", "occurred_at"  },
        { "general",        "general_events",             "occurred_at"  },
        { "passive_alloc",  "passive_skill_allocations",  "allocated_at" },
    };

    char sql[512];
    execSql(m_db, "BEGIN;");

    for (const auto &s : kSources) {
        std::snprintf(sql, sizeof(sql),
            "INSERT OR IGNORE INTO events(occurred_at, event_type, source_id) "
            "SELECT s.%s, '%s', s.id FROM %s s "
            "LEFT JOIN events e ON e.event_type='%s' AND e.source_id=s.id "
            "WHERE e.id IS NULL;",
            s.tsCol, s.eventType, s.table, s.eventType);
        execSql(m_db, sql);
        const int inserted = sqlite3_changes(m_db);
        r.found += inserted;
        r.fixed += inserted;

        std::snprintf(sql, sizeof(sql),
            "DELETE FROM events WHERE event_type='%s' "
            "AND source_id NOT IN (SELECT id FROM %s);",
            s.eventType, s.table);
        execSql(m_db, sql);
        const int deleted = sqlite3_changes(m_db);
        r.found += deleted;
        r.fixed += deleted;
    }

    execSql(m_db, "COMMIT;");
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::optimizeStats()
{
    CheckResult r;
    r.name = QStringLiteral("Query planner statistics (PRAGMA optimize)");
    execSql(m_db, "PRAGMA optimize;");
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::checkpointWal()
{
    CheckResult r;
    r.name = QStringLiteral("WAL checkpoint");
    // Returns (busy, checkpointed, wal_pages). busy > 0 means another writer held the lock.
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "PRAGMA wal_checkpoint(TRUNCATE);", -1, &stmt, nullptr);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const int busy = sqlite3_column_int(stmt, 0);
        if (busy > 0) {
            r.passed = false;
            r.issues.append(QStringLiteral("WAL checkpoint blocked by active writer"));
        }
    }
    sqlite3_finalize(stmt);
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::quickIntegrityCheck()
{
    CheckResult r;
    r.name = QStringLiteral("Quick integrity check");
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "PRAGMA quick_check;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString line =
            QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        if (line != QLatin1String("ok")) {
            r.passed = false;
            r.issues.append(line);
            ++r.found;
        }
    }
    sqlite3_finalize(stmt);
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::checkForeignKeys()
{
    CheckResult r;
    r.name = QStringLiteral("Foreign key consistency");
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "PRAGMA foreign_key_check;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString table  = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        const qint64  rowid  = sqlite3_column_int64(stmt, 1);
        const QString parent = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2)));
        r.passed = false;
        r.issues.append(QStringLiteral("%1 rowid %2 → missing row in %3").arg(table).arg(rowid).arg(parent));
        ++r.found;
    }
    sqlite3_finalize(stmt);
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::fullIntegrityCheck()
{
    CheckResult r;
    r.name = QStringLiteral("Full integrity check");
    sqlite3_stmt *stmt = nullptr;
    sqlite3_prepare_v2(m_db, "PRAGMA integrity_check;", -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const QString line =
            QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
        if (line != QLatin1String("ok")) {
            r.passed = false;
            r.issues.append(line);
            ++r.found;
        }
    }
    sqlite3_finalize(stmt);
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::reindexAll()
{
    CheckResult r;
    r.name = QStringLiteral("Rebuild all indexes (REINDEX)");
    execSql(m_db, "REINDEX;");
    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::rebuildEventSpine()
{
    CheckResult r;
    r.name = QStringLiteral("Rebuild event spine from source tables");

    // Count existing rows so we can report how many were replaced.
    sqlite3_stmt *countStmt = nullptr;
    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM events;", -1, &countStmt, nullptr);
    if (sqlite3_step(countStmt) == SQLITE_ROW)
        r.found = sqlite3_column_int(countStmt, 0);
    sqlite3_finalize(countStmt);

    execSql(m_db, "BEGIN;");
    execSql(m_db, "DELETE FROM events;");
    execSql(m_db, R"(
        INSERT OR IGNORE INTO events(occurred_at, event_type, source_id)
        SELECT occurred_at, 'whisper',        id FROM whispers
        UNION ALL
        SELECT occurred_at, 'death',          id FROM character_deaths
        UNION ALL
        SELECT occurred_at, 'level_up',       id FROM character_level_events
        UNION ALL
        SELECT occurred_at, 'achievement',    id FROM achievement_events
        UNION ALL
        SELECT occurred_at, 'hideout',        id FROM hideout_discovered_events
        UNION ALL
        SELECT occurred_at, 'quest',          id FROM quest_events
        UNION ALL
        SELECT occurred_at, 'pvp_queue',      id FROM pvp_queue_events
        UNION ALL
        SELECT occurred_at, 'ruleset_failed', id FROM zone_ruleset_failed_events
        UNION ALL
        SELECT occurred_at, 'general',        id FROM general_events
        UNION ALL
        SELECT allocated_at, 'passive_alloc', id FROM passive_skill_allocations;
    )");
    execSql(m_db, "COMMIT;");

    sqlite3_prepare_v2(m_db, "SELECT COUNT(*) FROM events;", -1, &countStmt, nullptr);
    if (sqlite3_step(countStmt) == SQLITE_ROW)
        r.fixed = sqlite3_column_int(countStmt, 0);
    sqlite3_finalize(countStmt);

    return r;
}

DatabaseHealth::CheckResult DatabaseHealth::vacuum()
{
    CheckResult r;
    r.name = QStringLiteral("Vacuum (compact and defragment)");
    // VACUUM cannot run inside a transaction and temporarily resets WAL mode.
    execSql(m_db, "VACUUM;");
    // Re-apply WAL mode since VACUUM resets it to DELETE journal.
    execSql(m_db, "PRAGMA journal_mode=WAL;");
    return r;
}
