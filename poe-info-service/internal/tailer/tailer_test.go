package tailer

import (
	"context"
	"database/sql"
	"os"
	"path/filepath"
	"testing"
	"time"

	_ "modernc.org/sqlite"
)

func newTestDB(t *testing.T) (*sql.DB, int64) {
	t.Helper()
	db, err := sql.Open("sqlite", "file:"+t.Name()+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })

	if _, err := db.Exec(`CREATE TABLE installs (
		id INTEGER PRIMARY KEY,
		last_byte_offset INTEGER DEFAULT 0,
		file_created_at INTEGER DEFAULT 0,
		file_modified_at INTEGER DEFAULT 0,
		file_size INTEGER DEFAULT 0
	)`); err != nil {
		t.Fatalf("create table: %v", err)
	}
	res, err := db.Exec(`INSERT INTO installs (id) VALUES (1)`)
	if err != nil {
		t.Fatalf("insert install row: %v", err)
	}
	installID, _ := res.LastInsertId()
	return db, installID
}

func TestLastActivityZeroBeforeAnyPoll(t *testing.T) {
	db, installID := newTestDB(t)
	out := make(chan string, 8)
	tl := New(filepath.Join(t.TempDir(), "Client.txt"), db, installID, out)

	if got := tl.LastActivity(); !got.IsZero() {
		t.Fatalf("expected zero LastActivity before any poll, got %v", got)
	}
}

func TestLastActivityUpdatesWhenNewLinesAppear(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	if err := os.WriteFile(logPath, []byte("first line\n"), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	out := make(chan string, 8)
	tl := New(logPath, db, installID, out)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	select {
	case <-out:
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for tailer to read the seeded line")
	}

	deadline := time.Now().Add(time.Second)
	for time.Now().Before(deadline) {
		if !tl.LastActivity().IsZero() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("expected non-zero LastActivity after tailer read a new line")
}
