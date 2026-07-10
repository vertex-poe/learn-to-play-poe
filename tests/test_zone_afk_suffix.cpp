#include <QtTest/QtTest>

#include "ui/log/ZoneAfkSuffix.h"

// Regression coverage for the zone-card AFK suffix folded into the Log
// screen's timeline — replaces what used to be separate "AFK" notification
// cards. See SessionViewPage::makeZoneCard/updateLiveAfkSuffix.
class TestZoneAfkSuffix : public QObject
{
    Q_OBJECT
private slots:
    void noAfk_areaType_stillOpen()
    {
        QCOMPARE(buildZoneSuffix(/*hasAreaType=*/true, /*durationSecs=*/-1, /*afkSecs=*/0, /*afkOngoing=*/false),
                  QStringLiteral("entered."));
    }

    void noAfk_areaType_closed()
    {
        QCOMPARE(buildZoneSuffix(true, 90, 0, false), QStringLiteral("entered \xc2\xb7 1m30s"));
    }

    void noAfk_plainZone_closed()
    {
        QCOMPARE(buildZoneSuffix(/*hasAreaType=*/false, 90, 0, false), QStringLiteral("\xc2\xb7 1m30s"));
    }

    void noAfk_plainZone_stillOpen_isEmpty()
    {
        // The plain (no areaType) card variant never showed an "entered."
        // fallback — preserve that when there's no AFK either.
        QCOMPARE(buildZoneSuffix(false, -1, 0, false), QString());
    }

    void closedAfk_leadingNumberIsActiveTimeNotTotal()
    {
        // durationSecs=5400 (1h30m total) minus afkSecs=1800 (30m) is 1h
        // active — the leading number excludes AFK, not the raw wall-clock
        // zone duration.
        QCOMPARE(buildZoneSuffix(true, 5400, 1800, false),
                  QStringLiteral("entered \xc2\xb7 1h \xc2\xb7 30m afk"));
    }

    void closedAfk_entireZoneWasAfk_showsEnteredDotNotZero()
    {
        // durationSecs == afkSecs: active time is 0, not negative or blank —
        // falls back to the same "entered." used when duration is unknown.
        QCOMPARE(buildZoneSuffix(true, 120, 120, false),
                  QStringLiteral("entered. \xc2\xb7 2m afk"));
    }

    void closedAfk_withNoZoneDuration_stillAppendsToEnteredDot()
    {
        // Still-open zone (durationSecs=-1) with only closed AFK time in it.
        QCOMPARE(buildZoneSuffix(true, -1, 120, false),
                  QStringLiteral("entered. \xc2\xb7 2m afk"));
    }

    void closedAfk_plainZone_noDuration_isJustTheAfkPart()
    {
        // The plain (no areaType, no known duration) variant has an empty
        // base — the AFK part should stand alone with no leading separator.
        QCOMPARE(buildZoneSuffix(false, -1, 120, false), QStringLiteral("2m afk"));
    }

    void ongoingAfk_isHighlighted()
    {
        const QString suffix = buildZoneSuffix(true, -1, 45, /*afkOngoing=*/true);
        QVERIFY2(suffix.contains(QStringLiteral("<span")), "ongoing AFK should be wrapped for highlighting");
        QVERIFY2(suffix.contains(QStringLiteral("45s afk")), "ongoing AFK text should still show the running tally");
    }

    void zeroAfkSecs_neverShowsAfkPart_evenIfFlaggedOngoing()
    {
        // Guards the very first instant of a fresh AFK (elapsed still 0) —
        // callers are expected to withhold the "ongoing" flag until at
        // least 1s has elapsed, but the helper itself should never emit a
        // bare "afk" label with no number.
        QCOMPARE(buildZoneSuffix(true, 100, 0, true), QStringLiteral("entered \xc2\xb7 1m40s"));
    }
};

QTEST_MAIN(TestZoneAfkSuffix)
#include "test_zone_afk_suffix.moc"
