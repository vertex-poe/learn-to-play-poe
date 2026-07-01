# CLI Reference

`l2p-poe` can be invoked headlessly from the command line. Any unrecognised
invocation falls through to the GUI.

---

## `ingest`

```
l2p-poe ingest
```

Reads every `Client.txt` log found in configured install directories and
writes new records into the database. Skips files whose size and modification
time match the last-seen state. Progress is printed to stdout.

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
    "npc_name_hash": "a3f1c8b2e9d04712",
    "message_hash":  "7e2a1f9b3c504d81"
  }
]
```

**Single entry (direct args):**

```
l2p-poe dialog hash "NPC Name" "message text"
```

Same output format, one element in the array.

---

## `dialog ingest`

Hashes NPC dialog entries and writes them into the `npc_dialog_entries` table.
Existing rows are left untouched — hand-assigned labels are never overwritten.
Prints a count of newly inserted vs already-present rows.

**Batch (JSON file or stdin):**

```
l2p-poe dialog ingest [file.json]
```

Uses the same input format as `dialog hash`.

**Single entry (direct args):**

```
l2p-poe dialog ingest "NPC Name" "message text"
```

**Example workflow** — hash first to check output, then ingest:

```sh
# Inspect hashes without writing
l2p-poe dialog hash npc_dialog.json

# Write to DB
l2p-poe dialog ingest npc_dialog.json

# Or pipe directly
l2p-poe dialog hash npc_dialog.json | l2p-poe dialog ingest
```

> Note: the piped form re-hashes already-hashed output. The input schema
> for `dialog ingest` is the same as the *input* to `dialog hash` (raw
> `npc_name`/`message` pairs), not its output. Pipe from the original JSON,
> not from `dialog hash`.

---

## Hash contract

All three code paths that produce dialog hashes — the CLI, the in-app form,
and the log ingest worker — share the same function (`dialogHash()` in
`src/DialogHash.h`):

1. NFC-normalise the text
2. Trim leading/trailing whitespace
3. Encode as UTF-8
4. SHA-256
5. Take the first 16 hex characters

This means a hash produced in-app, on the command line, or via the Python
dev script is always identical for the same input string.
