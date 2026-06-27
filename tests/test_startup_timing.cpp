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
    const QString dbPath = tmpDir.path() + "/timing.db";

    QVector<qint64> times;
    constexpr int runs = 3;

    for (int i = 0; i < runs; ++i) {
        QProcess p;
        p.setProgram(exePath);
        p.setArguments({"--startup-timing"});
        QStringList env = QProcess::systemEnvironment();
        env << "L2P_STARTUP_TIMING_DB=" + dbPath;
        p.setEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);

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

        const QByteArray output = p.readAll();
        QVERIFY2(output.contains("STARTUP_TIMING:populated"),
                 qPrintable(QString("Run %1: marker not found in output: %2")
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
