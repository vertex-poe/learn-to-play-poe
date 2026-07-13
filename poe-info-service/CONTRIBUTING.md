<!-- CONTRIBUTING.md (markdown) -->

# Contributing

`poe-info-service` is a standalone Go binary developed alongside
[learn-to-play-poe](../), but built and versioned independently of it — see
root [ADR-006](../docs/decisions/006-poe-info-service.md) for why this exists
as a separate service rather than app-internal logic. Any addon on the same
machine can depend on it; `l2p-poe` is simply its first consumer.

## Documentation

### Architecture decisions

[`docs/decisions/`](docs/decisions/) holds this service's own ADRs — its
process lifecycle, distribution model, API versioning policy, and credential
handling are decided here, separately from `l2p-poe`'s ADRs, because they
concern the service's own architecture rather than any one consumer's:

- [ADR-001: Single Shared Instance, Ephemeral Lifecycle](docs/decisions/001-single-shared-instance-lifecycle.md)
- [ADR-002: Distribution and Self-Update Model](docs/decisions/002-distribution-and-self-update.md)
- [ADR-003: Backward-Compatible Client API, Not Backward-Compatible Database Schema](docs/decisions/003-client-api-versioning.md)
- [ADR-004: Credential Custody for POESESSID and OAuth Sessions](docs/decisions/004-credential-custody.md)
- [ADR-005: Credential Storage Mechanism](docs/decisions/005-credential-storage-mechanism.md)
- [ADR-006: User-Facing Config Lives in the External TOML File, Never in SQLite](docs/decisions/006-user-config-storage.md)
- [ADR-007: Outbound HTTP Integration Policy](docs/decisions/007-outbound-http-integration-policy.md)

Read ADR-001 first — it explains the singleton-election and keep-alive model
that most of the rest of the service is built around.

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

## Building

Requires Go (see [`go.mod`](go.mod) for the minimum version). No cgo, no
platform SDK, and no dependency on the Qt/CMake toolchain that builds
`l2p-poe` — this is a plain `go build`.

From the repo root, via [`just`](https://just.systems/) (see the root
[CONTRIBUTING.md](../CONTRIBUTING.md) for installing `just`):

```bash
just service-build   # builds bin/poe-info-service(.exe)
just service-test     # go test ./...
just service-run -- --log-path "C:\Games\PoE\logs\Client.txt"
```

Or directly:

```bash
go build -C poe-info-service -o ../bin/poe-info-service .
go test  -C poe-info-service ./...
```

`just test` (run from the repo root) runs this service's tests alongside
`l2p-poe`'s ctest suite — always run it after making changes here.

### Running standalone during development

`main.go`'s flags let you point the service at a specific install and
database without going through `l2p-poe` at all:

```bash
go run ./poe-info-service \
  --install-dir "C:\Games\PoE" \
  --log-path    "C:\Games\PoE\logs\Client.txt" \
  --data-dir    "C:\path\to\scratch-dir" \
  --service-log "C:\path\to\service-debug.log"
```

`--install-dir` is repeatable — pass it once per candidate install and the
service ingests every one that actually exists on disk concurrently (each
gets its own `installs` row and tailer), skipping any that don't. `--log-path`
is an explicit override for a single exact Client.txt path (as above), and
takes priority over `--install-dir` resolution when set.

Because of the singleton election in ADR-001, a second instance started this
way while another is already listening on the same port will negotiate with
it and exit (or ask it to step down, if this build's version is newer) rather
than binding the port itself — pass a different `--port` if you want two
instances running side by side for comparison.

### Windows Credential Manager during tests

`internal/creds`'s tests write to the real OS credential store (there is no
mock backend yet — see ADR-005's consequences), but under a distinct test
service name so they never touch a real stored `POESESSID`.

## Adding a new WebSocket method

1. Add the request/response (or event) shape to [`internal/proto`](internal/proto/proto.go).
2. Add a handler in [`internal/server/server.go`](internal/server/server.go) and register it in `handleRequest`'s switch.
3. If it's a read, back it with a method on [`internal/query.DB`](internal/query/query.go); if it's a write reacting to log events, add it to [`internal/ingest.Writer`](internal/ingest/writer.go).
4. Once shipped, per ADR-003, that method's response shape is permanent — new fields only, never removed/renamed/repurposed ones.

## Steam presence

`internal/steam` fetches the local Steam-based PoE client's rich-presence
text — the same rich text the Steam client itself shows (e.g. a game's
league/character/level) — by scraping
`steamcommunity.com/miniprofile/<id3>`. No credential is needed, but this is
an undocumented HTML page with no stability guarantee from Valve — see
[ADR-007](docs/decisions/007-outbound-http-integration-policy.md).

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
[ADR-003](docs/decisions/003-backward-compatible-client-api-not-backward-compatible-database-schema.md),
these shapes are permanent once shipped), since a future second source (e.g.
Client.txt-derived level/class, or a Steam Deck acting as the *primary*
source when Client.txt isn't locally available — see ROADMAP) can then be
added as a new `source` value without any rename. The raw text itself is
still available verbatim via `steam.presence` / the `steamPresence` topic.

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
`api.pathofexile.com`, exposed over three WebSocket methods and one push
topic (`internal/server/poe_oauth.go`, `internal/proto.PoeOAuthStatusPayload`):

- **`poe.oauth.login`** — starts an interactive login: this service itself
  opens the system's default browser to PoE's authorize page and runs a
  loopback HTTP listener (`127.0.0.1:{dynamic port}/auth/path-of-exile`) to
  capture the redirect, per
  [ADR-004](docs/decisions/004-credential-custody.md) — no WebView-capable
  client is needed, unlike `POESESSID`. The response only confirms the flow
  (re)started (`{"started": true|false}`); the actual outcome arrives via
  `TopicPoeOAuthStatus`, or a follow-up `poe.oauth.status` call.
- **`poe.oauth.status`** — the current state: `authorized`, `inProgress`,
  and (only while authorized) `username`/`scope`/`accessExpiration`. Never
  the token itself, per ADR-004.
- **`poe.oauth.logout`** — discards the stored token and cancels any
  scheduled refresh.

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

See [`_reference/poe-apis/poe-apis.md`](../_reference/poe-apis/poe-apis.md)
§3.3 for the full protocol reference this implements (PKCE mechanics,
loopback redirect rationale, token lifecycle state machine, the
`client_secret` refresh quirk). This service only implements
*authentication* — the OAuth data endpoints themselves (`/character`,
`/stash`, etc., §6.2 of that doc) are not yet wired up.

**Automated tests never exercise `loadPoeOAuthToken`/`savePoeOAuthToken`/
`clearPoeOAuthToken`** (poe_oauth.go's sole points of contact with
`internal/creds`), for the same reason noted above for
`credentials.store`/`has`/`delete`: a test must never risk touching or
deleting a real stored credential. Only the in-memory login/status/refresh
logic around that boundary is covered.

## Database schema

The `l2p` database schema lives in [`internal/schema/sql/`](internal/schema/sql/)
and is migrated by [`internal/schema/schema.go`](internal/schema/schema.go).
Bump `kVersion` and add a branch to `migrate()` for any change — additive
only, per ADR-003. See the root project's [`docs/schema.md`](../docs/schema.md)
for a full table-by-table reference.
