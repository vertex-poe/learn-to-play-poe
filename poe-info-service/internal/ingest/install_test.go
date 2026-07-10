package ingest

import "testing"

// TestEnsureInstall_DedupesRegardlessOfSlashDirection is a regression test
// for a real production bug: the same physical install directory arrived
// spelled two different ways (forward slashes from config/l2p-poe's Settings
// UI, backslashes from Windows process/auto-detection APIs), and — before
// EnsureInstall normalized its path argument — compared unequal against
// installs.path, producing two installs rows for one physical directory and
// tailing the same Client.txt twice (doubling every session).
func TestEnsureInstall_DedupesRegardlessOfSlashDirection(t *testing.T) {
	db := newTestDB(t)

	id1, err := EnsureInstall(db, `F:\SteamLibrary\steamapps\common\Path of Exile`)
	if err != nil {
		t.Fatalf("EnsureInstall (backslash): %v", err)
	}
	id2, err := EnsureInstall(db, `F:/SteamLibrary/steamapps/common/Path of Exile`)
	if err != nil {
		t.Fatalf("EnsureInstall (forward slash): %v", err)
	}
	if id1 != id2 {
		t.Errorf("EnsureInstall returned different ids (%d, %d) for the same directory spelled differently", id1, id2)
	}

	var count int
	if err := db.QueryRow(`SELECT COUNT(*) FROM installs`).Scan(&count); err != nil {
		t.Fatalf("count installs: %v", err)
	}
	if count != 1 {
		t.Errorf("installs row count = %d, want 1", count)
	}
}
