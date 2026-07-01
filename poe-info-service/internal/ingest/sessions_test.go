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
