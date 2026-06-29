#include <QTest>
#include <QProcess>
#include <QTemporaryFile>
#include <QByteArray>
#include <QDebug>
#include <QElapsedTimer>
#include <QString>
#include <sqlite3.h>

class RefDataTest : public QObject {
    Q_OBJECT

private slots:
    void testRefData() {
        QTemporaryFile dbFile;
        QVERIFY(dbFile.open());
        QString dbPath = dbFile.fileName();
        dbFile.close();

        sqlite3 *db;
        int rc = sqlite3_open(dbPath.toUtf8().constData(), &db);
        QVERIFY2(rc == SQLITE_OK, "Failed to create sqlite database");

        char *errMsg = nullptr;
        rc = sqlite3_exec(db, "CREATE TABLE data (id INTEGER PRIMARY KEY, value TEXT)", nullptr, nullptr, &errMsg);
        QVERIFY2(rc == SQLITE_OK, errMsg);

        sqlite3_exec(db, "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
        for (int i = 0; i < 100; ++i) {
            QString query = QString("INSERT INTO data (value) VALUES ('dummy_data_%1')").arg(i);
            sqlite3_exec(db, query.toUtf8().constData(), nullptr, nullptr, nullptr);
        }
        sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
        sqlite3_close(db);

        QProcess p;
        p.setProcessChannelMode(QProcess::ForwardedChannels); // Output directly to terminal

#ifndef L2P_REF_DATA_EXE_PATH
#error "L2P_REF_DATA_EXE_PATH not defined"
#endif

        p.start(QString::fromUtf8(L2P_REF_DATA_EXE_PATH), { dbPath });
        QVERIFY(p.waitForStarted());

        bool finished = p.waitForFinished(10000);
        QVERIFY2(finished, "App timed out");
        QVERIFY(p.exitStatus() == QProcess::NormalExit);
        QVERIFY(p.exitCode() == 0);
    }
};

QTEST_GUILESS_MAIN(RefDataTest)
#include "test_ref_data.moc"
