#include "perf_targets.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <sqlite3.h>

#ifndef L2P_EXE_PATH
#error "L2P_EXE_PATH not defined by CMake"
#endif
#ifndef L2P_PERF_RESULTS_PATH
#define L2P_PERF_RESULTS_PATH ""
#endif

class StartupTimingTest : public QObject
{
    Q_OBJECT
private slots:
    void sessionListVisible();
};

void StartupTimingTest::sessionListVisible()
{
    const QByteArray exeOverride = qgetenv("L2P_STARTUP_TIMING_EXE");
    const QString exePath = exeOverride.isEmpty()
        ? QString::fromUtf8(L2P_EXE_PATH)
        : QString::fromUtf8(exeOverride);
    QTemporaryDir tmpDir;
    QVERIFY(tmpDir.isValid());
    // poe-info-service resolves its database as <data-dir>/poe-info-service.db;
    // pre-seeding it here and passing tmpDir via --service-data-dir points the
    // whole l2p-poe -> poe-info-service chain at this fixture in one step.
    const QString dbPath = tmpDir.path() + "/poe-info-service.db";

    {
        sqlite3 *db = nullptr;
        QVERIFY(sqlite3_open(dbPath.toUtf8().constData(), &db) == SQLITE_OK);
        QFile schemaFile(QString::fromUtf8(L2P_SCHEMA_SQL_PATH));
        QVERIFY(schemaFile.open(QIODevice::ReadOnly));
        sqlite3_exec(db, schemaFile.readAll().constData(), nullptr, nullptr, nullptr);
        sqlite3_exec(db, "PRAGMA user_version = 99;", nullptr, nullptr, nullptr);
        sqlite3_close(db);
    }

    QVector<qint64> times;
    constexpr int runs = 10;

    for (int i = 0; i < runs; ++i) {
        // File-based IPC: l2p-poe.exe is a GUI subsystem app with no stdout
        // handle when launched as a child process on Windows.
        const QString logPath = tmpDir.path() + QString("/timing_%1.log").arg(i);
        qputenv("L2P_STARTUP_TIMING_LOG", logPath.toUtf8());

        QProcess p;
        p.setProgram(exePath);
        p.setArguments({"--startup-timing", "--service-data-dir", tmpDir.path()});
        p.setProcessChannelMode(QProcess::ForwardedChannels);

        QElapsedTimer t;
        t.start();
        p.start();
        QVERIFY2(p.waitForStarted(10'000), "App process failed to start");

        // Self-terminates after emitting the marker; allow 15 s for cold start.
        const bool finished = p.waitForFinished(15'000);
        const qint64 ms = t.elapsed();

        if (!finished) {
            p.kill();
            p.waitForFinished(3'000);
        }

        QFile logFile(logPath);
        QByteArray output;
        if (logFile.open(QIODevice::ReadOnly))
            output = logFile.readAll();

        QVERIFY2(finished,
                 qPrintable(QString("Run %1: process timed out (log: %2)")
                     .arg(i + 1).arg(QString::fromUtf8(output).left(500))));
        QVERIFY2(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
                 qPrintable(QString("Run %1: process exited abnormally (status %2, code %3, log: %4)")
                     .arg(i + 1).arg(p.exitStatus()).arg(p.exitCode())
                     .arg(QString::fromUtf8(output).left(500))));
        QVERIFY2(output.contains("STARTUP_TIMING:populated"),
                 qPrintable(QString("Run %1: marker not found in log: %2")
                     .arg(i + 1).arg(QString::fromUtf8(output).left(500))));

        times.append(ms);
    }

    std::sort(times.begin(), times.end());
    const qint64 median = times[runs / 2];
    qInfo() << "Startup times (ms):" << times << "| median:" << median << "ms";

    // Write results for delta comparison (before asserting so data is always saved).
    const QString resultsPath = QString::fromUtf8(L2P_PERF_RESULTS_PATH);
    if (!resultsPath.isEmpty()) {
        QString commit = "unknown";
        QProcess git;
        git.start("git", {"rev-parse", "--short", "HEAD"});
        if (git.waitForFinished(3'000))
            commit = QString::fromUtf8(git.readAllStandardOutput()).trimmed();

        QJsonArray runsArr;
        for (qint64 t : times) runsArr.append(t);

        QJsonObject metricObj;
        metricObj["runs"]   = runsArr;
        metricObj["median"] = median;

        QJsonObject metrics;
        metrics["startup_to_session_list_ms"] = metricObj;

        QJsonObject root;
        root["commit"]    = commit;
        root["timestamp"] = QDateTime::currentDateTime().toString(Qt::ISODate);
        root["metrics"]   = metrics;

        QFile f(resultsPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(QJsonDocument(root).toJson());
    }

    QVERIFY2(median < PerfTargets::kStartupToSessionListMs,
             qPrintable(QString("Median startup %1 ms exceeds %2 ms threshold")
                 .arg(median).arg(PerfTargets::kStartupToSessionListMs)));
}

QTEST_MAIN(StartupTimingTest)
#include "test_startup_timing.moc"
