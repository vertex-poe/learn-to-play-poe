#pragma once

#include <QString>

// Formats a duration in the compact "1h30m"-style shorthand used throughout
// the Log screen (zone cards, AFK/alt-tab suffixes, session summaries).
inline QString formatDuration(int secs)
{
    if (secs <= 0) return {};
    constexpr int kYear  = 365 * 86400;
    constexpr int kMonth = 30  * 86400;
    constexpr int kWeek  = 7   * 86400;
    const int Y = secs / kYear;
    const int M = (secs % kYear)  / kMonth;
    const int W = (secs % kMonth) / kWeek;
    const int D = (secs % kWeek)  / 86400;
    const int h = (secs % 86400)  / 3600;
    const int m = (secs % 3600)   / 60;
    const int s = secs % 60;
    if (Y > 0)
        return (Y > 5 || M == 0) ? QStringLiteral("%1Y").arg(Y)
                                  : QStringLiteral("%1Y%2M").arg(Y).arg(M);
    if (M > 0)
        return (M > 5 || W == 0) ? QStringLiteral("%1M").arg(M)
                                  : QStringLiteral("%1M%2W").arg(M).arg(W);
    if (W > 0)
        return (W > 5 || D == 0) ? QStringLiteral("%1W").arg(W)
                                  : QStringLiteral("%1W%2D").arg(W).arg(D);
    if (D > 0)
        return (D > 5 || h == 0) ? QStringLiteral("%1D").arg(D)
                                  : QStringLiteral("%1D%2h").arg(D).arg(h);
    if (h > 0)
        return (h > 5 || m == 0) ? QStringLiteral("%1h").arg(h)
                                  : QStringLiteral("%1h%2m").arg(h).arg(m);
    if (m > 0)
        return (m > 5 || s == 0) ? QStringLiteral("%1m").arg(m)
                                  : QStringLiteral("%1m%2s").arg(m).arg(s);
    return QStringLiteral("%1s").arg(s);
}

// Builds the "entered · 1h · 30m afk" suffix folded into a zone card's
// header — replaces what used to be a separate "AFK" notification card per
// interval (see SessionViewPage::makeZoneCard/updateLiveAfkSuffix).
// durationSecs is the zone's total wall-clock time (-1 if still open); the
// leading number displayed is *active* time (durationSecs minus afkSecs),
// not the raw total — AFK time is broken out separately rather than double
// counted. afkSecs is the AFK time already accumulated in this zone (closed
// intervals only); when afkOngoing is true it is still counting and
// rendered highlighted — the caller is expected to have already added the
// live-elapsed time into afkSecs before calling this. Pulled out of
// SessionViewPage so it's testable without pulling in the rest of that
// class's Qt Widget dependencies (mirrors SessionQueryLimits.h).
inline QString buildZoneSuffix(bool hasAreaType, int durationSecs, int afkSecs, bool afkOngoing)
{
    // A zone spent entirely AFK still reads as "entered." rather than "0s"
    // active — durationSecs stays -1 (unknown/open) untouched either way.
    const int activeSecs = durationSecs > 0 ? qMax(0, durationSecs - afkSecs) : durationSecs;

    QString base;
    if (hasAreaType)
        base = activeSecs > 0 ? "entered \xc2\xb7 " + formatDuration(activeSecs) : QStringLiteral("entered.");
    else if (activeSecs > 0)
        base = "\xc2\xb7 " + formatDuration(activeSecs);

    if (afkSecs <= 0)
        return base;

    QString afkPart = formatDuration(afkSecs) + " afk";
    if (afkOngoing)
        afkPart = QStringLiteral("<span style=\"color:#b79bea;\">\xE2\x8F\xB1 %1</span>").arg(afkPart);

    return base.isEmpty() ? afkPart : base + " \xc2\xb7 " + afkPart;
}
