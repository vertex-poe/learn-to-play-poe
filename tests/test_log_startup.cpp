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
    void staleInstallDirStartup();

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

    static QByteArray readFile(const QString &path)
    {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            return f.readAll();
        return QByteArray();
    }

    // dataDir is passed to poe-info-service via --service-data-dir; it
    // resolves the database as <dataDir>/poe-info-service.db itself.
    static void assertStartup(const QString &dataDir)
    {
        // Write timing markers to a file instead of stdout.
        // l2p-poe.exe is a GUI subsystem app (WIN32_EXECUTABLE) and has no
        // stdout handle when launched as a child process on Windows.
        QTemporaryDir logDir;
        QVERIFY(logDir.isValid());
        const QString logPath    = logDir.path() + "/timing.log";
        const QString svcLogPath = logDir.path() + "/service.log";
        qputenv("L2P_STARTUP_TIMING_LOG", logPath.toUtf8());
        qputenv("L2P_SERVICE_LOG",        svcLogPath.toUtf8());

        fprintf(stderr, "[test_log_startup] starting app: data-dir=%s\n",
                dataDir.toUtf8().constData());
        fflush(stderr);

        // Isolate AppConfig too (not just the DB): otherwise AppConfig::load()
        // falls back to the repo root's (gitignored, developer-owned)
        // l2p-poe.toml when run via `just test`, which may configure a real
        // install dir. That would make poe-info-service's tailer replay a
        // real, possibly huge Client.txt from offset 0 against this test's
        // fresh DB — since LogPage now waits for that backlog replay to
        // finish before querying sessions, this could stall the test
        // indefinitely instead of hitting the isolated, empty/tiny fixture.
        const QString configPath = dataDir + "/l2p-poe-test.toml";
        QProcess p;
        p.setProgram(QString::fromUtf8(L2P_EXE_PATH));
        p.setArguments({"--startup-timing", "--service-data-dir", dataDir,
                         "--config", configPath});
        p.setProcessChannelMode(QProcess::ForwardedChannels);
        p.start();
        QVERIFY2(p.waitForStarted(10'000), "App process failed to start");

        // The app self-terminates after LogPage emits the populated marker.
        const bool finished = p.waitForFinished(30'000);
        if (!finished) { p.kill(); p.waitForFinished(3'000); }

        const QByteArray appLog = readFile(logPath);
        const QByteArray svcLog = readFile(svcLogPath);

        auto diag = [&](const char *label) {
            return qPrintable(QString("%1\n--- timing log ---\n%2\n--- service log ---\n%3")
                .arg(label)
                .arg(QString::fromUtf8(appLog.left(2000)))
                .arg(QString::fromUtf8(svcLog.left(2000))));
        };

        QVERIFY2(finished, diag("Process timed out."));
        QVERIFY2(p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
                 diag(qPrintable(QString("Process exited abnormally (status %1, code %2).")
                     .arg(p.exitStatus()).arg(p.exitCode()))));
        QVERIFY2(appLog.contains("STARTUP_TIMING:started"),
                 diag("Missing 'started' marker."));
        QVERIFY2(appLog.contains("STARTUP_TIMING:populated"),
                 diag("Missing 'populated' marker."));

        fprintf(stderr, "[test_log_startup] passed (exit %d)\n", p.exitCode());
        fflush(stderr);
    }
};

void LogStartupTest::emptyDatabaseStartup()
{
    fprintf(stderr, "[test_log_startup] emptyDatabaseStartup\n"); fflush(stderr);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    // No db file at all yet: poe-info-service creates it from scratch (clean-install path).
    assertStartup(tmp.path());
}

void LogStartupTest::populatedDatabaseStartup()
{
    fprintf(stderr, "[test_log_startup] populatedDatabaseStartup\n"); fflush(stderr);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString dbPath = tmp.path() + "/poe-info-service.db";

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

    assertStartup(tmp.path());
}

// A stale install_dirs entry (e.g. a removed drive or moved install) must be
// skipped, not handed to poe-info-service — which has no way to notice the
// log path will never appear and just sits in "ingesting" forever (see
// MainWindow's install dir selection). Regression test for that hang.
void LogStartupTest::staleInstallDirStartup()
{
    fprintf(stderr, "[test_log_startup] staleInstallDirStartup\n"); fflush(stderr);
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());

    const QString configPath = tmp.path() + "/l2p-poe-test.toml";
    QFile cfgFile(configPath);
    QVERIFY(cfgFile.open(QIODevice::WriteOnly));
    cfgFile.write(QByteArrayLiteral(
        "auto_detect_install_dir = false\n"
        "install_dirs = [ 'Z:/does/not/exist/Path of Exile' ]\n"));
    cfgFile.close();

    assertStartup(tmp.path());
}

QTEST_GUILESS_MAIN(LogStartupTest)
#include "test_log_startup.moc"
