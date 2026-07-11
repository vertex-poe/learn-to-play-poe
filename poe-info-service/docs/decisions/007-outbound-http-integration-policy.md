# ADR-007: Outbound HTTP Integration Policy

**Status**: Decided
**Date**: 2026-07-11
**Deciders**: MovingCairn

---

## Context

Steam presence (`internal/steam`, `internal/server/steam.go`) is poe-info-service's first outbound HTTP integration — every prior responsibility is inbound WebSocket handling, SQLite reads/writes, or local Client.txt tailing. It combines two sources: the official Steam Web API (`ISteamUser/GetPlayerSummaries`, needs a Web API key) and an unofficial scrape of `steamcommunity.com/miniprofile/<id3>` (no credential, but an undocumented HTML page with no stability contract from Valve).

[ADR-001](001-single-shared-instance-lifecycle.md) already names "duplicate-rate-limit-consumption" as a motivating reason for the single-shared-instance model, but only asserts the problem — it never spells out how a shared instance actually honors an external source's rate limits once it starts making outbound calls at all. Root [ROADMAP_DETAILS.md](../../ROADMAP_DETAILS.md) already anticipates more of these (the PoE official API), so this is written as general policy for *any* outbound integration, with Steam as the first concrete example throughout, rather than a Steam-specific decision.

## Decision

- **Throttle every outbound call**, shared across all endpoints of a given external source (`internal/steam`'s `minIntervalLimiter`), not just per-request politeness — the throttle is one shared instance per source, so a batched call and N follow-up calls in the same cycle all still respect one overall pace.
- **Graceful degradation, never a hard failure.** One data source's fetch failure or absence (missing credential, non-2xx, a scrape that comes back empty) degrades to an empty/error value on the affected field or entry only. It never blocks unrelated fields, unrelated tracked ids, or aborts a whole poll cycle. A missing optional credential (e.g. no `steamApiKey` stored) is an expected steady state, not logged as an error.
- **Poll only while it matters, by default.** For live-status-style data with no history value — Steam presence is the example — the default posture is subscriber-gated: the poller only contacts the external source while at least one client is subscribed to the relevant push topic (`Hub.HasSubscribers`). Polling a rate-limited, ToS-sensitive external source for nobody listening is pure waste and unnecessary risk.
- **Carve-out**: a future integration *may* poll without an active subscriber if the data represents coherent history that cannot be reconstructed after the fact — e.g. a session/event log where a missed interval is permanently lost, unlike a live "now" status which is cheap to re-fetch on demand. Even then, throttling and quota-respect are never optional, only the subscriber-gating is waived. Steam presence does not invoke this carve-out: rich presence and official-API status are both point-in-time live data, not history, so subscriber-gating applies to it without exception.
- **No stability guarantee on undocumented or scraped sources.** `steamcommunity.com/miniprofile` is the concrete example: no contract, may change or break silently, and the integration must degrade gracefully (per the point above) rather than crash or spam errors when it does.
- **Absence of an optional credential is not an error** — reuses the pattern already established by [ADR-004](004-credential-custody.md) for `POESESSID`: a client supplies a credential (e.g. `steamApiKey`) via the existing generic `credentials.store`, and this service never returns its value back, only whether one is present.

## Consequences

- **Positive**: a single, documented policy new outbound integrations (the anticipated PoE official API among them) can point back to, instead of re-deciding throttle/degradation/gating design from scratch each time.
- **Positive**: bounds the blast radius of Steam (or any future source) being slow, rate-limiting, or briefly down — it degrades one entry/field, never the service's core responsibilities (log ingest, the WebSocket API for everything else).
- **Negative**: subscriber-gating means a client that only ever calls a request method (e.g. `steam.presence`) without subscribing to its push topic will see stale or `"pending"` data indefinitely — this is intentional (see [CONTRIBUTING.md](../../CONTRIBUTING.md)'s "Steam presence" section for the exact contract), but is a real, deliberate trade-off against "just works" out of the box.
- **Open**: the carve-out for history-shaped data (poll without a subscriber) is not yet exercised by anything and has no concrete example implementation yet — its exact trigger conditions will be refined the first time a real integration needs it.
