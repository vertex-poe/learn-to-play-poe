<!-- CONTRIBUTING.md (markdown) -->

# Contributing

`poe-info-service` is a standalone Go binary developed alongside
[learn-to-play-poe](../), but built and versioned independently of it — see
root [ADR-006](../docs/decisions/006-poe-info-service.md) for why this exists
as a separate service rather than app-internal logic. Any addon on the same
machine can depend on it; `l2p-poe` is simply its first consumer.

## Documentation

### API reference

[`docs/api.md`](docs/api.md) is the wire-level reference for the WebSocket
API: every method's request/response shape and every push topic's payload.
Update it in the same change whenever a method/topic is added or a shape
gains a field (see "Adding a new WebSocket method" below).

### Database schema

[`docs/schema.md`](docs/schema.md) documents every table in the SQLite
database this service owns — reference/lookup tables, sessions, movement,
character progression, social/chat, game events, PoE OAuth-derived data, the
app-state store, and the event history spine — along with the design
patterns used throughout. Update it in the same change as any schema
migration (see "Adding a schema migration" below).

### Architecture

[`docs/architecture.md`](docs/architecture.md) is a reference for how the
service actually works — process responsibilities, and a deep dive into each
major feature (Steam presence, PoE OAuth). Read it when you need to
understand existing behavior, not just add new behavior.

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
5. Document it in [`docs/api.md`](docs/api.md) — the request/response shape and, if relevant, the corresponding push topic.

For the Steam presence and PoE OAuth features specifically (behavior,
caching/polling rules, credential handling), see
[`docs/architecture.md`](docs/architecture.md) — those sections moved there
since they document existing behavior rather than how to contribute.

## Adding a schema migration

The `l2p` database schema lives in [`internal/schema/sql/`](internal/schema/sql/)
and is migrated by [`internal/schema/schema.go`](internal/schema/schema.go).
Bump `kVersion` and add a branch to `migrate()` for any change — additive
only, per ADR-003 (new tables/columns only, never removed or repurposed
ones). See [`docs/schema.md`](docs/schema.md) for a full table-by-table
reference, and update it in the same change.

**Automated tests never exercise `internal/creds`-backed credential
paths directly** (`loadPoeOAuthToken`/`savePoeOAuthToken`/`clearPoeOAuthToken`,
`credentials.store`/`has`/`delete`): a test must never risk touching or
deleting a real stored credential. Only the in-memory logic around that
boundary is covered — see "Windows Credential Manager during tests" above
for the one exception (a distinct test service name).
