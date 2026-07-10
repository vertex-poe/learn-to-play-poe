package ingest

import "database/sql"

// CloseOrphanSessions closes any session whose install is not among
// runningInstallPaths, mirroring Database::closeOrphanSessions from the old
// C++ side. There's no clean "session stop" log event for these — the game
// vanished mid-session (crashed, force-killed, or the machine went down) —
// so ended_at/total_secs/active_secs are derived from the last activity
// actually recorded for the session (see closeOrphanSession) rather than the
// moment this sweep happens to run, which could be arbitrarily later than
// when the player actually stopped. Returns the number of sessions closed.
//
// Both runningInstallPaths (from l2p-poe's WindowTracker) and installs.path
// (normalized by EnsureInstall) are run through NormalizeInstallPath before
// comparing, so a still-running install isn't incorrectly closed as
// orphaned just because the two sides spell its path differently.
func CloseOrphanSessions(db *sql.DB, runningInstallPaths []string) (int, error) {
	running := make(map[string]bool, len(runningInstallPaths))
	for _, p := range runningInstallPaths {
		running[NormalizeInstallPath(p)] = true
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
		if !running[NormalizeInstallPath(path)] {
			staleIDs = append(staleIDs, id)
		}
	}
	if err := rows.Err(); err != nil {
		return 0, err
	}
	rows.Close()

	closed := 0
	for _, id := range staleIDs {
		if err := closeOrphanSession(db, id); err != nil {
			return closed, err
		}
		closed++
	}
	return closed, nil
}

// closeOrphanSession closes one orphaned session at the timestamp of the
// last activity recorded for it — the latest area_time_spans row (entered_at,
// or exited_at if that span was itself already closed) — falling back to the
// session's own started_at if it has no spans at all (e.g. it never got past
// character select). Any zone span or AFK/alt-tab interval this session left
// open is closed at that same timestamp first, mirroring what a clean
// Writer.closeSession would have done, so neither is left dangling open
// forever and total_secs/active_secs/afk_secs come out non-NULL like any
// other closed session's.
func closeOrphanSession(db *sql.DB, sessionID int64) error {
	var startedAt string
	if err := db.QueryRow(`SELECT started_at FROM sessions WHERE id=?`, sessionID).Scan(&startedAt); err != nil {
		return err
	}

	var lastKnownAt sql.NullString
	if err := db.QueryRow(
		`SELECT MAX(COALESCE(exited_at, entered_at)) FROM area_time_spans WHERE session_id=?`,
		sessionID).Scan(&lastKnownAt); err != nil {
		return err
	}
	endTs := startedAt
	if lastKnownAt.Valid && lastKnownAt.String > startedAt {
		endTs = lastKnownAt.String
	}

	var openSpanID sql.NullInt64
	var openSpanEnteredAt string
	err := db.QueryRow(
		`SELECT id, entered_at FROM area_time_spans WHERE session_id=? AND exited_at IS NULL`,
		sessionID).Scan(&openSpanID, &openSpanEnteredAt)
	if err != nil && err != sql.ErrNoRows {
		return err
	}
	if openSpanID.Valid {
		duration := max0(tsToSecs(endTs) - tsToSecs(openSpanEnteredAt))
		if _, err := db.Exec(
			`UPDATE area_time_spans SET exited_at=?, duration_secs=? WHERE id=?`,
			endTs, duration, openSpanID.Int64); err != nil {
			return err
		}
	}

	if _, err := db.Exec(
		`UPDATE session_afk SET afk_off_at=? WHERE session_id=? AND afk_off_at IS NULL`,
		endTs, sessionID); err != nil {
		return err
	}

	var afkSecs int64
	if err := db.QueryRow(`
		SELECT COALESCE(SUM(CAST(strftime('%s',afk_off_at) AS INTEGER) - CAST(strftime('%s',afk_on_at) AS INTEGER)),0)
		FROM session_afk WHERE session_id=?`, sessionID).Scan(&afkSecs); err != nil {
		return err
	}

	totalSecs := max0(tsToSecs(endTs) - tsToSecs(startedAt))
	activeSecs := max0(totalSecs - afkSecs)

	_, err = db.Exec(
		`UPDATE sessions SET ended_at=?, total_secs=?, afk_secs=?, active_secs=? WHERE id=? AND ended_at IS NULL`,
		endTs, totalSecs, afkSecs, activeSecs, sessionID)
	return err
}
