#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "ui/stash/StashPage.h"

#include <QComboBox>

// Coverage for StashPage's league dropdown: it must populate from
// poe.leagues.list and default-select whichever league poe.league (the
// player's current, Steam-rich-presence-derived league) reports — falling
// back sanely when that can't be determined. Driven through a real
// PoeInfoClient connected to an in-process FakePoeInfoServer, same approach
// as test_logpage_retry.cpp (PoeInfoClient isn't mockable).
class TestStashPage : public QObject
{
    Q_OBJECT
private slots:
    void defaultsToCurrentLeagueFromPoeLeague()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

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
};

QTEST_MAIN(TestStashPage)
#include "test_stashpage.moc"
