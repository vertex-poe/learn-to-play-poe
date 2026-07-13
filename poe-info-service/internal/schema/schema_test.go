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

	for _, table := range []string{"installs", "sessions", "areas", "chats", "whispers", "session_afk", "passive_point_snapshots", "leagues"} {
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
	// Reconstruct the pre-v8 shape of session_afk (no span_id, no kind, and
	// the narrower UNIQUE) *before* applying schema.sql, so schema.sql's
	// `CREATE TABLE IF NOT EXISTS session_afk` below is a no-op and this old
	// shape survives to be migrated. A genuine v4 database also predates
	// session_alt_tabs (added by the fromVersion<5 step), so it's
	// deliberately not created here either.
	if _, err := db.Exec(`CREATE TABLE session_afk (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		session_id INTEGER NOT NULL REFERENCES sessions(id),
		afk_on_at  TEXT    NOT NULL,
		afk_off_at TEXT,
		UNIQUE(session_id, afk_on_at)
	)`); err != nil {
		t.Fatalf("create pre-v8 session_afk: %v", err)
	}
	if _, err := db.Exec(schemaSQL); err != nil {
		t.Fatalf("apply schema.sql: %v", err)
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
	// accounts.poe_uuid/oauth_credential_key/oauth_authenticated_at are added
	// by the fromVersion<10 migration step — drop them so this reconstructed
	// "old" database doesn't already have what that step is meant to add.
	for _, col := range []string{"poe_uuid", "oauth_credential_key", "oauth_authenticated_at"} {
		if _, err := db.Exec("ALTER TABLE accounts DROP COLUMN " + col); err != nil {
			t.Fatalf("drop accounts.%s: %v", col, err)
		}
	}
	// leagues is added by the fromVersion<11 migration step — drop it so this
	// reconstructed "old" database doesn't already have what that step adds.
	if _, err := db.Exec("DROP TABLE leagues"); err != nil {
		t.Fatalf("drop leagues: %v", err)
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
	if err := db.QueryRow("SELECT name FROM sqlite_master WHERE type='table' AND name='session_alt_tabs'").Scan(&name); err == nil {
		t.Error("expected session_alt_tabs to be dropped (merged into session_afk) by the v9 migration, but it still exists")
	}

	if _, err := db.Exec("SELECT played_secs FROM characters"); err != nil {
		t.Errorf("played_secs column not restored by migration: %v", err)
	}

	if _, err := db.Exec("SELECT name FROM chat_channels"); err == nil {
		t.Error("expected chat_channels.name to be dropped by migration, but it still exists")
	}

	if _, err := db.Exec("SELECT span_id, kind FROM session_afk"); err != nil {
		t.Errorf("span_id/kind columns not added by migration: %v", err)
	}

	if _, err := db.Exec("SELECT poe_uuid, oauth_credential_key, oauth_authenticated_at FROM accounts"); err != nil {
		t.Errorf("accounts OAuth columns not restored by migration: %v", err)
	}

	if _, err := db.Exec("SELECT name, realm, rules_json, is_event, is_delve_event, fetched_at FROM leagues"); err != nil {
		t.Errorf("leagues table not restored by migration: %v", err)
	}
}

// TestMigrateToV9_MergesAltTabIntoSessionAfk covers the actual data movement
// the v9 migration performs on a real (pre-unification) v8 database: rows in
// both session_afk and session_alt_tabs must survive into the unified
// session_afk table, tagged with the right kind and without cross-talk.
func TestMigrateToV9_MergesAltTabIntoSessionAfk(t *testing.T) {
	db := openMemDB(t)
	if _, err := db.Exec(`CREATE TABLE session_afk (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		session_id INTEGER NOT NULL,
		span_id    INTEGER,
		afk_on_at  TEXT    NOT NULL,
		afk_off_at TEXT,
		UNIQUE(session_id, afk_on_at)
	)`); err != nil {
		t.Fatalf("create v8 session_afk: %v", err)
	}
	if _, err := db.Exec(`CREATE TABLE session_alt_tabs (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		session_id INTEGER NOT NULL,
		out_at     TEXT    NOT NULL,
		in_at      TEXT,
		UNIQUE(session_id, out_at)
	)`); err != nil {
		t.Fatalf("create v8 session_alt_tabs: %v", err)
	}
	// migrate() also runs the fromVersion<10 step against this same db, which
	// ALTERs accounts — give it the pre-v10 shape so that step has a table to
	// work with (schemaSQL isn't applied in this test, unlike real EnsureSchema
	// use, which always creates/upgrades accounts before migrate() runs).
	if _, err := db.Exec(`CREATE TABLE accounts (
		id         INTEGER PRIMARY KEY AUTOINCREMENT,
		name       TEXT    NOT NULL UNIQUE,
		guild_name TEXT
	)`); err != nil {
		t.Fatalf("create v8 accounts: %v", err)
	}
	if _, err := db.Exec(`INSERT INTO session_afk(session_id, span_id, afk_on_at, afk_off_at) VALUES(1, 10, '2024-01-15 10:01:00', '2024-01-15 10:03:00')`); err != nil {
		t.Fatalf("seed session_afk: %v", err)
	}
	if _, err := db.Exec(`INSERT INTO session_alt_tabs(session_id, out_at, in_at) VALUES(1, '2024-01-15 10:05:00', '2024-01-15 10:06:00')`); err != nil {
		t.Fatalf("seed session_alt_tabs: %v", err)
	}
	if _, err := db.Exec("PRAGMA user_version = 8"); err != nil {
		t.Fatalf("set user_version: %v", err)
	}

	if err := migrate(db, 8); err != nil {
		t.Fatalf("migrate: %v", err)
	}

	rows, err := db.Query(`SELECT kind, afk_on_at, afk_off_at, span_id FROM session_afk ORDER BY afk_on_at`)
	if err != nil {
		t.Fatalf("query merged session_afk: %v", err)
	}
	defer rows.Close()

	type row struct {
		kind, onAt, offAt string
		spanID            sql.NullInt64
	}
	var got []row
	for rows.Next() {
		var r row
		if err := rows.Scan(&r.kind, &r.onAt, &r.offAt, &r.spanID); err != nil {
			t.Fatalf("scan: %v", err)
		}
		got = append(got, r)
	}

	if len(got) != 2 {
		t.Fatalf("expected 2 merged rows, got %d", len(got))
	}
	if got[0].kind != "afk" || got[0].onAt != "2024-01-15 10:01:00" || got[0].offAt != "2024-01-15 10:03:00" || !got[0].spanID.Valid || got[0].spanID.Int64 != 10 {
		t.Errorf("afk row wrong after merge: %+v", got[0])
	}
	if got[1].kind != "alt_tab" || got[1].onAt != "2024-01-15 10:05:00" || got[1].offAt != "2024-01-15 10:06:00" || got[1].spanID.Valid {
		t.Errorf("alt_tab row wrong after merge: %+v", got[1])
	}

	if err := db.QueryRow(`SELECT name FROM sqlite_master WHERE type='table' AND name='session_alt_tabs'`).Scan(new(string)); err == nil {
		t.Error("expected session_alt_tabs to be dropped after merge")
	}
}
