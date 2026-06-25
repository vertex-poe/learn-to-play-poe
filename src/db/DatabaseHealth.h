#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>
#include <sqlite3.h>

// Three-tier database maintenance module.
//
// Routine  — fast, run after every ingest (seconds)
// Full     — thorough, run weekly when idle (seconds to minutes)
// Repair   — destructive/reconstructive, manual or auto-triggered on serious problems (minutes)
//
// Add new checks as private methods; call them from the appropriate run* method.
class DatabaseHealth
{
public:
    struct CheckResult {
        QString     name;
        int         found  = 0;     // rows missing, invalid, or flagged
        int         fixed  = 0;     // rows inserted, deleted, or rebuilt
        bool        passed = true;  // false when a check finds unrecoverable problems
        QStringList issues;         // human-readable detail lines when passed=false
    };

    explicit DatabaseHealth(const QString &dbPath);
    ~DatabaseHealth();

    bool isOpen() const { return m_db != nullptr; }

    // Timestamps of the last completed run of each tier (null if never run).
    QDateTime lastRoutineRun() const { return readStateTs("last_routine_check"); }
    QDateTime lastFullRun()    const { return readStateTs("last_full_check"); }
    QDateTime lastRepairRun()  const { return readStateTs("last_repair"); }

    // Repair index + optimize + WAL checkpoint.
    QVector<CheckResult> runRoutine();

    // Everything in routine + quick integrity check + FK check.
    // Skips silently and returns empty if called within the minimum interval.
    QVector<CheckResult> runFull(int minIntervalDays = 7);

    // Full integrity check + REINDEX + spine rebuild + VACUUM + orphan cleanup.
    // Always runs regardless of last-run time; intended for manual or auto-triggered use.
    QVector<CheckResult> runRepair();

private:
    // ── individual checks ────────────────────────────────────────────────────
    CheckResult repairEventIndex();
    CheckResult optimizeStats();
    CheckResult checkpointWal();
    CheckResult quickIntegrityCheck();
    CheckResult checkForeignKeys();
    CheckResult fullIntegrityCheck();
    CheckResult reindexAll();
    CheckResult rebuildEventSpine();
    CheckResult vacuum();

    // ── app_state helpers ────────────────────────────────────────────────────
    QDateTime readStateTs(const char *key) const;
    void      writeStateNow(const char *key);

    sqlite3 *m_db = nullptr;
};
