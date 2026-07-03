package channels

import (
	"database/sql"
	"strings"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/schema"
	_ "modernc.org/sqlite"
)

func newTestDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("EnsureSchema: %v", err)
	}
	return db
}

func countLabels(t *testing.T, db *sql.DB, channel int) int {
	t.Helper()
	var n int
	if err := db.QueryRow(
		`SELECT COUNT(*) FROM chat_channel_labels l JOIN chat_channels c ON c.id=l.channel_id WHERE c.number=?`,
		channel).Scan(&n); err != nil {
		t.Fatalf("count labels: %v", err)
	}
	return n
}

func TestRegisterDedupsExactTuple(t *testing.T) {
	db := newTestDB(t)
	if err := Register(db, 820, "Trade", "", ""); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if err := Register(db, 820, "Trade", "", ""); err != nil {
		t.Fatalf("Register (dup): %v", err)
	}
	if got := countLabels(t, db, 820); got != 1 {
		t.Errorf("expected 1 label after duplicate register, got %d", got)
	}
}

func TestRegisterDistinctDateRangesAreSeparateRows(t *testing.T) {
	db := newTestDB(t)
	if err := Register(db, 777, "My Special Event", "2026-07-01", "2026-08-01"); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if err := Register(db, 777, "My Special Event", "2026-09-01", "2026-10-01"); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if got := countLabels(t, db, 777); got != 2 {
		t.Errorf("expected 2 distinct labels for different date ranges, got %d", got)
	}
}

func TestRenameRetargetsLabelWithinSameDateRange(t *testing.T) {
	db := newTestDB(t)
	if err := Register(db, 820, "Trade", "", ""); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if err := Rename(db, 820, "", "", "Trade", "Global Trade"); err != nil {
		t.Fatalf("Rename: %v", err)
	}
	var label string
	if err := db.QueryRow(
		`SELECT label FROM chat_channel_labels l JOIN chat_channels c ON c.id=l.channel_id WHERE c.number=820`).
		Scan(&label); err != nil {
		t.Fatalf("query label: %v", err)
	}
	if label != "Global Trade" {
		t.Errorf("label = %q, want Global Trade", label)
	}
}

func TestRenameMissingRowErrors(t *testing.T) {
	db := newTestDB(t)
	if err := Rename(db, 820, "", "", "DoesNotExist", "Whatever"); err == nil {
		t.Error("expected error renaming a label that was never registered")
	}
}

func TestDeleteRemovesOnlyMatchingRow(t *testing.T) {
	db := newTestDB(t)
	if err := Register(db, 820, "Trade", "", ""); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if err := Register(db, 820, "Global", "", ""); err != nil {
		t.Fatalf("Register: %v", err)
	}
	if err := Delete(db, 820, "Trade", "", ""); err != nil {
		t.Fatalf("Delete: %v", err)
	}
	if got := countLabels(t, db, 820); got != 1 {
		t.Errorf("expected 1 label remaining after delete, got %d", got)
	}
}

func TestDeleteMissingRowIsNotAnError(t *testing.T) {
	db := newTestDB(t)
	if err := Delete(db, 820, "NeverRegistered", "", ""); err != nil {
		t.Errorf("Delete of a nonexistent label should be a no-op, got: %v", err)
	}
}
