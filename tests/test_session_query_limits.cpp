#include <QtTest/QtTest>

#include "ui/log/SessionQueryLimits.h"

// Regression coverage for a real bug: computeSessionQueryLimits used to zero
// out zone_limit for the live session whenever runningGameCount was 0 (no OS
// process currently detected running). Since query.FetchSessionPageData
// gates its entire detail fetch (zones/client-screen/AFK/alt-tab) on
// `zoneLimit > 0`, that meant the *current* session's detail view could get
// permanently stuck empty — e.g. viewed before WindowTracker's first poll
// ever reports in, at app startup, when runningGameCount is still 0 by
// default. zone_limit must never depend on runningGameCount.
class TestSessionQueryLimits : public QObject
{
    Q_OBJECT
private slots:
    void historicalSession_ignoresRunningGameCount()
    {
        for (int count : {0, 1, 2}) {
            const auto limits = computeSessionQueryLimits(/*isLiveSession=*/false, count, /*defaultZoneLimit=*/50);
            QCOMPARE(limits.zoneLimit, 50);
            QCOMPARE(limits.sessionEventLimit, 0);
        }
    }

    void liveSession_zeroRunningGames_stillFetchesZones()
    {
        const auto limits = computeSessionQueryLimits(/*isLiveSession=*/true, /*runningGameCount=*/0, /*defaultZoneLimit=*/50);
        QCOMPARE(limits.zoneLimit, 50);
        QVERIFY(limits.sessionEventLimit > 0);
    }

    void liveSession_oneRunningGame()
    {
        const auto limits = computeSessionQueryLimits(true, 1, 50);
        QCOMPARE(limits.zoneLimit, 50);
        QCOMPARE(limits.sessionEventLimit, 10);
    }

    void liveSession_multipleRunningGames_scalesUp()
    {
        const auto limits = computeSessionQueryLimits(true, 2, 50);
        QCOMPARE(limits.zoneLimit, 50);
        QCOMPARE(limits.sessionEventLimit, 50);
    }
};

QTEST_MAIN(TestSessionQueryLimits)
#include "test_session_query_limits.moc"
