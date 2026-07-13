<!-- architecture.md (markdown) -->

# Architecture

This is a reference for how `poe-info-service` actually works today — its
process responsibilities, and a deep dive into each of its major features.
For *why* things are built this way, see [`docs/decisions/`](decisions/); for
*how to build/test/contribute*, see [`../CONTRIBUTING.md`](../CONTRIBUTING.md).

## How it works

The service is one process, shared system-wide by whatever addons are
currently running (ADR-001). Its responsibilities:

1. **`internal/tailer`** polls `Client.txt` for new lines, resuming from the
   offset recorded in the install's row rather than re-reading from the start.
2. **`internal/parser`** turns raw log lines into typed `ParsedEvent`s.
3. **`internal/ingest`** applies each parsed event to the SQLite database
   (`Writer.HandleEvent`) — sessions, zone transitions, chat/DM rows,
   character snapshots, and more. Ingest is idempotent: re-processing the same
   line twice is always safe.
4. **`internal/server`** owns the WebSocket API: it negotiates which service
   instance is the running singleton on startup, serves `request`/`response`
   calls (e.g. `chat.messages`, `log.sessions`), and publishes a subset of
   live events to subscribers of the `clientlog` topic for overlay/alert use.
5. **`internal/store`** is this service's own small SQLite database (distinct
   from the client-owned `l2p` database) — cached API responses and
   process-local state such as the log tailer's resume offset.
6. **`internal/creds`** stores secrets (`POESESSID`, future OAuth tokens)
   directly in the OS credential store, never in a database file. Platform
   backends are selected by Go build tags — see ADR-005.
7. **`internal/query`** reads from the client-owned `l2p` database to answer
   the read-oriented WebSocket methods (session history, chat/DM lookups).
8. **`internal/schema`** creates and migrates the `l2p` database schema.
   Migrations are additive-only (new tables/columns, never removed or
   repurposed ones) per ADR-003 — the physical schema can evolve freely
   because no code outside the currently-running service version ever opens
   it directly.

This service owns the database exclusively (ADR-001): no addon, and no other
copy of this binary, opens it directly. Addons talk to whichever instance
won the startup election, over the WebSocket API only.

## Steam presence

`internal/steam` fetches the local Steam-based PoE client's rich-presence
text — the same rich text the Steam client itself shows (e.g. a game's
league/character/level) — by scraping
`steamcommunity.com/miniprofile/<id3>`. No credential is needed, but this is
an undocumented HTML page with no stability guarantee from Valve — see
[ADR-007](decisions/007-outbound-http-integration-policy.md).

This project supports only a **single** local Steam-based PoE client: which
steamid64 to track is user-facing, non-secret config — the `steam_id`
setting (`config.set`/`config.list`, persisted to `poe-info-service.toml`
like `install_dirs`/`executable_names`). Auto-discovering "who's currently
logged into Steam locally" isn't built yet (see ROADMAP's "Steam OpenID
webview login" item); `steam_id` must be entered manually today. Tracking
additional steamids (friends, for group play) is also on the ROADMAP, not
built yet.

The raw rich-presence text is parsed (`steam.ParseRichPresence`) into
**league**, **character level**, and **class** — e.g.
`"SSF Ancestors: 92 Warden - The Sarn Encampment"` → league `"SSF Ancestors"`,
level `92`, class `"Warden"`. The zone suffix is intentionally discarded:
Client.txt zone-transfer events (`area_entered`) already track the player's
current zone more authoritatively. Each parsed part is exposed as its own
concept-named request method / push topic — `character.level`,
`character.class`, `poe.league` — deliberately *not* named after Steam (per
[ADR-003](decisions/003-client-api-versioning.md), these shapes are permanent
once shipped), since a future second source (e.g. Client.txt-derived
level/class, or a Steam Deck acting as the *primary* source when Client.txt
isn't locally available — see ROADMAP) can then be added as a new `source`
value without any rename. The raw text itself is still available verbatim
via `steam.presence` / the `steamPresence` topic.

The official Steam Web API (`ISteamUser/GetPlayerSummaries`,
`internal/steam/official.go`) and the `steamApiKey` credential it needs
(supplied via `credentials.store`, same as `POESESSID`) are **not** used by
rich presence — they're unused/dormant code kept around in case
`personaName`/`gameName`/`gameAppId`/`inGame` are wanted again later; process
scanning already tells this service whether the game is running, and
Client.txt already tells it most of what's going on.

**Every rich-presence request fetches first if stale.** Unlike a pure
subscription push, `steam.presence`/`character.level`/`character.class`/
`poe.league` always fetch a fresh value first when the cached copy is more
than 25s old (`richPresenceRequestTTL` in `internal/server/steam.go`),
regardless of subscription state, then return the (possibly just-refreshed)
cached copy. A Client.txt zone-transfer event from the Steam-associated
install (`isSteamInstall` — matched by install path containing `steamapps`,
or by a currently running process whose name contains "steam") triggers the
same on-demand fetch, subject to the same 25s gate.

**Background polling is still subscriber-gated.** The background poller
(`watchRichPresence` in `internal/server/steam.go`) only ever contacts Steam
on its own initiative (every 60s by default) while at least one client is
subscribed to `steamPresence`/`character.level`/`character.class`/
`poe.league` — Steam is a rate-limited, ToS-sensitive external resource, so
an idle service with nobody listening must not poll it (see ADR-007). It
only broadcasts a topic whose value actually changed since the last fetch —
a level-up doesn't also fire a spurious league-changed event.

## PoE OAuth (official Developer API)

`internal/poe` implements the OAuth 2.0 Authorization Code + PKCE flow for
`api.pathofexile.com`, exposed over four WebSocket methods and one push
topic (`internal/server/poe_oauth.go`, `internal/proto.PoeOAuthStatusPayload`):

- **`poe.oauth.login`** — starts an interactive login: this service itself
  opens the system's default browser to PoE's authorize page and runs a
  loopback HTTP listener (`127.0.0.1:{dynamic port}/auth/path-of-exile`) to
  capture the redirect, per
  [ADR-004](decisions/004-credential-custody.md) — no WebView-capable
  client is needed, unlike `POESESSID`. The response only confirms the flow
  (re)started (`{"started": true|false}`); the actual outcome arrives via
  `TopicPoeOAuthStatus`, or a follow-up `poe.oauth.status` call.
- **`poe.oauth.status`** — the current state: `authorized`, `inProgress`,
  and (only while authorized) `username`/`scope`/`accessExpiration`. Never
  the token itself, per ADR-004.
- **`poe.oauth.logout`** — discards the stored token and cancels any
  scheduled refresh.
- **`poe.accounts.list`** — every account this service knows of (from
  `Client.txt` guild events and/or a PoE OAuth login, merged onto one row by
  name — see the `accounts` table), each with `name`, `poeUuid` (empty if
  that account has never been OAuth-authenticated locally), and `active`
  (true for the one account, if any, currently signed in via OAuth). Lets a
  client decide whether to show an account switcher at all — only once this
  list has more than one entry — without every other request needing an
  explicit account argument for the common single-account case.

The resulting token set is persisted through `internal/creds` under key
`poeOAuthToken` (a JSON-serialized `poe.Token`, including the derived
`birthday`/`access_expiration`/`refresh_expiration` fields) — this service
both originates and owns this credential, unlike `steamApiKey`/`POESESSID`
which a client supplies via `credentials.store`. A background timer
refreshes the access token 5 minutes before it expires and reschedules
itself; a failed refresh retries after a short backoff rather than
immediately forcing re-login, unless the assumed 7-day refresh-token
lifetime has actually elapsed, in which case the token is dropped and
`poe.oauth.status` reports unauthorized again.

See [`_reference/poe-apis/poe-apis.md`](../../_reference/poe-apis/poe-apis.md)
§3.3 for the full protocol reference this implements (PKCE mechanics,
loopback redirect rationale, token lifecycle state machine, the
`client_secret` refresh quirk). This service only implements
*authentication* plus the `/profile` and `/leagues` data endpoints (see
below) — the rest of the OAuth data endpoints (`/character`, `/stash`, etc.,
§6.2 of that doc) are not yet wired up.

poe_oauth.go's sole points of contact with `internal/creds`
(`loadPoeOAuthToken`/`savePoeOAuthToken`/`clearPoeOAuthToken`) are never
exercised by automated tests — see `../CONTRIBUTING.md`'s note on credential
testing.

For the database schema, see `../CONTRIBUTING.md`'s "Adding a schema
migration" section.

## PoE Leagues

`poe.leagues.list` (`internal/server/poe_leagues.go`) serves the OAuth data
API's `GET /leagues` — the one endpoint in that API that requires no Bearer
token at all (`internal/poe.Client.FetchLeagues` sends no `Authorization`
header, unlike `FetchProfile`). Because it's account-independent, this
method needs no `account` selector, unlike `poe.profile.*`.

Results are cached in the `leagues` table (see `schema.md`),
keyed by `(name, realm)` since a league name like "Standard" repeats
identically across realms. A request accepts optional `realm`
(`pc`/`xbox`/`sony`/`poe2`, default `pc`), `type` (`main`/`event`/`season`,
default `main`), and `season` (only meaningful for `type: "season"`) filters
mirroring the endpoint's own query parameters, plus the same
`maxAgeSeconds`/`priority`/`wait` triple `poe.profile.locale`/
`poe.profile.twitch` accept, and follows the identical fresh/pending/ok
response convention (see the PoE OAuth section above) — a fresh cache hit
responds immediately; otherwise a fetch is submitted to the same
`s.poeQueue` used by `/profile`, under a stable `poeOAuthLeaguesPolicyHint`
tag, and the result (or a background failure) is published to
`TopicPoeLeagues` for any `wait:false` caller. Default cache TTL is 6 hours
(`poeLeaguesCacheTTL`, `poe_constants.go`) — leagues rarely change — clamped
to a 5-minute floor (`poeLeaguesMinRefetchAge`) regardless of what a caller
requests.

This is unrelated to `poe.league` (singular, `internal/server/steam.go`) —
that's the player's *current* league, parsed from Steam rich-presence text;
`poe.leagues.list` is the full catalogue of currently active leagues fetched
from the PoE OAuth API. `poe.league`'s response does get a zero-cost bonus
from this table though: `leagueDetailFor` joins in whatever's already
cached for the parsed league name (assuming the `pc` realm, since Steam rich
presence only ever describes a `pc` client) as the response's `detail`
field — a plain DB read, never a fetch of its own, and nil if nothing's
cached yet.

`poe.leagues.detail` (same file) serves one specific league's cached row by
name. There is no dedicated single-league endpoint in the PoE OAuth API
today (the Legacy API has `GET /api/leagues/{id}`, per
`_reference/poe-apis/poe-apis.md` §"Leagues (expanded)," but the OAuth API
doesn't mirror it) — a needed refresh here (`submitLeaguesFetch`) is
literally the same bulk `GET /leagues` call `poe.leagues.list` makes,
sharing its `reqqueue` dedup key so the two never double-fetch when called
concurrently for the same `realm`/`type`, with the caller (`handlePoeLeaguesDetail`)
projecting the one requested league back out of the bulk result. The
request/response shape doesn't expose this — a future dedicated
single-league OAuth endpoint could swap in behind `submitLeaguesFetch` with
no wire-visible change.

### Fetch policy: avoiding overfetching

Every `ensure*`-style cache/fetch function (`ensurePoeProfile`,
`ensureLeagues`, `ensureLeagueDetail`) takes an explicit `fetchPolicy`
alongside `maxAge` — the two are orthogonal (`internal/server/poe_fetch.go`):
`maxAge` decides whether an existing cache entry counts as fresh;
`fetchPolicy` decides whether a stale-or-missing entry is even allowed to
trigger a real, rate-limit-budget-spending fetch at all:

- `"ifStale"` (default, and every one of these methods' original — the
  only — behavior before this existed): fetch only if the cache is missing
  or older than `maxAge`.
- `"never"`: a pure read-only peek. Return whatever's cached, however
  stale, and never submit a fetch.
- `"always"`: force a fetch even over an already-fresh cache entry.

This was added (2026-07-13, following an architecture review by Opus) to
let a caller distinguish "give me whatever's cheaply available" from "it's
worth spending a query to get something newer" without the service ever
guessing wrong in either direction — the previous fixed always-fetch-on-stale
behavior had no way to serve a *stale* cached value without either treating
it as good enough (wrong once staleness actually matters to the caller) or
erroring outright even when something real was on hand (the exact gap
`TestHandlePoeProfileLocale_StaleCache_NotAuthenticated_ServesStaleInsteadOfError`
now covers: a known-but-inactive account's stale cached profile is served
with `status: "stale"` today, where it used to be an unconditional error).

The response vocabulary was extended to match: `status` keeps its original
`"fresh"`/`"pending"`/`"ok"` values (plus `"error"`, topic-only) for
continuity, and gains `"stale"`/`"miss"` for the peek case, alongside two
new orthogonal fields, `freshness` (`"fresh"`/`"stale"`/`"miss"`) and
`fetching` (bool) — added rather than replacing `status` outright, per
ADR-003's response-shape-permanence rule. A `"pending"` response (and the
peek/`"stale"` case) now also carries whatever's cached, even if stale,
rather than nothing — see `docs/api.md`'s "Fetch policy and cost reporting"
section for the full wire reference.

### Rate-limit cost reporting

Two related but distinct pieces of "what did this cost" information exist,
deliberately not merged into one:

- **Per-response provenance** (`proto.FetchCost`, opt-in via a request's
  `includeCost: true`, `internal/server/poe_ratelimit.go`'s
  `buildFetchCost`): which API and rate-limit policy a specific call's
  fetch was actually billed against, and that policy's rule state as of
  *this* response — computed by re-parsing the same response headers
  `reqqueue`'s dispatch loop itself learns from
  (`poeOAuthRateLimitHeaders`), not by reading `reqqueue`'s aggregate
  internal state (which, at the point `Exec` runs, is necessarily one
  fetch stale — see `dispatch`'s doc comment: policy state updates happen
  *after* `Exec` returns).
- **Live per-policy budget** (`poe.ratelimit.status`/`TopicPoeRateLimit`,
  `internal/reqqueue.Queue.Policies`): every policy this service's PoE
  OAuth queue currently knows about, independent of any one request —
  exposed once, rather than stapled onto every domain response, since it's
  shared state that would otherwise be duplicated (and go stale) across
  every payload that carried it. `publishPoeRateLimitStatusAfter` pushes an
  updated full snapshot to `TopicPoeRateLimit` after every fetch — spawned
  as its own goroutine waiting on the same `Waiter` the triggering request
  may or may not itself be blocking on, specifically so it observes
  `reqqueue`'s state *after* `dispatch` has applied it, never from inside
  `Exec`.

Both surfaces report `Remaining`/`ResetsAt` as best-effort estimates —
`internal/reqqueue`'s own documented simplification (a saturated rule's
reset is estimated as its full `Period` plus a fixed buffer, not an exact
rolling-window computation) propagates through unchanged.
