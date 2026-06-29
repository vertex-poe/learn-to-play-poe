// Fine-grained startup performance test.
//
// Runs every default-tab config (dt=0..6) under two scenarios each (baseline
// and swap_early), collects 3 runs per scenario, takes the median of each
// milestone, asserts against perf_targets.h thresholds, and writes the full
// results to perf_results.json for perf_compare.py.
//
// Labeled "perf" — skipped by `just test`, run by `just test-perf`.
//
// Click simulation is Windows-only (PostMessage WM_LBUTTONDOWN/UP).
// On non-Windows this test is skipped.

#include <QDateTime>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <sqlite3.h>
#include <algorithm>
#include <cstdio>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#ifndef L2P_EXE_PATH
#error "L2P_EXE_PATH not defined by CMake"
#endif
#ifndef L2P_PERF_RESULTS_PATH
#error "L2P_PERF_RESULTS_PATH not defined by CMake"
#endif
#ifndef L2P_SCHEMA_SQL_PATH
#error "L2P_SCHEMA_SQL_PATH not defined by CMake"
#endif

#include "perf_targets.h"

static const int kRuns     = 3;
static const int kTimeout  = 40'000; // ms per run
static const int kClickMs  = 100;    // interval between repeated clicks

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

struct HitboxInfo { int x{0}; int y{0}; quintptr hwnd{0}; };

struct RunResult {
    QMap<QString, qint64> milestones; // marker name → abs_ms
    bool timedOut{false};
};

static QString tabName(int dt)
{
    static const char *names[] = { "guide","chats","dms","stash","profile","current","log" };
    return (dt >= 0 && dt <= 6) ? QLatin1String(names[dt]) : QLatin1String("unknown");
}

static bool isPlaceholder(int dt)
{
    return dt == 0 || dt == 3 || dt == 4; // Guide, Stash, Profile
}

#ifdef Q_OS_WIN
static void sendClick(HWND hwnd, int screenX, int screenY)
{
    POINT pt = { screenX, screenY };
    ScreenToClient(hwnd, &pt);
    PostMessage(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    PostMessage(hwnd, WM_LBUTTONUP,   0,           MAKELPARAM(pt.x, pt.y));
}
#endif

// ---------------------------------------------------------------------------
// Test class
// ---------------------------------------------------------------------------

class PerfMetricsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void perfSuite();

private:
    RunResult runOnce(int dt, bool baseline, const QString &dbPath);

    QString m_dbPath;
};

// ---------------------------------------------------------------------------
// initTestCase: create a temp DB with sessions, chats, and whispers
// ---------------------------------------------------------------------------

void PerfMetricsTest::initTestCase()
{
#ifndef Q_OS_WIN
    QSKIP("Perf metrics test requires Windows (PostMessage click simulation)");
#endif

    QTemporaryDir tmp;
    QVERIFY2(tmp.isValid(), "Failed to create temp dir");
    tmp.setAutoRemove(false); // keep for the lifetime of the test object
    m_dbPath = tmp.path() + "/perf_test.db";

    sqlite3 *db = nullptr;
    QVERIFY2(sqlite3_open(m_dbPath.toUtf8().constData(), &db) == SQLITE_OK,
             "Failed to open test DB");

    QFile schemaFile(QString::fromUtf8(L2P_SCHEMA_SQL_PATH));
    QVERIFY2(schemaFile.open(QIODevice::ReadOnly), "Failed to open schema.sql");
    const QByteArray schema = schemaFile.readAll();
    QVERIFY2(sqlite3_exec(db, schema.constData(), nullptr, nullptr, nullptr) == SQLITE_OK,
             "Failed to execute schema.sql");

    sqlite3_exec(db, "PRAGMA user_version = 99;", nullptr, nullptr, nullptr);

    sqlite3_exec(db,
        "INSERT INTO installs(path) VALUES('/perf/Client.txt');",
        nullptr, nullptr, nullptr);
    const qint64 installId = sqlite3_last_insert_rowid(db);

    // Insert 120 closed sessions spanning the last 60 days.
    for (int i = 0; i < 120; ++i) {
        // Start: subtract i*12 hours from a fixed point
        const qint64 startSecs = 1'750'000'000LL - i * 43200LL; // 12h apart
        const qint64 endSecs   = startSecs + 7200LL;             // 2h duration
        char sql[512];
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO sessions(install_id,started_at,ended_at,total_secs,active_secs)"
            " VALUES(%lld,"
            " datetime(%lld,'unixepoch'),"
            " datetime(%lld,'unixepoch'),"
            " 7200, 6500);",
            static_cast<long long>(installId),
            static_cast<long long>(startSecs),
            static_cast<long long>(endSecs));
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }

    // Seed a public_chars row so chats can reference it.
    sqlite3_exec(db,
        "INSERT INTO public_chars(name) VALUES('TradeBot');",
        nullptr, nullptr, nullptr);
    const qint64 pubCharId = sqlite3_last_insert_rowid(db);

    // Insert 80 chat messages spread across the first session.
    // occurred_at descending so newest appear first in the query result.
    for (int i = 0; i < 80; ++i) {
        const qint64 ts = 1'750'000'000LL - i * 60LL; // 1 min apart
        char sql[512];
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO chats(session_id, public_char_id, channel, message, occurred_at)"
            " VALUES(1, %lld, '$', 'WTS item %d 1c', datetime(%lld,'unixepoch'));",
            static_cast<long long>(pubCharId), i,
            static_cast<long long>(ts));
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }

    // Insert 60 whisper messages (mix of from/to) spread across the first session.
    for (int i = 0; i < 60; ++i) {
        const qint64 ts = 1'750'000'000LL - i * 90LL; // 90s apart
        const char *dir = (i % 2 == 0) ? "from" : "to";
        char sql[512];
        std::snprintf(sql, sizeof(sql),
            "INSERT INTO whispers(session_id, direction, player_name, message, occurred_at)"
            " VALUES(1, '%s', 'Trader%d', 'Hi %d', datetime(%lld,'unixepoch'));",
            dir, i % 10, i,
            static_cast<long long>(ts));
        sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
    }

    sqlite3_close(db);
}

// ---------------------------------------------------------------------------
// runOnce: launch the app for one scenario run and collect milestones
// ---------------------------------------------------------------------------

RunResult PerfMetricsTest::runOnce(int dt, bool baseline, const QString &dbPath)
{
    RunResult result;

#ifndef Q_OS_WIN
    result.timedOut = true;
    return result;
#else
    const QString scenario = baseline ? QLatin1String("baseline")
                                      : QLatin1String("swap_early");

    // The default swap nav: navIdx 0 (Guide) unless we're starting on Guide,
    // then navIdx 4 (Log). Matches the logic in main.cpp.
    static const int kNavIdx[] = { 0, 1, 1, 2, 3, 4, 4 };
    const int defaultNavIdx    = kNavIdx[dt];
    const int swapNavIdx       = (defaultNavIdx == 0) ? 4 : 0;

    QProcess p;
    p.setProgram(QString::fromUtf8(L2P_EXE_PATH));
    p.setArguments({
        QStringLiteral("--perf-scenario=") + scenario,
        QStringLiteral("--default-tab=%1").arg(dt),
        QStringLiteral("--perf-swap-nav=%1").arg(swapNavIdx),
    });
    QStringList env = QProcess::systemEnvironment();
    env << QLatin1String("L2P_STARTUP_TIMING_DB=") + dbPath;
    p.setEnvironment(env);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start();

    if (!p.waitForStarted(10'000)) {
        result.timedOut = true;
        return result;
    }

    // Phase enum drives click sending.
    enum Phase {
        WaitConfig,       // collect hitbox/config markers
        WaitFirstPaint,
        Clicking,         // send repeated clicks on default nav tab
        WaitFinalPaint,   // baseline only: wait after first_interaction
        SendSwapClick,    // send one click on swap nav tab
        WaitDone,
        Done
    };

    Phase phase = WaitConfig;
    QMap<int, HitboxInfo> hitboxes;
    HWND mainHwnd  = nullptr;
    int  defNavIdx = defaultNavIdx;
    int  swpNavIdx = swapNavIdx;
    bool hasDefNav = false, hasSwpNav = false;

    QElapsedTimer timeout;
    timeout.start();
    QElapsedTimer lastClick;
    lastClick.start();

    while (phase != Done) {
        if (timeout.elapsed() > kTimeout) {
            result.timedOut = true;
            p.kill();
            p.waitForFinished(3'000);
            return result;
        }

        p.waitForReadyRead(5);

        while (p.canReadLine()) {
            const QByteArray raw  = p.readLine().trimmed();
            const QLatin1String s(raw.constData(), raw.size());

            if (s.startsWith(QLatin1String("PERF:hitbox:"))) {
                // PERF:hitbox:<navIdx>:<screenX>:<screenY>:<hwnd>
                const QList<QByteArray> parts = raw.split(':');
                if (parts.size() >= 6) {
                    HitboxInfo h;
                    const int idx = parts[2].toInt();
                    h.x    = parts[3].toInt();
                    h.y    = parts[4].toInt();
                    h.hwnd = (quintptr)parts[5].toULongLong();
                    hitboxes[idx] = h;
                    // All hitboxes share the same top-level HWND.
                    if (!mainHwnd)
                        mainHwnd = reinterpret_cast<HWND>(h.hwnd);
                }
            } else if (s.startsWith(QLatin1String("PERF:config:default_nav_idx:"))) {
                defNavIdx = raw.mid(28).toInt();
                hasDefNav = true;
            } else if (s.startsWith(QLatin1String("PERF:config:swap_nav_idx:"))) {
                swpNavIdx = raw.mid(25).toInt();
                hasSwpNav = true;
            } else if (s.startsWith(QLatin1String("PERF:first_paint:"))) {
                QList<QByteArray> p = raw.mid(17).split(':');
                result.milestones["first_paint"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["first_paint_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["first_paint_from_paint"] = p[2].toLongLong();
                if (phase == WaitFirstPaint || phase == WaitConfig)
                    phase = Clicking;
            } else if (s.startsWith(QLatin1String("PERF:first_interaction:"))) {
                QList<QByteArray> p = raw.mid(23).split(':');
                result.milestones["first_interaction"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["first_interaction_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["first_interaction_from_paint"] = p[2].toLongLong();
                if (phase == Clicking)
                    phase = baseline ? WaitFinalPaint : SendSwapClick;
            } else if (s.startsWith(QLatin1String("PERF:first_load:"))) {
                QList<QByteArray> p = raw.mid(16).split(':');
                result.milestones["first_load"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["first_load_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["first_load_from_paint"] = p[2].toLongLong();
            } else if (s.startsWith(QLatin1String("PERF:final_paint:"))) {
                QList<QByteArray> p = raw.mid(17).split(':');
                result.milestones["final_paint"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["final_paint_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["final_paint_from_paint"] = p[2].toLongLong();
                if (phase == WaitFinalPaint)
                    phase = SendSwapClick;
            } else if (s.startsWith(QLatin1String("PERF:final_interaction:"))) {
                QList<QByteArray> p = raw.mid(23).split(':');
                result.milestones["final_interaction"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["final_interaction_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["final_interaction_from_paint"] = p[2].toLongLong();
            } else if (s.startsWith(QLatin1String("PERF:menu_swap_final:"))) {
                QList<QByteArray> p = raw.mid(21).split(':');
                result.milestones["menu_swap_final"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["menu_swap_final_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["menu_swap_final_from_paint"] = p[2].toLongLong();
            } else if (s.startsWith(QLatin1String("PERF:menu_swap_early:"))) {
                QList<QByteArray> p = raw.mid(21).split(':');
                result.milestones["menu_swap_early"] = p[0].toLongLong();
                if (p.size() > 1) result.milestones["menu_swap_early_delta"] = p[1].toLongLong();
                if (p.size() > 2) result.milestones["menu_swap_early_from_paint"] = p[2].toLongLong();
            } else if (s == QLatin1String("PERF:done") ||
                       s.startsWith(QLatin1String("PERF:done:"))) {
                phase = Done;
                break;
            }

            // Advance past WaitConfig as soon as we have hitboxes + config.
            if (phase == WaitConfig && mainHwnd && hasDefNav && hasSwpNav)
                phase = WaitFirstPaint;
        }

        // Send repeated clicks while waiting for first_interaction.
        if (phase == Clicking && lastClick.elapsed() >= kClickMs) {
            if (hitboxes.contains(defNavIdx) && mainHwnd) {
                sendClick(mainHwnd,
                          hitboxes[defNavIdx].x,
                          hitboxes[defNavIdx].y);
            }
            lastClick.restart();
        }

        // Send a single swap click, then wait for done.
        if (phase == SendSwapClick) {
            if (hitboxes.contains(swpNavIdx) && mainHwnd) {
                sendClick(mainHwnd,
                          hitboxes[swpNavIdx].x,
                          hitboxes[swpNavIdx].y);
            }
            phase = WaitDone;
        }
    }

    if (!p.waitForFinished(5'000)) {
        p.kill();
        p.waitForFinished(2'000);
    }

    return result;
#endif
}

// ---------------------------------------------------------------------------
// perfSuite: the main test — loops all configs × scenarios × runs
// ---------------------------------------------------------------------------

void PerfMetricsTest::perfSuite()
{
#ifndef Q_OS_WIN
    QSKIP("Perf metrics test requires Windows");
#endif

    // key → vector of 3 abs_ms values (one per run)
    QMap<QString, QVector<qint64>> collected;

    static const bool kScenarios[]   = { true, false }; // baseline, swap_early
    static const char *kScenNames[]  = { "baseline", "swap_early" };

    for (int dt = 0; dt <= 6; ++dt) {
        const QString tab = tabName(dt);
        for (int si = 0; si < 2; ++si) {
            const bool baseline = kScenarios[si];
            const QString scen  = QLatin1String(kScenNames[si]);

            for (int run = 0; run < kRuns; ++run) {
                qDebug("perf: dt=%d (%s) scenario=%s run=%d",
                       dt, qPrintable(tab), qPrintable(scen), run + 1);

                const RunResult r = runOnce(dt, baseline, m_dbPath);

                QVERIFY2(!r.timedOut,
                         qPrintable(QString("Run timed out: dt=%1 scenario=%2 run=%3")
                             .arg(dt).arg(scen).arg(run + 1)));

                // Collect each milestone received.
                for (auto it = r.milestones.begin(); it != r.milestones.end(); ++it) {
                    const QString key = tab + "_" + scen + "_" + it.key() + "_ms";
                    collected[key].append(it.value());
                }
            }
        }
    }

    // Compute medians and assert thresholds.
    QJsonObject metrics;
    bool anyFail = false;
    QStringList failures;

    // Load targets from JSON
    QJsonObject targetsJson;
    {
        QFile f(QString::fromUtf8(L2P_PERF_TARGETS_PATH));
        if (f.open(QIODevice::ReadOnly)) {
            targetsJson = QJsonDocument::fromJson(f.readAll()).object();
        } else {
            QFAIL(qPrintable(QString("Failed to open perf targets: %1").arg(QString::fromUtf8(L2P_PERF_TARGETS_PATH))));
        }
    }

    for (auto it = collected.begin(); it != collected.end(); ++it) {
        QVector<qint64> vals = it.value();
        std::sort(vals.begin(), vals.end());
        const qint64 median = vals[vals.size() / 2];

        QJsonObject entry;
        QJsonArray  runs;
        for (qint64 v : vals) runs.append(v);
        entry["runs"]   = runs;
        entry["median"] = median;
        metrics[it.key()] = entry;

        const QString &key = it.key();
        QJsonObject targetObj = targetsJson[key].toObject();
        if (targetObj.isEmpty()) {
            // Unconfigured milestone target; could warn, but skipping allows dynamic tracking of info-only metrics
            continue;
        }

        int threshold = targetObj["threshold_ms"].toInt(0);
        if (threshold > 0 && median > threshold) {
            anyFail = true;
            failures.append(
                QStringLiteral("  %1: median %2 ms > threshold %3 ms")
                    .arg(key).arg(median).arg(threshold));
        }
    }

    // Merge with any existing perf_results.json written by other perf tests.
    const QString resultsPath = QString::fromUtf8(L2P_PERF_RESULTS_PATH);
    QJsonObject existingMetrics;
    {
        QFile f(resultsPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonDocument d = QJsonDocument::fromJson(f.readAll());
            existingMetrics = d.object()["metrics"].toObject();
        }
    }
    for (auto it = metrics.begin(); it != metrics.end(); ++it)
        existingMetrics[it.key()] = it.value();

    QJsonObject root;
    root["metrics"]   = existingMetrics;
    root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);

    QFile out(resultsPath);
    if (out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        out.write(QJsonDocument(root).toJson());

    if (anyFail) {
        QFAIL(qPrintable(
            QLatin1String("Perf thresholds exceeded:\n") + failures.join('\n')));
    }
}

QTEST_GUILESS_MAIN(PerfMetricsTest)
#include "test_perf_metrics.moc"
