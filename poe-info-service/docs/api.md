# WebSocket API Reference

`poe-info-service` exposes one WebSocket endpoint, `ws://127.0.0.1:{port}/ws`
(port is configurable, default in `poe-info-service.toml`), plus a small HTTP
health check. Every WebSocket message is a single JSON object with this
envelope (`internal/proto.Message`):

```json
{
  "type":    "request",
  "id":      "client-chosen-string",
  "topic":   "poeLeagues",
  "method":  "poe.leagues.list",
  "error":   "human-readable message",
  "payload": { }
}
```

Only the fields relevant to a given `type` are set; the rest are omitted
(`omitempty`). `id` is echoed back on the matching `response` so a client can
correlate concurrent in-flight requests — request/response pairing is the
client's job, not a queue-order guarantee (see "Requests" below).

This document is a wire-level reference: method/topic names, request
parameters, response shapes. For *why* things behave the way they do —
caching rules, polling/rate-limit gating, credential handling — see
[`architecture.md`](architecture.md). For the database this service owns,
see [`schema.md`](schema.md). Per [ADR-003](decisions/003-client-api-versioning.md),
once a method/topic ships, its response shape only ever gains fields —
existing ones are never removed, renamed, or repurposed.

---

## Message types

| `type` | Direction | Meaning |
|---|---|---|
| `hello` | either | Version-negotiation handshake between two service instances (singleton election, [ADR-001](decisions/001-single-shared-instance-lifecycle.md)) — not sent by a normal addon client. |
| `step-down` | either | Sent by a newer instance's `hello` reply to tell an older incumbent to shut down. |
| `ping` / `pong` | client → server / server → client | Bare connectivity check. Does **not** count as activity for the idle-shutdown timer (see `keepalive` below). |
| `keepalive` | client → server, echoed back | Tells the service this client still needs it running; resets the idle-shutdown timer ([ADR-001](decisions/001-single-shared-instance-lifecycle.md)). The service shuts itself down after `--idle-timeout` (default 5 minutes) with no `keepalive`/request/subscription from any client and no Client.txt activity. |
| `subscribe` / `unsubscribe` | client → server | Adds/removes `c` from a topic's push list (see "Push topics"). `subscribe` replies with `{"subscribed": true}`; `unsubscribe` sends no reply. |
| `request` | client → server | Invokes `method` with `payload` as parameters; always gets exactly one `response` back. |
| `response` | server → client | Reply to a `request`, matched by `id`. Either `payload` (success) or `error` (failure) is set, never both. |
| `event` | server → client | An unsolicited push to every client currently subscribed to `topic`, carrying the same payload shape as the matching request's response unless noted otherwise below. |

Requests on one connection are processed **one at a time, in the order
received** — a slow request (e.g. a large `chat.messages` page) delays every
request queued behind it on that same connection. Open multiple connections
if you need concurrent in-flight requests. `poe.profile.*`/`poe.leagues.list`
are the exception in practice: their fetch itself runs asynchronously
through a background queue (see their sections below), so the read loop
isn't blocked waiting on an external HTTP call.

---

## Requests

### Status & health

#### `status`

No parameters. Returns the ingest pipeline's current state:

```json
{
  "version": "0.1.0",
  "startTime": 1700000000,
  "logPath": "C:\\...\\Client.txt;C:\\...\\Client.txt",
  "logOffset": 12345,
  "uptime": "1h2m3s",
  "phase": "tailing",
  "message": "waiting for game events",
  "percent": null
}
```

`phase` is `"waiting"` (no install configured yet), `"ingesting"` (replaying
a Client.txt backlog — `percent` is 0-100 while this is true), or
`"tailing"` (caught up, watching for new lines live). Also published on
`TopicStatus` whenever `phase` changes or `percent` crosses a new whole
percent, so a client only needs to poll `status` once for an initial
snapshot.

#### `ping`

No parameters. Replies `{"pong": "ok"}`. See "Message types" above for why
this differs from `keepalive`.

### Chat & DM history

#### `chat.messages`

```json
{"channels": ["#"], "include_dms": false, "limit": 100, "offset": 0, "from_date": "2024-01-01", "to_date": "2024-01-31"}
```

All fields optional. `channels` filters by the raw channel prefix character
(`#` global, `$` trade, `%` party, `&` guild); omit for all channels.
`from_date`/`to_date` are `YYYY-MM-DD`, inclusive. Returns
`{"records": [ChatRecord, ...]}`:

```json
{"source": "chat", "channel": "#", "player_name": "SomeAccount", "guild_tag": "TAG", "message": "hi", "occurred_at": "2024-01-15T10:00:00"}
```

`include_dms: true` merges whispers into the same result set (`source:
"whisper"`).

#### `dm.messages`

```json
{"player_filter": "SomeAccount", "limit": 100, "offset": 0}
```

`player_filter` optionally narrows to one partner (substring match). Returns
`{"records": [WhisperRecord, ...]}`:

```json
{"direction": "from", "player_name": "SomeAccount", "guild_tag": "TAG", "message": "hi", "occurred_at": "2024-01-15T10:00:00"}
```

`direction` is `"from"` (they whispered the player) or `"to"` (the player
whispered them).

#### `chat.dates`

```json
{"channels": ["#"], "include_dms": false}
```

Returns `{"dates": ["2024-01-15", ...]}` — every distinct `YYYY-MM-DD` with
at least one matching message, most-recent first. Used to populate a date
picker without paging through every message first.

#### `dm.partners`

No parameters. Returns `{"partners": [PartnerRecord, ...]}`:

```json
{"name": "SomeAccount", "dates": ["2024-01-15", "2024-01-10"]}
```

Every whisper partner ever seen, ordered by most-recent activity, each with
its own distinct active dates (most-recent first).

### Session & zone history

#### `log.sessions`

```json
{"limit": 50, "offset": 0}
```

Returns `{"records": [SessionRecord, ...]}`, chronological (oldest first):

```json
{"id": 1, "started_at": "...", "ended_at": "...", "total_secs": 3600, "active_secs": 3400, "afk_secs": 200, "account_name": "SomeAccount", "char_name": "MyWitch", "char_class": "Witch", "install_path": "C:\\..."}
```

An in-progress (not yet closed) session has `ended_at`/`total_secs`/
`active_secs` as `""`/`-1`/`-1`.

#### `log.session`

```json
{"session_id": 1, "zone_limit": 100, "session_event_limit": 200}
```

Returns one session's detail page, `SessionPageData`:

```json
{
  "zones": [ZoneTransitionRecord, ...],
  "session_events": [SessionEventRecord, ...],
  "client_screen_events": [ClientScreenEventRecord, ...]
}
```

`ZoneTransitionRecord`: `area_name`, `area_code`, `area_type`,
`area_subtype`, `area_level`, `entered_at`, `duration_secs`, `afk_secs`
(recomputed from `session_afk` on every fetch), `afk_open_since` (the
current still-open away interval's start, or `""`).

`SessionEventRecord`: `event_type`, `occurred_at`, `char_name`,
`char_class`, `install_path`, `active_secs`, `total_secs`.

`ClientScreenEventRecord`: `event_type` (`"login_screen"` or
`"char_select"`), `occurred_at`.

#### `log.zones`

```json
{"session_id": 1, "limit": 50, "offset": 0}
```

Returns `{"zones": [ZoneTransitionRecord, ...]}` — the same shape as
`log.session`'s `zones`, paginated independently for a session with more
zone transitions than fit in one `log.session` call.

#### `sessions.closeOrphans`

```json
{"running_install_paths": ["C:\\..."]}
```

Closes any session left open (no `ended_at`) whose install path is *not* in
`running_install_paths` — cleanup for sessions orphaned by an unclean
shutdown. Returns `{"closed": 2}` (count of sessions closed).

### Steam rich presence

See [`architecture.md`](architecture.md)'s "Steam presence" section for the
fetch/caching/polling rules these four share (all backed by the same
`richPresenceState`, gated by `richPresenceRequestTTL` and subscriber-gated
background polling). Each accepts no parameters and always returns
live-enough data on its own (fetching first if the cached copy is older than
25s) — no prior subscription is required, unlike the old list-based design.

#### `steam.presence`

Returns `RichPresencePayload` — the verbatim rich-presence text:

```json
{"richPresence": "SSF Ancestors: 92 Warden - The Sarn Encampment", "fetchedAt": 1700000000, "status": "ok", "error": ""}
```

`status` is `"pending"` (never fetched), `"ok"`, or `"error"` (`error` then
carries detail; the previous fields are left in place rather than blanked).

#### `character.level`

Returns `CharacterLevelPayload`, parsed from the same rich-presence text:

```json
{"level": 92, "source": "steamRichPresence", "fetchedAt": 1700000000}
```

`level` is `0` and `source` is `""` if not currently parseable (not in a PoE
session, or nothing fetched yet).

#### `character.class`

Returns `CharacterClassPayload`: `{"class": "Warden", "source":
"steamRichPresence", "fetchedAt": 1700000000}`.

#### `poe.league`

Returns `LeaguePayload` — the player's **current** league name, parsed from
rich presence, free of any PoE OAuth API cost:

```json
{"league": "SSF Ancestors", "source": "steamRichPresence", "fetchedAt": 1700000000, "detail": LeagueSummary}
```

`detail` is a zero-cost bonus: whenever the `leagues` table already has a
cached row for `league` (from some earlier `poe.leagues.list`/`.detail`
call), it's joined in for free — omitted if nothing is cached yet. This
never itself triggers a PoE OAuth API call; see "Fetch policy and cost
reporting" below for the tier this sits in relative to `poe.leagues.*`.

### PoE OAuth

See [`architecture.md`](architecture.md)'s "PoE OAuth" and "PoE Leagues"
sections for the full behavioral reference (token lifecycle, the
`reqqueue`-backed fetch/cache pattern, rate-limit handling). This section is
the wire shapes only.

#### Fetch policy and cost reporting

`poe.profile.locale`, `poe.profile.twitch`, `poe.leagues.list`, and
`poe.leagues.detail` all share this request/response vocabulary — this
subsection is the shared reference; each method's own section below only
calls out what's specific to it.

**Three tiers of "how much did this cost."** `poe.league` (above) is free —
piggybacked on Steam rich presence, never a PoE OAuth call. A cache hit on
any of the four methods here costs nothing either — it's the `leagues`
table or the profile cache answering from what's already stored. Only an
actual fetch (a cache miss or a forced refresh) draws down PoE's shared,
per-policy rate-limit budget (`_reference/poe-apis/poe-apis.md` §5). The
`fetch` request field controls which of these a given call is allowed to
do:

| `fetch` | Meaning |
|---|---|
| `"ifStale"` (default) | Fetch only if the cache is missing or older than `maxAgeSeconds`. |
| `"never"` | A read-only peek: return whatever's cached — fresh, stale, or nothing at all — and never spend a query, regardless of `maxAgeSeconds`. |
| `"always"` | Force a fresh fetch even over an already-fresh cache entry. |

**Response vocabulary.** Every response carries `status` (a single string,
kept for continuity with earlier versions of these methods) and the same
information split into two orthogonal fields for a caller that wants to
branch on them directly:

- `freshness`: `"fresh"` (cache hit within `maxAgeSeconds`), `"stale"` (cached but older than that, no fetch happened), or `"miss"` (nothing cached, no fetch happened).
- `fetching`: `true` while a fetch is enqueued or in flight for this request (`status` `"pending"`/`"ok"`), `false` otherwise.

`status` itself is one of `"fresh"`, `"stale"`, `"miss"`, `"pending"` (a
fetch was enqueued, `wait: false` — the real result arrives later on the
method's push topic), `"ok"` (a `wait: true` request's fetch completed in
time), or `"error"` (topic-only: a background fetch failed). The cached
value (however stale) is included on every response where `freshness` isn't
`"miss"` — including `"pending"`, so a caller has something to show
immediately rather than nothing until the fetch completes. The one case
that still surfaces as a top-level `error` instead of a `status` is nothing
cached and no way to ever fetch because of an auth requirement:
`poe.profile.*` always needs an authenticated account; `poe.leagues.detail`
only needs one if a fetch actually turns out to be necessary (`GET
/league/{name}` requires Bearer auth, unlike `poe.leagues.list`'s public
bulk `GET /leagues`). A peek (`fetch: "never"`) never hits this case either
way, since it never expects to be able to fetch anything.

**Cost reporting.** Add `"includeCost": true` to get a `cost` object on a
response that actually performed a fetch (never present on a cache hit or a
peek):

```json
{"cost": {"api": "poe-oauth", "policy": "profile-policy", "queries": 1, "rules": [RateLimitRule, ...]}}
```

`queries` is always `1` today (every fetch here is one HTTP round-trip).
Each `RateLimitRule`: `{"name": "R", "limit": 30, "remaining": 25,
"periodSeconds": 10, "resetsAt": 1700000010}` — `resetsAt` (unix seconds) is
only set while the rule looks saturated (`remaining == 0`), and is a
best-effort estimate (see `poe.ratelimit.status` below and
`internal/reqqueue`'s documented simplifications) rather than an exact
rolling-window computation. `cost` is opt-in on a direct request/response (not included by default) to
keep a normal response small and to avoid leaking rate-limit policy details
to a caller that doesn't care — but always present (no opt-in) on a
push-topic event whenever a fetch actually happened, since a topic push is
a broadcast to every subscriber rather than one caller's own preference;
see `TopicPoeProfile`/`TopicPoeLeagues` below.

#### `poe.oauth.login`

No parameters. Starts an interactive login (opens the system browser); a
login already in progress is left running rather than restarted. Returns
only whether the flow (re)started: `{"started": true}`. The actual outcome
arrives via `TopicPoeOAuthStatus`, or a follow-up `poe.oauth.status` call.

#### `poe.oauth.status`

No parameters. Returns `PoeOAuthStatusPayload`:

```json
{"authorized": true, "inProgress": false, "username": "SomeAccount", "scope": "account:leagues account:stashes account:characters", "accessExpiration": 1700003600, "error": ""}
```

`username`/`scope`/`accessExpiration` are only populated while `authorized`.
The access/refresh token itself is never returned, per
[ADR-004](decisions/004-credential-custody.md).

#### `poe.oauth.logout`

No parameters. Discards the stored token and cancels any scheduled refresh.
Returns `{"ok": true}`.

#### `poe.accounts.list`

No parameters. Returns `{"accounts": [PoeAccountSummary, ...]}`:

```json
{"name": "SomeAccount", "poeUuid": "uuid-1", "active": true}
```

Every account this service knows of — from `Client.txt` guild events and/or
a PoE OAuth login, merged onto one row by `name`. `poeUuid` is `""` for an
account never OAuth-authenticated locally. `active` is `true` for at most
one row: whichever account is currently signed in via OAuth
([ADR-005](decisions/005-credential-storage-mechanism.md)).

#### `poe.profile.locale` / `poe.profile.twitch`

```json
{"account": "", "maxAgeSeconds": 0, "priority": 0, "wait": false, "fetch": "ifStale", "includeCost": false}
```

All fields optional. `account` selects a `poe_uuid` or `accounts.name`,
defaulting to the currently OAuth-active account. `maxAgeSeconds` overrides
the field's default cache TTL (locale 30 days, Twitch 7 days), clamped to a
1-hour floor. `priority` overrides the default `reqqueue` priority (locale:
High=3, Twitch: Low=1 — see `internal/reqqueue`'s `Priority*` constants for
the full scale). `wait` chooses blocking (`true`) vs. non-blocking (`false`,
default) delivery. `fetch`/`includeCost` are "Fetch policy and cost
reporting" above. Returns `PoeProfileFieldPayload`:

```json
{"status": "fresh", "freshness": "fresh", "fetching": false, "value": "en_US", "fetchedAt": 1700000000, "error": "", "cost": null}
```

See "Fetch policy and cost reporting" above for `status`/`freshness`/
`fetching`/`cost`'s full vocabulary — the one case specific to this method:
a request naming a known-but-inactive account with nothing cached and no
way to ever fetch (not authenticated for that account) responds with a
top-level `error` instead of a `status`, unless `fetch: "never"` (a peek
never expects to fetch, so it never hits this case — it just reports
`"miss"`).

#### `poe.leagues.list`

```json
{"realm": "pc", "type": "main", "season": "", "maxAgeSeconds": 0, "priority": 0, "wait": false, "fetch": "ifStale", "includeCost": false}
```

All fields optional. `realm` (`pc`/`xbox`/`sony`/`poe2`, default `pc`),
`type` (`main`/`event`/`season`, default `main`), `season` (only meaningful
for `type: "season"`, PoE1 only) mirror `GET /leagues`'s own query
parameters. `maxAgeSeconds`/`priority`/`wait`/`fetch`/`includeCost` behave
like `poe.profile.*`'s fields of the same name (default cache TTL 6 hours,
clamped to a 5-minute floor; default priority Medium=2). Unlike
`poe.profile.*`, no `account` selector exists, and there's no "not
authenticated" error case — `GET /leagues` is public and account-independent,
so a fetch is always schedulable regardless of PoE OAuth sign-in state.
Returns `PoeLeaguesPayload`:

```json
{"status": "fresh", "freshness": "fresh", "fetching": false, "leagues": [LeagueSummary, ...], "fetchedAt": 1700000000, "error": "", "cost": null}
```

Each `LeagueSummary`:

```json
{"name": "SSF Ancestors", "realm": "pc", "url": "https://...", "startAt": "2024-01-01T00:00:00Z", "endAt": "", "description": "...", "rules": ["Hardcore", "NoParties"], "event": false, "delveEvent": false}
```

`endAt` is `""` for a permanent league. `rules` is the flattened list of
rule id strings — no other per-rule metadata exists today.

#### `poe.leagues.detail`

```json
{"name": "Hardcore", "realm": "pc", "account": "", "maxAgeSeconds": 0, "priority": 0, "wait": false, "fetch": "ifStale", "includeCost": false}
```

`name` is required — the exact league name (`LeagueSummary.name`), not a
display label. `realm`/`maxAgeSeconds`/`priority`/`wait`/`fetch`/
`includeCost` mirror `poe.leagues.list`'s fields of the same name (there's
no `type`/`season` here — `GET /league/{name}` looks a league up directly
by name, with no `type=main`/`event` bucket to choose between). `account`
is an optional selector (a `poe_uuid` or an `accounts.name`, exactly like
`poe.profile.*`'s field of the same name) — used only to obtain a Bearer
token if a fetch turns out to be needed: unlike `poe.leagues.list`'s bulk
`GET /leagues`, this single-league endpoint isn't public. An empty
`account` with nobody currently signed in is *not* itself an error — it
just means no fetch can happen, exactly like `poe.leagues.list`'s
account-independence, except here that only matters once a fetch is
actually needed (see below). Returns `PoeLeagueDetailPayload` — the same
vocabulary as `poe.leagues.list`, but for one league:

```json
{"status": "fresh", "freshness": "fresh", "fetching": false, "league": LeagueSummary, "fetchedAt": 1700000000, "error": "", "cost": null}
```

A response with nothing cached, a `fetch` policy that would need to fetch,
and no resolvable account errors out exactly like `poe.profile.*` would
("no cached league, and not authenticated") — the one case specific to this
method. A `fetch: "never"` peek never hits that case, since it never
expects to fetch in the first place; it just reports `status: "miss"`. If a
refresh completes and PoE reports no such league exists (`GET
/league/{name}` returns a `null` league — not an error), the response is
also a clean `status: "miss"`. A non-blocking (`wait: false`) caller learns
a refresh's outcome via `TopicPoeLeagueDetail`.

#### `poe.ratelimit.status`

No parameters. Returns `PoeRateLimitStatusPayload` — every PoE OAuth
rate-limit policy this service has learned about so far (from some prior
`poe.profile.*`/`poe.leagues.*` fetch's response headers); a policy never
dispatched under yet is simply absent, not zero-valued. Always free to call
— it never itself makes a PoE OAuth API call.

```json
{"policies": [{"policy": "profile-policy", "rules": [RateLimitRule, ...], "nextAllowedAt": 1700000010}]}
```

`nextAllowedAt` (unix seconds) is omitted when the policy is clear to
dispatch right now. See "Fetch policy and cost reporting" above for
`RateLimitRule`'s shape, and `internal/reqqueue`'s package doc for why this
is a best-effort estimate, not an exact rolling-window computation.

### Credentials

Per [ADR-004](decisions/004-credential-custody.md), a stored value's
contents are never logged and never sent back to any client — only whether
one exists.

#### `credentials.store`

```json
{"key": "poesessid", "value": "..."}
```

Stores `value` under `key` in the OS credential store. Returns `{"ok":
true}`.

#### `credentials.has`

```json
{"key": "poesessid"}
```

Returns `{"present": true}` — never the value itself.

#### `credentials.delete`

```json
{"key": "poesessid"}
```

Returns `{"ok": true}`.

### Config

See [ADR-006](decisions/006-user-config-storage.md): this is the sole
store for user-facing config — nothing here is written to the database.

#### `config.list`

No parameters. Returns `{"settings": {"<key>": configEntry, ...}}`, one
entry per known setting (mutable and read-only):

```json
{"value": true, "description": "Enable verbose debug logging.", "mutable": true}
```

Mutable keys today: `debug_logging`, `install_dirs`, `auto_detect_install_dir`,
`executable_names`, `steam_id`. Read-only: `bind`, `port` (changing either
requires editing `poe-info-service.toml` directly and restarting — a live
change wouldn't take effect without rebinding the listener). Also published
on `TopicConfig` whenever any setting changes, including ones the auto-detect
loop makes on its own.

#### `config.get`

```json
{"key": "steam_id"}
```

Returns one `configEntry` (same shape as one value in `config.list`'s
`settings` map). Unknown key is an `error`.

#### `config.set`

```json
{"key": "steam_id", "value": "76561198000000000"}
```

`value`'s shape depends on `key` (boolean, string, or array of strings — see
`config.list`'s descriptions). A read-only or unknown key is an `error`.
Applies immediately in memory, persists to `poe-info-service.toml`, and
publishes `TopicConfig`. Returns `{"ok": true}`.

### Chat channel labels

User-registered labels for a numbered chat channel (e.g. channel `820` ->
`"Trade"`), optionally scoped to a date range — see the `chat_channel_labels`
table in [`schema.md`](schema.md).

#### `channels.register`

```json
{"channel": 820, "label": "Trade", "valid_from": "", "valid_to": ""}
```

`valid_from`/`valid_to` are `YYYY-MM-DD`, `""` = unbounded. Returns `{"ok":
true}`.

#### `channels.rename`

```json
{"channel": 820, "valid_from": "", "valid_to": "", "old_label": "Trade", "new_label": "Trade Chat"}
```

Relabels an existing registration without touching its date range. Returns
`{"ok": true}`.

#### `channels.delete`

```json
{"channel": 820, "label": "Trade", "valid_from": "", "valid_to": ""}
```

Removes one label registration. Returns `{"ok": true}`.

---

## Push topics

Subscribe with `{"type": "subscribe", "topic": "<name>", "id": "..."}`. Each
topic's `event` payload matches the shape below unless noted.

| Topic | Payload | Published when |
|---|---|---|
| `status` | `StatusPayload` (same shape as the `status` request) | Ingest `phase` changes, or `percent` crosses a new whole percent during backlog replay. |
| `config` | `{"settings": {...}}` (same shape as `config.list`) | Any setting changes — a client's own `config.set`, another client's, or the auto-detect loop finding an install dir on its own. |
| `steamPresence` | `RichPresencePayload` | The raw rich-presence text, status, or error changes. Requires a subscription (to this or one of the three topics below) to activate background polling at all ([ADR-007](decisions/007-outbound-http-integration-policy.md)) — Steam is rate-limited/ToS-sensitive, so an idle service with nobody listening must not poll it. |
| `character.level` | `CharacterLevelPayload` | Parsed level changes. |
| `character.class` | `CharacterClassPayload` | Parsed class changes. |
| `poe.league` | `LeaguePayload` (including the zero-cost `detail` join, see above) | Parsed current-league name changes. |
| `poeOAuthStatus` | `PoeOAuthStatusPayload` (same shape as `poe.oauth.status`) | A login attempt starts/succeeds/fails, a background token refresh succeeds/fails, or logout. |
| `poeProfile` | `PoeProfilePayload` — full profile: `poeUuid`, `name`, `locale`, `twitch`, `fetchedAt`, `error`, `cost` (always populated when present, no `includeCost` opt-in on a broadcast) | A `/profile` fetch completes (or fails), letting a non-blocking `poe.profile.*` caller learn the result asynchronously. |
| `poeLeagues` | `PoeLeaguesPayload` (same shape as `poe.leagues.list`'s response) | A bulk `GET /leagues` fetch completes (`status: "ok"`) or fails (`status: "error"`). |
| `poeLeagueDetail` | `PoeLeagueDetailPayload` (same shape as `poe.leagues.detail`'s response) | A `GET /league/{name}` fetch completes — `status: "ok"` (found), `"miss"` (PoE reports no such league), or `"error"`. |
| `poeRateLimit` | `PoeRateLimitStatusPayload` (same shape as `poe.ratelimit.status`) | Any `poe.profile.*`/`poe.leagues.*` fetch completes — always the full policy snapshot, not a diff. |
| `clientlog` | `ParsedEvent`: `{"type": "...", "timestamp": "...", "data": {...}}` | Any live-relevant `Client.txt` event (area entry, level-up, death, chat, whisper, achievement, hideout discovery, PvP queue, passive allocation, quest event, general event, session start, login/char-select screen, alt-tab). See `internal/proto`'s `Event*` constants for the full `type` set — a subset of everything actually written to the database (e.g. `played`/`passives_snapshot`/`guild_*` are DB-only, never broadcast). |
| `system` | `{"type": "shutdown", "reason": "version-upgrade"}` | This instance is stepping down for a newer one (singleton election, [ADR-001](decisions/001-single-shared-instance-lifecycle.md)). |

---

## HTTP endpoints

#### `GET /health`

Plain HTTP (not WebSocket), for process-liveness checks:

```json
{"status": "ok", "version": "0.1.0", "uptime": "1h2m3s"}
```
