# CLI Reference

`l2p-poe` can be invoked headlessly from the command line. Any unrecognised
invocation falls through to the GUI.

poe-info-service owns the database exclusively (ADR-006) — `l2p-poe` never
opens it directly, including from its own CLI. Client.txt is tailed and
ingested continuously by poe-info-service itself while it runs, not via a
CLI verb here. Subcommands that touch the database, such as `dialog ingest`,
live on poe-info-service's own CLI instead — see
[`poe-info-service/docs/cli.md`](../poe-info-service/docs/cli.md).

---

## `dialog hash`

Hashes NPC dialog entries using the canonical algorithm
(NFC normalise → trim → UTF-8 → SHA-256 → first 16 hex chars) and prints
the result as JSON. Nothing is written to the database.

**Batch (JSON file or stdin):**

```
l2p-poe dialog hash [file.json]
```

Input is a JSON array of objects with `npc_name` and `message` keys.
If no file is given, JSON is read from stdin.

```json
[
  { "npc_name": "Nessa", "message": "Welcome to Lioneye's Watch." }
]
```

Output:

```json
[
  {
    "npc_name": "Nessa",
    "npc_name_hash": "489fd650993e6b11",
    "message_hash":  "28f376a6781b3308"
  }
]
```

**Single entry (direct args):**

```
l2p-poe dialog hash "NPC Name" "message text"
```

Same output format, one element in the array.

---

## `dialog ingest` — see poe-info-service's CLI reference

Writing hashed entries into `npc_dialog_entries` is **not** an `l2p-poe`
subcommand — poe-info-service owns the database exclusively (ADR-006), so
that verb lives on poe-info-service's own CLI. See
[`poe-info-service/docs/cli.md`](../poe-info-service/docs/cli.md#dialog-ingest)
for `poe-info-service dialog ingest`'s usage, input format, and the full
hash-then-ingest workflow.

---

## Hash contract

Two code paths currently produce dialog hashes — `l2p-poe dialog hash` and
the Python dev script (`poe-info-service/dev/log_split/parse_npc_dialog.py`)
— and share one algorithm (`dialogHash()` in `src/util/DialogHash.h`):

1. NFC-normalise the text
2. Trim leading/trailing whitespace
3. Encode as UTF-8
4. SHA-256
5. Take the first 16 hex characters

This means a hash produced on the command line or via the Python dev script
is always identical for the same input string.
