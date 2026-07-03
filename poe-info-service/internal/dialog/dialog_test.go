package dialog

import (
	"database/sql"
	"strings"
	"testing"

	_ "modernc.org/sqlite"
)

func openTestDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	if _, err := db.Exec(`CREATE TABLE npc_dialog_entries (
		message_hash  TEXT NOT NULL PRIMARY KEY,
		npc_name      TEXT NOT NULL,
		npc_name_hash TEXT NOT NULL,
		label         TEXT
	);`); err != nil {
		db.Close()
		t.Fatalf("create schema: %v", err)
	}
	t.Cleanup(func() { db.Close() })
	return db
}

func TestUpsertEntriesInsertsNewRows(t *testing.T) {
	db := openTestDB(t)
	entries := []Entry{
		{NpcName: "Nessa", NpcNameHash: "nh1", MessageHash: "mh1"},
		{NpcName: "Nessa", NpcNameHash: "nh1", MessageHash: "mh2"},
	}

	inserted, err := UpsertEntries(db, entries)
	if err != nil {
		t.Fatalf("UpsertEntries: %v", err)
	}
	if inserted != 2 {
		t.Errorf("inserted = %d, want 2", inserted)
	}

	var count int
	if err := db.QueryRow(`SELECT COUNT(*) FROM npc_dialog_entries`).Scan(&count); err != nil {
		t.Fatalf("count: %v", err)
	}
	if count != 2 {
		t.Errorf("row count = %d, want 2", count)
	}
}

func TestUpsertEntriesSkipsExistingRows(t *testing.T) {
	db := openTestDB(t)
	if _, err := db.Exec(`INSERT INTO npc_dialog_entries (message_hash, npc_name, npc_name_hash, label) VALUES ('mh1', 'Nessa', 'nh1', 'hand-assigned')`); err != nil {
		t.Fatalf("seed: %v", err)
	}

	inserted, err := UpsertEntries(db, []Entry{
		{NpcName: "Nessa", NpcNameHash: "nh1", MessageHash: "mh1"}, // already present
		{NpcName: "Nessa", NpcNameHash: "nh1", MessageHash: "mh2"}, // new
	})
	if err != nil {
		t.Fatalf("UpsertEntries: %v", err)
	}
	if inserted != 1 {
		t.Errorf("inserted = %d, want 1", inserted)
	}

	var label string
	if err := db.QueryRow(`SELECT label FROM npc_dialog_entries WHERE message_hash = 'mh1'`).Scan(&label); err != nil {
		t.Fatalf("query label: %v", err)
	}
	if label != "hand-assigned" {
		t.Errorf("label = %q, want existing label preserved", label)
	}
}

func TestUpsertEntriesEmptyIsNoop(t *testing.T) {
	db := openTestDB(t)
	inserted, err := UpsertEntries(db, nil)
	if err != nil {
		t.Fatalf("UpsertEntries: %v", err)
	}
	if inserted != 0 {
		t.Errorf("inserted = %d, want 0", inserted)
	}
}
