#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QtTest>
#include <sqlite3.h>

#ifndef L2P_EXE_PATH
#error "L2P_EXE_PATH not defined by CMake"
#endif
#ifndef L2P_SCHEMA_SQL_PATH
#error "L2P_SCHEMA_SQL_PATH not defined by CMake"
#endif

// Verifies the startup path (DB open → sessions query → LogPage population → marker)
// is correct under two conditions: brand-new empty DB and pre-populated DB.
// The perf test measures the same path but does not run on every `just test`.
class LogStartupTest : public QObject
{
    Q_OBJECT
private slots:
    void emptyDatabaseStartup();
    void populatedDatabaseStartup();

private:
    // Initialise the DB schema (runs schema.sql via sqlite3).
    // Returns the open handle; caller must sqlite3_close it.
    static sqlite3 *createSchema(const QString &dbPath)
    {
        sqlite3 *db = nullptr;
        if (sqlite3_open(dbPath.toUtf8().constData(), &db) != SQLITE_OK)
            return nullptr;
        QFile f(QString::fromUtf8(L2P_SCHEMA_SQL_PATH));
        if (!f.open(QIODevice::ReadOnly)) { sqlite3_close(db); return nullptr; }
        const QByteArray sql = f.readAll();
        sqlite3_exec(db, sql.constData(), nullptr, nullptr, nullptr);
        return db;
    }

    static void assertStartup(const QString &dbPath)
    {
        QProcess p;
        p.setProgram(QString::fromUtf8(L2P_EXE_PATH));
        p.setArguments({"--startup-timing"});
        QStringList env = QProcess::systemEnvironment();
        env << "L2P_STARTUP_TIMING_DB=" + dbPath;
        p.setEnvironment(env);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start();
        QVERIFY2(p.waitForStarted(10'000), "App process failed to start");

        QByteArray output;
        bool startedMarkerSeen = false;
        QElapsedTimer timer; timer.start();
        while (timer.elapsed() < 30'000) {
            output += p.readAll();
            if (output.contains("STARTUP_TIMING:started")) {
                startedMarkerSeen = true;
                break;
            }
            if (p.state() == QProcess::NotRunning) {
                output += p.readAll();
                if (output.contains("STARTUP_TIMING:started"))
                    startedMarkerSeen = true;
                break;
            }
            p.waitForReadyRead(100);
        }
        QVERIFY2(startedMarkerSeen, 
                 qPrintable(QString("Did not see 'started' marker. Output: %1").arg(output.left(500))));

        const bool finished = p.waitForFinished(10'000);
        if (!finished) { p.kill(); p.waitForFinished(3'000); }
        output += p.readAll();
        
        QVERIFY2(finished,
                 qPrintable(QString("Process timed out (output: %1)").arg(output.left(500))));
        QVERIFY2(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
                 qPrintable(QString("Process exited abnormally (status %1, code %2, output: %3)")
                     .arg(p.exitStatus()).arg(p.exitCode()).arg(output.left(500))));
        QVERIFY2(output.contains("STARTUP_TIMING:populated"),
                 qPrintable(QString("Marker not found in output: %1").arg(output.left(500))));
    }
};

void LogStartupTest::emptyDatabaseStartup()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // Pass a path to a nonexistent file: app creates the DB from scratch (clean-install path).
    assertStartup(tmp.path() + "/timing.db");
}

void LogStartupTest::populatedDatabaseStartup()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/timing.db";

    // Initialise schema then insert two closed sessions so LogPage renders session cards.
    sqlite3 *db = createSchema(dbPath);
    QVERIFY2(db, "Failed to initialise test database");

    sqlite3_exec(db, "INSERT INTO installs(path) VALUES('/game/Client.txt');",
                 nullptr, nullptr, nullptr);
    const qint64 iid = sqlite3_last_insert_rowid(db);

    char s1[256], s2[256];
    std::snprintf(s1, sizeof(s1),
        "INSERT INTO sessions(install_id, started_at, ended_at, total_secs, active_secs) "
        "VALUES(%lld, '2024-01-15 10:00:00', '2024-01-15 11:00:00', 3600, 3200);",
        static_cast<long long>(iid));
    std::snprintf(s2, sizeof(s2),
        "INSERT INTO sessions(install_id, started_at, ended_at, total_secs, active_secs) "
        "VALUES(%lld, '2024-01-15 14:00:00', '2024-01-15 16:00:00', 7200, 6800);",
        static_cast<long long>(iid));
    sqlite3_exec(db, s1, nullptr, nullptr, nullptr);
    sqlite3_exec(db, s2, nullptr, nullptr, nullptr);
    sqlite3_close(db);

    assertStartup(dbPath);
}

QTEST_GUILESS_MAIN(LogStartupTest)
#include "test_log_startup.moc"
