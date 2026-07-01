package ingest

import "database/sql"

// CloseOrphanSessions closes any session whose install is not among
// runningInstallPaths, mirroring Database::closeOrphanSessions from the old
// C++ side. Only ended_at is set — total/active seconds are left NULL since
// the precise stop time is unknown. Returns the number of sessions closed.
func CloseOrphanSessions(db *sql.DB, runningInstallPaths []string) (int, error) {
	running := make(map[string]bool, len(runningInstallPaths))
	for _, p := range runningInstallPaths {
		running[p] = true
	}

	rows, err := db.Query(`
		SELECT s.id, i.path
		FROM sessions s
		JOIN installs i ON s.install_id = i.id
		WHERE s.ended_at IS NULL`)
	if err != nil {
		return 0, err
	}
	var staleIDs []int64
	for rows.Next() {
		var id int64
		var path string
		if err := rows.Scan(&id, &path); err != nil {
			rows.Close()
			return 0, err
		}
		if !running[path] {
			staleIDs = append(staleIDs, id)
		}
	}
	if err := rows.Err(); err != nil {
		return 0, err
	}
	rows.Close()

	closed := 0
	for _, id := range staleIDs {
		res, err := db.Exec(
			`UPDATE sessions SET ended_at=datetime('now','localtime') WHERE id=? AND ended_at IS NULL`, id)
		if err != nil {
			return closed, err
		}
		n, _ := res.RowsAffected()
		closed += int(n)
	}
	return closed, nil
}
