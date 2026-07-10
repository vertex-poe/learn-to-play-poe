package ingest

import "testing"

func TestCloseOrphanSessions(t *testing.T) {
	db := newTestDB(t)

	runningID, err := EnsureInstall(db, "/game1/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	stoppedID, err := EnsureInstall(db, "/game2/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}

	mustExec := func(q string, args ...any) {
		t.Helper()
		if _, err := db.Exec(q, args...); err != nil {
			t.Fatalf("exec %q: %v", q, err)
		}
	}
	mustExec(`INSERT INTO sessions(install_id, started_at) VALUES(?, '2024-01-15 10:00:00')`, runningID)
	mustExec(`INSERT INTO sessions(install_id, started_at) VALUES(?, '2024-01-15 09:00:00')`, stoppedID)
	mustExec(`INSERT INTO sessions(install_id, started_at, ended_at) VALUES(?, '2024-01-14 09:00:00', '2024-01-14 10:00:00')`, stoppedID)

	closed, err := CloseOrphanSessions(db, []string{"/game1/Client.txt"})
	if err != nil {
		t.Fatalf("CloseOrphanSessions: %v", err)
	}
	if closed != 1 {
		t.Fatalf("closed = %d, want 1", closed)
	}

	var runningEnded, stoppedEnded any
	if err := db.QueryRow(`SELECT ended_at FROM sessions WHERE install_id=? AND started_at='2024-01-15 10:00:00'`, runningID).Scan(&runningEnded); err != nil {
		t.Fatalf("query running session: %v", err)
	}
	if runningEnded != nil {
		t.Errorf("expected running install's open session to stay open, got ended_at=%v", runningEnded)
	}
	if err := db.QueryRow(`SELECT ended_at FROM sessions WHERE install_id=? AND started_at='2024-01-15 09:00:00'`, stoppedID).Scan(&stoppedEnded); err != nil {
		t.Fatalf("query stopped session: %v", err)
	}
	if stoppedEnded == nil {
		t.Errorf("expected stopped install's open session to be closed")
	}

	// Idempotent: running the close again finds nothing new to close.
	closed, err = CloseOrphanSessions(db, []string{"/game1/Client.txt"})
	if err != nil {
		t.Fatalf("CloseOrphanSessions (second call): %v", err)
	}
	if closed != 0 {
		t.Errorf("second call closed = %d, want 0", closed)
	}
}

// TestCloseOrphanSessions_NormalizesSlashDirection covers a still-running
// install not getting incorrectly closed as orphaned just because
// l2p-poe's WindowTracker (forward slashes, via Qt) spells its path
// differently than installs.path (backslashes, via EnsureInstall's
// filepath.Clean).
func TestCloseOrphanSessions_NormalizesSlashDirection(t *testing.T) {
	db := newTestDB(t)

	runningID, err := EnsureInstall(db, `F:\SteamLibrary\steamapps\common\Path of Exile`)
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	if _, err := db.Exec(`INSERT INTO sessions(install_id, started_at) VALUES(?, '2024-01-15 10:00:00')`, runningID); err != nil {
		t.Fatalf("insert session: %v", err)
	}

	closed, err := CloseOrphanSessions(db, []string{`F:/SteamLibrary/steamapps/common/Path of Exile`})
	if err != nil {
		t.Fatalf("CloseOrphanSessions: %v", err)
	}
	if closed != 0 {
		t.Errorf("closed = %d, want 0 — the running install's session should not have been closed", closed)
	}
}

// TestCloseOrphanSessions_UsesLastKnownActivity is a regression test: closing
// an orphaned session used to stamp ended_at as "now" (whenever the orphan
// sweep happened to run) and leave total_secs/active_secs NULL forever. It
// should instead use the timestamp of the last zone the player was actually
// seen in, and fill in total_secs/active_secs/afk_secs like any other closed
// session, closing the dangling-open span and AFK interval at that same
// moment so their durations aren't left open indefinitely either.
func TestCloseOrphanSessions_UsesLastKnownActivity(t *testing.T) {
	db := newTestDB(t)

	installID, err := EnsureInstall(db, "/game/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	mustExec := func(q string, args ...any) {
		t.Helper()
		if _, err := db.Exec(q, args...); err != nil {
			t.Fatalf("exec %q: %v", q, err)
		}
	}
	mustExec(`INSERT INTO sessions(install_id, started_at) VALUES(?, '2024-01-15 10:00:00')`, installID)
	var sessionID int64
	if err := db.QueryRow(`SELECT id FROM sessions WHERE install_id=?`, installID).Scan(&sessionID); err != nil {
		t.Fatalf("query session id: %v", err)
	}

	// A closed span (10:00-10:20), then the still-open span the player was
	// in when the game vanished (10:20 onward, never closed) — and an AFK
	// interval left open inside that final span.
	mustExec(`INSERT INTO area_time_spans(session_id, entered_at, exited_at, duration_secs) VALUES(?,?,?,?)`,
		sessionID, "2024-01-15 10:00:00", "2024-01-15 10:20:00", 1200)
	res, err := db.Exec(`INSERT INTO area_time_spans(session_id, entered_at) VALUES(?,?)`,
		sessionID, "2024-01-15 10:20:00")
	if err != nil {
		t.Fatalf("insert open span: %v", err)
	}
	openSpanID, _ := res.LastInsertId()
	mustExec(`INSERT INTO session_afk(session_id, span_id, afk_on_at) VALUES(?,?,?)`,
		sessionID, openSpanID, "2024-01-15 10:25:00")
	// Last known activity: the player entered a third zone at 10:35 (still open).
	mustExec(`INSERT INTO area_time_spans(session_id, entered_at) VALUES(?,?)`,
		sessionID, "2024-01-15 10:35:00")

	closed, err := CloseOrphanSessions(db, nil)
	if err != nil {
		t.Fatalf("CloseOrphanSessions: %v", err)
	}
	if closed != 1 {
		t.Fatalf("closed = %d, want 1", closed)
	}

	var endedAt string
	var totalSecs, activeSecs, afkSecs int64
	if err := db.QueryRow(
		`SELECT ended_at, total_secs, active_secs, afk_secs FROM sessions WHERE id=?`, sessionID,
	).Scan(&endedAt, &totalSecs, &activeSecs, &afkSecs); err != nil {
		t.Fatalf("query session: %v", err)
	}
	// Last known activity is the 10:35 zone entry, not "now".
	if endedAt != "2024-01-15 10:35:00" {
		t.Errorf("ended_at = %q, want %q (last known activity, not the current time)", endedAt, "2024-01-15 10:35:00")
	}
	if totalSecs != 2100 { // 10:00 -> 10:35 = 35 minutes
		t.Errorf("total_secs = %d, want 2100", totalSecs)
	}
	if afkSecs != 600 { // 10:25 -> 10:35 (the open AFK interval, closed at endTs)
		t.Errorf("afk_secs = %d, want 600", afkSecs)
	}
	if activeSecs != totalSecs-afkSecs {
		t.Errorf("active_secs = %d, want total_secs - afk_secs = %d", activeSecs, totalSecs-afkSecs)
	}

	// The two previously-open rows must have been closed, not left dangling.
	var midSpanExitedAt, midSpanDuration any
	if err := db.QueryRow(`SELECT exited_at, duration_secs FROM area_time_spans WHERE id=?`, openSpanID).
		Scan(&midSpanExitedAt, &midSpanDuration); err != nil {
		t.Fatalf("query mid span: %v", err)
	}
	if midSpanExitedAt != "2024-01-15 10:35:00" {
		t.Errorf("mid span exited_at = %v, want closed at last known activity", midSpanExitedAt)
	}
	if midSpanDuration != int64(900) { // 10:20 -> 10:35
		t.Errorf("mid span duration_secs = %v, want 900", midSpanDuration)
	}

	var afkOffAt any
	if err := db.QueryRow(`SELECT afk_off_at FROM session_afk WHERE session_id=?`, sessionID).Scan(&afkOffAt); err != nil {
		t.Fatalf("query session_afk: %v", err)
	}
	if afkOffAt != "2024-01-15 10:35:00" {
		t.Errorf("afk_off_at = %v, want closed at last known activity", afkOffAt)
	}
}

// TestCloseOrphanSessions_NoSpans_FallsBackToStartedAt covers the edge case
// of a session that never got past character select (no area_time_spans
// rows at all) — there's no "last known activity" to derive from, so it
// should fall back to started_at rather than erroring or leaving ended_at
// NULL.
func TestCloseOrphanSessions_NoSpans_FallsBackToStartedAt(t *testing.T) {
	db := newTestDB(t)

	installID, err := EnsureInstall(db, "/game/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	if _, err := db.Exec(`INSERT INTO sessions(install_id, started_at) VALUES(?, '2024-01-15 10:00:00')`, installID); err != nil {
		t.Fatalf("insert session: %v", err)
	}
	var sessionID int64
	if err := db.QueryRow(`SELECT id FROM sessions WHERE install_id=?`, installID).Scan(&sessionID); err != nil {
		t.Fatalf("query session id: %v", err)
	}

	closed, err := CloseOrphanSessions(db, nil)
	if err != nil {
		t.Fatalf("CloseOrphanSessions: %v", err)
	}
	if closed != 1 {
		t.Fatalf("closed = %d, want 1", closed)
	}

	var endedAt string
	var totalSecs, activeSecs int64
	if err := db.QueryRow(`SELECT ended_at, total_secs, active_secs FROM sessions WHERE id=?`, sessionID).
		Scan(&endedAt, &totalSecs, &activeSecs); err != nil {
		t.Fatalf("query session: %v", err)
	}
	if endedAt != "2024-01-15 10:00:00" {
		t.Errorf("ended_at = %q, want started_at as fallback", endedAt)
	}
	if totalSecs != 0 {
		t.Errorf("total_secs = %d, want 0", totalSecs)
	}
	if activeSecs != 0 {
		t.Errorf("active_secs = %d, want 0", activeSecs)
	}
}
