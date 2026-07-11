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

`internal/steam` fetches a Steam user's "playing now" status from two
sources, combined into one entry per tracked steamid64 and exposed over the
`steam.presence` WebSocket method / `steamPresence` push topic (see
`internal/proto.SteamPresenceEntry`, `internal/server/steam.go`):

- **Official Steam Web API** (`ISteamUser/GetPlayerSummaries`) — gives
  `personaName`/`gameName`/`gameAppId`/`inGame`. Requires a Steam Web API
  key. A client supplies one the same way it supplies `POESESSID` — via
  `credentials.store` with `key: "steamApiKey"` — never over config.set, and
  this service never returns the key's value back to a client, only whether
  one is present (`credentials.has`). **A missing key is not an error**:
  these fields simply stay empty/false, and the rich-presence scrape below
  still runs independently.
- **Unofficial rich-presence scrape** (`steamcommunity.com/miniprofile/<id3>`)
  — gives `richPresence`, the actual rich text the Steam client itself shows
  (e.g. a game's league/character/level). No credential needed, but this is
  an undocumented HTML page with no stability guarantee from Valve — see
  [ADR-007](docs/decisions/007-outbound-http-integration-policy.md).

Which steamid64s to track is user-facing, non-secret config: the
`steam_ids` setting (`config.set`/`config.list`, persisted to
`poe-info-service.toml` like `install_dirs`/`executable_names`).

**Polling is subscriber-gated.** The background poller
(`watchSteamPresence` in `internal/server/steam.go`) only ever contacts
Steam while at least one client is subscribed to the `steamPresence` topic
— Steam is a rate-limited, ToS-sensitive external resource, so an idle
service with nobody listening must not poll it (see ADR-007). Practically,
this means a client must **subscribe** to get live data: requesting
`steam.presence` without subscribing returns whatever is cached (possibly
every entry still `"pending"`) and never triggers a fetch by itself.

## Database schema

The `l2p` database schema lives in [`internal/schema/sql/`](internal/schema/sql/)
and is migrated by [`internal/schema/schema.go`](internal/schema/schema.go).
Bump `kVersion` and add a branch to `migrate()` for any change — additive
only, per ADR-003. See the root project's [`docs/schema.md`](../docs/schema.md)
for a full table-by-table reference.
