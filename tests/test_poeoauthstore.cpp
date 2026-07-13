#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "util/PoeOAuthStore.h"

// PoeOAuthStore's poe.oauth.* round trip, driven through a real
// PoeInfoClient connected to an in-process FakePoeInfoServer — mirrors
// test_steamaccountstore.cpp's shape, but also covers the "poeOAuthStatus"
// push-topic path (FakePoeInfoServer::publishEvent), since unlike
// SteamAccountStore, PoeOAuthStore's login outcome only ever arrives that
// way, never as the poe.oauth.login response itself.
class TestPoeOAuthStore : public QObject
{
    Q_OBJECT
private slots:
    void checkStatus_reflectsAuthorizedPayload()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.status"), QJsonObject{
            {"authorized", true},
            {"inProgress", false},
            {"username", "SomeAccount"},
            {"scope", "account:leagues"},
            {"accessExpiration", 1700003600.0},
        });

        PoeOAuthStore store(&client);
        QSignalSpy statusSpy(&store, &PoeOAuthStore::statusChanged);
        store.checkStatus();

        QVERIFY(statusSpy.wait(2000));
        const QList<QVariant> args = statusSpy.constFirst();
        QCOMPARE(args.at(0).toBool(), true);        // authorized
        QCOMPARE(args.at(1).toBool(), false);        // inProgress
        QCOMPARE(args.at(2).toString(), QStringLiteral("SomeAccount"));
        QCOMPARE(args.at(3).toString(), QStringLiteral("account:leagues"));
        QCOMPARE(args.at(4).toLongLong(), 1700003600LL);
        QCOMPARE(server.requestCount(QStringLiteral("poe.oauth.status")), 1);
    }

    void login_sendsLoginRequest()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.login"), QJsonObject{{"started", true}});

        PoeOAuthStore store(&client);
        store.login();

        QTRY_COMPARE(server.requestCount(QStringLiteral("poe.oauth.login")), 1);
    }

    void logout_sendsLogoutRequest()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.logout"), QJsonObject{{"ok", true}});

        PoeOAuthStore store(&client);
        store.logout();

        QTRY_COMPARE(server.requestCount(QStringLiteral("poe.oauth.logout")), 1);
    }

    // Reproduces the actual production flow: login() only confirms the flow
    // started; the real outcome — the user finished in their browser and
    // poe-info-service obtained a token — arrives asynchronously on the
    // "poeOAuthStatus" push topic PoeOAuthStore subscribes to in its
    // constructor.
    void poeOAuthStatusPush_deliversLoginOutcome()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        PoeOAuthStore store(&client);
        QSignalSpy statusSpy(&store, &PoeOAuthStore::statusChanged);

        server.publishEvent(QStringLiteral("poeOAuthStatus"), QJsonObject{
            {"authorized", true},
            {"inProgress", false},
            {"username", "PushedAccount"},
            {"scope", "account:leagues account:stashes account:characters"},
            {"accessExpiration", 1700007200.0},
        });

        QVERIFY(statusSpy.wait(2000));
        const QList<QVariant> args = statusSpy.constFirst();
        QCOMPARE(args.at(0).toBool(), true);
        QCOMPARE(args.at(2).toString(), QStringLiteral("PushedAccount"));
    }

    void poeOAuthStatusPush_deliversErrorOnFailure()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        PoeOAuthStore store(&client);
        QSignalSpy statusSpy(&store, &PoeOAuthStore::statusChanged);

        server.publishEvent(QStringLiteral("poeOAuthStatus"), QJsonObject{
            {"authorized", false},
            {"inProgress", false},
            {"error", "authorization denied: access_denied"},
        });

        QVERIFY(statusSpy.wait(2000));
        const QList<QVariant> args = statusSpy.constFirst();
        QCOMPARE(args.at(0).toBool(), false); // authorized
        QCOMPARE(args.at(5).toString(), QStringLiteral("authorization denied: access_denied"));
    }
};

QTEST_MAIN(TestPoeOAuthStore)
#include "test_poeoauthstore.moc"
