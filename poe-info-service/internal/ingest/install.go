package ingest

import "database/sql"

// EnsureInstall inserts (if needed) and returns the id of the installs row
// for path, mirroring Database::upsertInstall from the old C++ side. Callers
// use the returned id to track tailer offset and attribute writes to this
// install.
func EnsureInstall(db *sql.DB, path string) (int64, error) {
	if _, err := db.Exec("INSERT OR IGNORE INTO installs(path) VALUES(?)", path); err != nil {
		return 0, err
	}
	var id int64
	if err := db.QueryRow("SELECT id FROM installs WHERE path=?", path).Scan(&id); err != nil {
		return 0, err
	}
	return id, nil
}
