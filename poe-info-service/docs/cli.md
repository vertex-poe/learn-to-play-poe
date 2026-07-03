# CLI Reference

`poe-info-service` can be invoked headlessly from the command line for
subcommands that don't start the long-running service. Any unrecognised
invocation falls through to normal service startup (see the root project's
`docs/cli.md` for `l2p-poe`'s own CLI, which is a separate binary).

poe-info-service owns the database exclusively (this project's own
[ADR-006](decisions/006-user-config-storage.md); see also root
[ADR-006](../../docs/decisions/006-poe-info-service.md) for why `l2p-poe`
depends on this service rather than touching the database itself). The
`dialog ingest` subcommand below always resolves `poe-info-service.db` the
default way (project root when launched via `just`/an IDE with CWD set
there, otherwise next to the executable) — it doesn't accept an override.

The long-running service itself (not a subcommand) does accept two
independent startup flags for this: `--data-dir <dir>` overrides where the
database (and the config file's default location) lives, and `--config
<file>` overrides the exact config file path regardless of `--data-dir`.
Both exist mainly for test isolation — see `l2p-poe --service-data-dir`,
which forwards to this service's `--data-dir` when launching it.

---

## `dialog ingest`

Writes already-hashed NPC dialog entries into the `npc_dialog_entries`
table. Existing rows (by `message_hash`) are left untouched — hand-assigned
labels are never overwritten. Prints a count of newly inserted vs
already-present rows.

```
poe-info-service dialog ingest [file.json]
```

Reads from the given file, or from stdin if no file is given. Input is a
JSON array with `npc_name`, `npc_name_hash`, and `message_hash` keys — the
exact shape `l2p-poe dialog hash` prints:

```json
[
  {
    "npc_name": "Nessa",
    "npc_name_hash": "489fd650993e6b11",
    "message_hash": "28f376a6781b3308"
  }
]
```

Hashing itself is **not** implemented here — it stays on the C++ side
(`src/util/DialogHash.h`) so there is exactly one implementation of the hash
algorithm (NFC-normalise → trim → UTF-8 → SHA-256 → first 16 hex chars) to
keep hashes interchangeable across every caller. `poe-info-service` only
persists entries it's handed.

**Workflow** — hash on `l2p-poe`, then pipe into poe-info-service to write:

```sh
# Inspect hashes without writing
l2p-poe dialog hash npc_dialog.json

# Hash, then write to DB in one step
l2p-poe dialog hash npc_dialog.json | poe-info-service dialog ingest

# Or write from an already-hashed file
poe-info-service dialog ingest npc_dialog_hashed.json
```
