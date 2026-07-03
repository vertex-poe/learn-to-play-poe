package schema

import (
	"database/sql"
	"fmt"
)

// kVersion mirrors the C++ Database::kDbVersion. Bump it, and add a branch to
// migrate(), whenever schema.sql changes in a way existing databases need to
// catch up on.
const kVersion = 7

// EnsureSchema creates the schema on a fresh database (and seeds it with
// reference data) or migrates an existing one up to kVersion. It is
// idempotent and safe to call every time the service starts, mirroring
// Database::initSchema/migrate from the old C++ implementation.
func EnsureSchema(db *sql.DB) error {
	if _, err := db.Exec(schemaSQL); err != nil {
		return err
	}

	version, err := userVersion(db)
	if err != nil {
		return err
	}

	if version == 0 {
		seed, err := combinedSeedSQL()
		if err != nil {
			return err
		}
		if _, err := db.Exec(seed); err != nil {
			return err
		}
		return setUserVersion(db, kVersion)
	}

	if version < kVersion {
		return migrate(db, version)
	}

	return nil
}

func migrate(db *sql.DB, fromVersion int) error {
	if fromVersion < 5 {
		if _, err := db.Exec(`CREATE TABLE IF NOT EXISTS session_alt_tabs (
			id         INTEGER PRIMARY KEY AUTOINCREMENT,
			session_id INTEGER NOT NULL REFERENCES sessions(id),
			out_at     TEXT    NOT NULL,
			in_at      TEXT,
			UNIQUE(session_id, out_at)
		);`); err != nil {
			return err
		}
		if err := setUserVersion(db, 5); err != nil {
			return err
		}
		fromVersion = 5
	}

	if fromVersion < 6 {
		// characters.played_secs was always referenced by the ingest writer
		// (character total play time) but never existed in schema.sql —
		// added here so the column exists for both fresh and upgraded DBs.
		if _, err := db.Exec(`ALTER TABLE characters ADD COLUMN played_secs INTEGER NOT NULL DEFAULT 0`); err != nil {
			return err
		}
		if err := setUserVersion(db, 6); err != nil {
			return err
		}
		fromVersion = 6
	}

	if fromVersion < 7 {
		// chat_channels.name was a single always-on label per channel, written
		// by the ingest writer from l2p-poe.toml's [chat_channel_names]. It's
		// superseded by chat_channel_labels (multiple time-scoped labels,
		// managed over the channels.register/.rename/.delete WS API) and
		// nothing writes or reads it anymore.
		if _, err := db.Exec(`ALTER TABLE chat_channels DROP COLUMN name`); err != nil {
			return err
		}
		if err := setUserVersion(db, 7); err != nil {
			return err
		}
		fromVersion = 7
	}

	if fromVersion < kVersion {
		// Stale schema version with no migration step defined for it — bump to
		// current anyway rather than looping forever; mirrors the C++ warning.
		return setUserVersion(db, kVersion)
	}

	return nil
}

func userVersion(db *sql.DB) (int, error) {
	var v int
	if err := db.QueryRow("PRAGMA user_version").Scan(&v); err != nil {
		return 0, err
	}
	return v, nil
}

func setUserVersion(db *sql.DB, v int) error {
	_, err := db.Exec(fmt.Sprintf("PRAGMA user_version = %d", v))
	return err
}
