#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "ui/stash/StashPage.h"

#include <QComboBox>
#include <QFrame>
#include <QPushButton>

// Coverage for StashPage's league dropdown: it must populate from
// poe.leagues.list (the account-scoped list, which requires being signed in
// to PoE OAuth) and default-select whichever league poe.league (the
// player's current, Steam-rich-presence-derived league) reports — falling
// back sanely when that can't be determined — plus the auth banner shown
// when the user isn't authenticated. Driven through a real PoeInfoClient
// connected to an in-process FakePoeInfoServer, same approach as
// test_logpage_retry.cpp (PoeInfoClient isn't mockable).
class TestStashPage : public QObject
{
    Q_OBJECT
private slots:
    void defaultsToCurrentLeagueFromPoeLeague()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.status"), QJsonObject{{"authorized", true}});
        server.queueResponse(QStringLiteral("poe.leagues.list"), QJsonObject{
            {"leagues", QJsonArray{
                QJsonObject{{"name", "Standard"}, {"realm", "pc"}, {"event", false}},
                QJsonObject{{"name", "Hardcore"}, {"realm", "pc"}, {"event", false}},
                QJsonObject{{"name", "SSF Ancestors"}, {"realm", "pc"}, {"event", false}},
            }}});
        server.queueResponse(QStringLiteral("poe.league"), QJsonObject{
            {"league", "SSF Ancestors"}, {"source", "steamRichPresence"}});

        StashPage page;
        page.setPoeInfoClient(&client);
        QSignalSpy leagueChangedSpy(&page, &StashPage::leagueChanged);

        page.show();

        QTRY_COMPARE(page.currentLeague(), QStringLiteral("SSF Ancestors"));
        QVERIFY(!leagueChangedSpy.isEmpty());
        QCOMPARE(leagueChangedSpy.last().at(0).toString(), QStringLiteral("SSF Ancestors"));
    }

    void fallsBackToFirstLeagueWhenCurrentUnknown()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.status"), QJsonObject{{"authorized", true}});
        server.queueResponse(QStringLiteral("poe.leagues.list"), QJsonObject{
            {"leagues", QJsonArray{
                QJsonObject{{"name", "Standard"}, {"realm", "pc"}, {"event", false}},
                QJsonObject{{"name", "Hardcore"}, {"realm", "pc"}, {"event", false}},
            }}});
        // No PoE session detected — poe.league reports an empty league.
        server.queueResponse(QStringLiteral("poe.league"), QJsonObject{{"league", ""}});

        StashPage page;
        page.setPoeInfoClient(&client);
        page.show();

        QTRY_COMPARE(page.currentLeague(), QStringLiteral("Standard"));
    }

    void emptyLeagueListLeavesComboDisabled()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.oauth.status"), QJsonObject{{"authorized", true}});
        server.queueResponse(QStringLiteral("poe.leagues.list"), QJsonObject{{"leagues", QJsonArray{}}});
        server.queueResponse(QStringLiteral("poe.league"), QJsonObject{{"league", ""}});

        StashPage page;
        page.setPoeInfoClient(&client);
        QSignalSpy leagueChangedSpy(&page, &StashPage::leagueChanged);
        page.show();

        QTRY_COMPARE(server.requestCount(QStringLiteral("poe.league")), 1);
        QTest::qWait(50); // let the poe.league response finish applyLeagues()
        QVERIFY(page.currentLeague().isEmpty());
        QVERIFY(leagueChangedSpy.isEmpty());
    }

    // Not authenticated: no "poe.oauth.status" response is queued, so
    // FakePoeInfoServer's default (empty-payload) response applies —
    // PoeOAuthStore decodes that as authorized=false, same as a real
    // unauthenticated poe-info-service. StashPage must show the auth banner
    // and never attempt the leagues fetch at all.
    void notAuthenticated_showsBannerAndSkipsFetch()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        StashPage page;
        page.setPoeInfoClient(&client);
        page.show();

        auto *notice = page.findChild<QFrame *>(QStringLiteral("stashAuthNotice"));
        QVERIFY(notice);
        QTRY_VERIFY(notice->isVisible());
        QVERIFY(page.currentLeague().isEmpty());
        QCOMPARE(server.requestCount(QStringLiteral("poe.leagues.list")), 0);
    }

    // The auth banner's button must emit loginRequested() so MainWindow can
    // navigate to Settings > Accounts.
    void authNoticeButtonClick_emitsLoginRequested()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        StashPage page;
        page.setPoeInfoClient(&client);
        page.show();

        auto *notice = page.findChild<QFrame *>(QStringLiteral("stashAuthNotice"));
        QVERIFY(notice);
        QTRY_VERIFY(notice->isVisible());

        auto *loginBtn = page.findChild<QPushButton *>(QStringLiteral("stashAuthNoticeLoginBtn"));
        QVERIFY(loginBtn);

        QSignalSpy loginSpy(&page, &StashPage::loginRequested);
        loginBtn->click();
        QCOMPARE(loginSpy.count(), 1);
    }

    // Becoming authenticated after the page is already shown (a
    // "poeOAuthStatus" push, mirroring a real interactive login completing)
    // must trigger the deferred leagues fetch and hide the banner.
    void becomesAuthenticatedLater_thenFetchesLeagues()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("poe.leagues.list"), QJsonObject{
            {"leagues", QJsonArray{
                QJsonObject{{"name", "Standard"}, {"realm", "pc"}, {"event", false}},
            }}});
        server.queueResponse(QStringLiteral("poe.league"), QJsonObject{{"league", ""}});

        StashPage page;
        page.setPoeInfoClient(&client);
        page.show();

        auto *notice = page.findChild<QFrame *>(QStringLiteral("stashAuthNotice"));
        QVERIFY(notice);
        QTRY_VERIFY(notice->isVisible());
        QCOMPARE(server.requestCount(QStringLiteral("poe.leagues.list")), 0);

        server.publishEvent(QStringLiteral("poeOAuthStatus"), QJsonObject{{"authorized", true}});

        QTRY_COMPARE(page.currentLeague(), QStringLiteral("Standard"));
        QVERIFY(!notice->isVisible());
    }
};

QTEST_MAIN(TestStashPage)
#include "test_stashpage.moc"
