# ADR-006: User-Facing Config Lives in the External TOML File, Never in SQLite

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

Clients need to list, get, and set poe-info-service configuration (e.g. debug-logging flags) over the existing WebSocket API. The service already has two candidate stores in place before this decision:

- `poe-info-service.toml`, read today by a hand-rolled, read-only parser (`config/config.go`) for exactly two bootstrap values, `bind` and `port`. It has no write path at all.
- `poe-info-service.db`, a SQLite database the service exclusively owns across restarts ([ADR-001](001-single-shared-instance-lifecycle.md)), already holding a generic key/value `state` table used for one thing today: internal `log_offset` bookkeeping.

The setting that forced this decision, `debug_logging`, has two requirements in tension:

1. It must be visible and editable in a plain external file, because the scenario that matters most for it is "the service won't start, and someone needs to inspect or change config without the service running" — a DB-only setting is useless here, since nothing can open the DB for that purpose if the service itself can't come up.
2. It must also be settable at runtime by a connected client, via a `config.set`-style WebSocket method, while the service is running.

### Alternatives considered and rejected

**1. Store all user-facing settings in the SQLite `state` table (or a new `settings` table), the same way `log_offset` is stored today.** Rejected outright for requirement #1: `sqlite3` is not a tool most users have or want to reach for mid-outage, and [ADR-001](001-single-shared-instance-lifecycle.md) makes the running service the DB's sole owner — encouraging direct inspection of the file it owns cuts against that. A setting a human needs to read or change specifically because the service won't start cannot live in a store that's hardest to read precisely when the service won't start.

**2. Hybrid: TOML supplies a default, SQLite holds an optional override that takes precedence when present.** This was the initial direction, on the theory that "service won't start" implies "no client ever ran to set an override, so the TOML file is guaranteed authoritative in exactly the case that matters." That guarantee only holds on a *first-ever* boot. The realistic failure is the *Nth* boot: the service ran fine for a while, a client called `config.set` (writing a DB override), and then something unrelated broke startup. A human now opens the TOML file to diagnose and sees a **stale default** while the value that was actually in effect sits in the DB — the harder-to-inspect half of the split, and exactly the artifact requirement #1 exists to avoid depending on. Flipping precedence (file-wins) doesn't fix it either — it would make a client's runtime `config.set` inert the moment anyone edits the file, breaking requirement #2. The tension is fundamental to splitting config across two stores, not something a precedence rule resolves.

**3. Chase a comment-preserving, round-trip TOML writer so `config.set` can rewrite the file without disturbing a human's own hand-written comments.** Rejected: no library in the Go ecosystem does this dependably today. `pelletier/go-toml/v2` (the actively maintained option) deliberately dropped preservation of pre-existing hand-written comments from its document model; the one library that advertises true lossless round-trip editing is pre-1.0, has effectively no adoption, and is maintained by a single author — the same profile this project already declined once for credential storage ([ADR-005](005-credential-storage-mechanism.md)'s rejection of unmaintained/thinly-maintained keyring forks). Adopting it here for the config path would repeat that mistake in a spot that matters more, not less.

**4. Switch the external file to JSON instead of TOML**, on the theory that `encoding/json` is a zero-dependency, trivially-correct round-trip, at the cost of no native comments. Viable as a fallback, but rejected in favor of TOML: the entire reason this setting needs to live in an external file is so a human can read an explanation next to it while diagnosing an outage, and TOML comments serve that better than a `_comment` string field bolted onto JSON.

## Decision

- **User-facing config has exactly one authoritative store: `poe-info-service.toml`.** The SQLite database is never a second source for it — it continues to hold only internal service bookkeeping (`log_offset` and the like), unchanged from today.
- Parsing and marshaling moves from the current hand-rolled reader to [`pelletier/go-toml/v2`](https://github.com/pelletier/go-toml/v2), gaining a real write path the project doesn't have today.
- **Comments are code-owned, not preserved.** Each setting's explanation is generated from its struct field tag and rewritten fresh every time the file is written. `config.set` regenerates the entire file canonically rather than editing it in place — no attempt is made to preserve whatever a human previously typed into the file by hand. This sidesteps the round-trip weak spot in alternative 3 entirely: the service is never trying to preserve arbitrary human text, only to re-emit its own canonical comments.
- Writes are atomic: write to a temp file in the same directory, then rename over `poe-info-service.toml`, so a crash mid-write cannot leave a human with a truncated or corrupt file to diagnose during exactly the outage this setting exists for.
- New WebSocket request methods, dot-namespaced consistent with existing ones (`credentials.store`, `chat.messages`): `config.list`, `config.get`, `config.set`. `config.set` validates the key against a known-settings registry, applies the change in memory immediately, and persists it to disk. No API version bump is required — new methods are additive under [ADR-003](003-client-api-versioning.md).

## Consequences

- **Positive**: there is exactly one place to look for user-facing config, and a human reading the TOML file during a "service won't start" outage always sees the real effective value — never masked by a hidden override in a second store.
- **Positive**: the emergency-readable file always carries explanatory comments regardless of whether a client has ever called `config.set`, since the service regenerates them on every write rather than depending on a human having authored them in the first place.
- **Positive**: adopts an actively maintained TOML library in place of a hand-rolled, read-only parser, gaining write support without taking on a thinly-maintained dependency.
- **Negative**: any comment a human manually adds to the file beyond the generated ones is lost the next time the service writes it (e.g. in response to a client's `config.set`). Accepted as a minor loss for a small, flat config surface.
- **Revisit if**: the config surface grows large or structured enough that preserving hand-authored comments/formatting becomes worth adopting a less mature library for, or JSON's simplicity ends up mattering more than TOML's comment support.
