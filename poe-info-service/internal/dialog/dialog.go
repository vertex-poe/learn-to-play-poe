// Package dialog persists NPC dialog entries hashed by `l2p-poe dialog
// hash`. Hashing itself stays on the C++ side (src/util/DialogHash.h) — this
// package only stores already-hashed entries, so there is exactly one
// implementation of the hash algorithm (NFC-normalise, trim, UTF-8, SHA-256,
// first 16 hex chars) to keep consistent across the CLI and the in-app form.
package dialog

import "database/sql"

// Entry mirrors the JSON shape `l2p-poe dialog hash` prints.
type Entry struct {
	NpcName     string `json:"npc_name"`
	NpcNameHash string `json:"npc_name_hash"`
	MessageHash string `json:"message_hash"`
}

// UpsertEntries inserts entries into npc_dialog_entries, skipping any row
// whose message_hash already exists so hand-assigned labels are never
// overwritten. Returns how many were newly inserted.
func UpsertEntries(db *sql.DB, entries []Entry) (inserted int, err error) {
	if len(entries) == 0 {
		return 0, nil
	}

	tx, err := db.Begin()
	if err != nil {
		return 0, err
	}
	defer tx.Rollback()

	stmt, err := tx.Prepare(`INSERT OR IGNORE INTO npc_dialog_entries (message_hash, npc_name, npc_name_hash) VALUES (?, ?, ?)`)
	if err != nil {
		return 0, err
	}
	defer stmt.Close()

	for _, e := range entries {
		res, err := stmt.Exec(e.MessageHash, e.NpcName, e.NpcNameHash)
		if err != nil {
			return inserted, err
		}
		if n, _ := res.RowsAffected(); n > 0 {
			inserted++
		}
	}

	if err := tx.Commit(); err != nil {
		return inserted, err
	}
	return inserted, nil
}
