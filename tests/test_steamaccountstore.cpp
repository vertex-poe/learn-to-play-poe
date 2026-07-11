#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "util/SteamAccountStore.h"

// Regression coverage for SteamAccountStore's credentials.* round trip —
// mirrors PoeAccountStore's shape (checkSession/storeSession/deleteSession)
// but under the "steamApiKey" credential key. Driven through a real
// PoeInfoClient connected to an in-process FakePoeInfoServer since
// PoeInfoClient itself isn't mockable (see test_logpage_retry.cpp).
class TestSteamAccountStore : public QObject
{
    Q_OBJECT
private slots:
    void checkKey_reflectsPresence()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("credentials.has"),
                              QJsonObject{{"present", true}});

        SteamAccountStore store(&client);
        QSignalSpy checkedSpy(&store, &SteamAccountStore::keyChecked);
        store.checkKey();

        QVERIFY(checkedSpy.wait(2000));
        QCOMPARE(checkedSpy.constFirst().at(0).toBool(), true);
        QCOMPARE(server.requestCount(QStringLiteral("credentials.has")), 1);
        // Must match poe-info-service's steamAPIKeyCredKey exactly (see
        // poe-info-service/internal/server/steam.go) — a mismatch here would
        // silently store/read the wrong credential slot.
        QCOMPARE(server.lastParams(QStringLiteral("credentials.has"))[QStringLiteral("key")].toString(),
                  QStringLiteral("steamApiKey"));
    }

    void storeKey_usesSteamApiKeyCredentialKey()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("credentials.store"), QJsonObject{{"ok", true}});

        SteamAccountStore store(&client);
        QSignalSpy storedSpy(&store, &SteamAccountStore::keyStored);
        store.storeKey(QStringLiteral("1A2B3C4D5E6F7890ABCDEF1234567890"));

        QVERIFY(storedSpy.wait(2000));
        QCOMPARE(storedSpy.constFirst().at(0).toBool(), true);
        QCOMPARE(server.requestCount(QStringLiteral("credentials.store")), 1);
    }

    void deleteKey_onError_reportsFailure()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("credentials.delete"), {}, QStringLiteral("boom"));

        SteamAccountStore store(&client);
        QSignalSpy deletedSpy(&store, &SteamAccountStore::keyDeleted);
        store.deleteKey();

        QVERIFY(deletedSpy.wait(2000));
        QCOMPARE(deletedSpy.constFirst().at(0).toBool(), false);
    }
};

QTEST_MAIN(TestSteamAccountStore)
#include "test_steamaccountstore.moc"
