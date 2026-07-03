package schema

import (
	"database/sql"
	"strings"
	"testing"

	_ "modernc.org/sqlite"
)

func openMemDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	return db
}

func TestEnsureSchema_freshDatabase(t *testing.T) {
	db := openMemDB(t)
	if err := EnsureSchema(db); err != nil {
		t.Fatalf("EnsureSchema: %v", err)
	}

	var version int
	if err := db.QueryRow("PRAGMA user_version").Scan(&version); err != nil {
		t.Fatalf("read user_version: %v", err)
	}
	if version != kVersion {
		t.Errorf("user_version = %d, want %d", version, kVersion)
	}

	for _, table := range []string{"installs", "sessions", "areas", "chats", "whispers", "session_alt_tabs", "passive_point_snapshots"} {
		var name string
		err := db.QueryRow("SELECT name FROM sqlite_master WHERE type='table' AND name=?", table).Scan(&name)
		if err != nil {
			t.Errorf("table %q missing: %v", table, err)
		}
	}

	var areaCount int
	if err := db.QueryRow("SELECT COUNT(*) FROM areas").Scan(&areaCount); err != nil {
		t.Fatalf("count areas: %v", err)
	}
	if areaCount == 0 {
		t.Error("expected seed data to populate areas table, got 0 rows")
	}
}

func TestEnsureSchema_idempotent(t *testing.T) {
	db := openMemDB(t)
	if err := EnsureSchema(db); err != nil {
		t.Fatalf("first EnsureSchema: %v", err)
	}
	var firstCount int
	db.QueryRow("SELECT COUNT(*) FROM areas").Scan(&firstCount)

	if err := EnsureSchema(db); err != nil {
		t.Fatalf("second EnsureSchema: %v", err)
	}
	var secondCount int
	db.QueryRow("SELECT COUNT(*) FROM areas").Scan(&secondCount)

	if firstCount != secondCount {
		t.Errorf("area count changed across idempotent calls: %d vs %d", firstCount, secondCount)
	}
}

func TestEnsureSchema_migratesOldVersion(t *testing.T) {
	db := openMemDB(t)
	if _, err := db.Exec(schemaSQL); err != nil {
		t.Fatalf("apply schema.sql: %v", err)
	}
	if _, err := db.Exec("DROP TABLE session_alt_tabs"); err != nil {
		t.Fatalf("drop session_alt_tabs: %v", err)
	}
	if _, err := db.Exec("ALTER TABLE characters DROP COLUMN played_secs"); err != nil {
		t.Fatalf("drop played_secs: %v", err)
	}
	// schema.sql no longer defines chat_channels.name (dropped by the
	// fromVersion<7 migration step) — add it back here to reconstruct the
	// pre-migration shape a real old database would have.
	if _, err := db.Exec("ALTER TABLE chat_channels ADD COLUMN name TEXT"); err != nil {
		t.Fatalf("add chat_channels.name: %v", err)
	}
	if _, err := db.Exec("PRAGMA user_version = 4"); err != nil {
		t.Fatalf("set user_version: %v", err)
	}

	if err := EnsureSchema(db); err != nil {
		t.Fatalf("EnsureSchema: %v", err)
	}

	var version int
	db.QueryRow("PRAGMA user_version").Scan(&version)
	if version != kVersion {
		t.Errorf("user_version = %d, want %d", version, kVersion)
	}

	var name string
	if err := db.QueryRow("SELECT name FROM sqlite_master WHERE type='table' AND name='session_alt_tabs'").Scan(&name); err != nil {
		t.Errorf("session_alt_tabs not recreated by migration: %v", err)
	}

	if _, err := db.Exec("SELECT played_secs FROM characters"); err != nil {
		t.Errorf("played_secs column not restored by migration: %v", err)
	}

	if _, err := db.Exec("SELECT name FROM chat_channels"); err == nil {
		t.Error("expected chat_channels.name to be dropped by migration, but it still exists")
	}
}
