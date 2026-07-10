package ingest

import (
	"database/sql"
	"path/filepath"
)

// NormalizeInstallPath canonicalizes an install directory (or any path
// derived from one) to forward-slash form, regardless of which separator it
// arrived with. The same physical directory can arrive spelled two
// different ways — e.g. a forward-slash path from config/l2p-poe's Settings
// UI (Qt paths are always "/"-separated) vs. a backslash path from Windows
// process/auto-detection APIs — and comparing those unequal produces two
// installs rows for one directory, tailing the same Client.txt twice and
// doubling every session. Forward slash is the canonical form (not the
// platform-native separator) because: it's what Qt already produces
// throughout l2p-poe without any conversion; Windows file APIs accept it
// transparently; and it needs no escaping in TOML/JSON, unlike backslash.
// filepath.Clean runs first so mixed/redundant separators collapse before
// the final ToSlash conversion.
func NormalizeInstallPath(path string) string {
	return filepath.ToSlash(filepath.Clean(path))
}

// EnsureInstall inserts (if needed) and returns the id of the installs row
// for path, mirroring Database::upsertInstall from the old C++ side. Callers
// use the returned id to track tailer offset and attribute writes to this
// install. path is normalized via NormalizeInstallPath before use — see its
// doc comment.
func EnsureInstall(db *sql.DB, path string) (int64, error) {
	path = NormalizeInstallPath(path)
	if _, err := db.Exec("INSERT OR IGNORE INTO installs(path) VALUES(?)", path); err != nil {
		return 0, err
	}
	var id int64
	if err := db.QueryRow("SELECT id FROM installs WHERE path=?", path).Scan(&id); err != nil {
		return 0, err
	}
	return id, nil
}
