#pragma once

// Computes the "zone_limit"/"session_event_limit" params for a "log.session"
// request. Pulled out of SessionViewPage so it's testable without pulling in
// the rest of that class's Qt Widget dependencies.
struct SessionQueryLimits
{
    int zoneLimit;
    int sessionEventLimit;
};

// isLiveSession: true when targeting the currently-open session (session_id
// < 0 server-side resolves to "the most recent session with no ended_at").
// runningGameCount is how many OS processes WindowTracker currently detects
// as running — used only to size how many session-start/stop records to
// fetch (more instances running means more concurrent sessions worth
// showing), never to suppress detail data entirely. Whether a game is
// currently *detected* running is an independent signal from whether the DB
// has an open session; treating "0 detected" as "fetch nothing" caused a
// real bug where the live session's detail view (zones/client-screen/AFK/
// alt-tab — see query.FetchSessionPageData's `if zoneLimit > 0` gate) got
// permanently stuck empty if viewed before WindowTracker's first poll had
// ever reported in (runningGameCount defaults to 0 at startup).
inline SessionQueryLimits computeSessionQueryLimits(bool isLiveSession, int runningGameCount, int defaultZoneLimit)
{
    if (!isLiveSession)
        return {defaultZoneLimit, 0};
    return {defaultZoneLimit, runningGameCount > 1 ? 50 : 10};
}
