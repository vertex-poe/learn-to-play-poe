package store

import (
	"database/sql"
	"strings"
	"testing"
	"time"

	_ "modernc.org/sqlite"
)

func openMemDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	return db
}

func newTestStore(t *testing.T) *Store {
	t.Helper()
	s, err := New(openMemDB(t))
	if err != nil {
		t.Fatalf("New: %v", err)
	}
	return s
}

func TestCacheMissingKey(t *testing.T) {
	s := newTestStore(t)
	_, ok, err := s.GetCache("nope")
	if err != nil {
		t.Fatalf("GetCache: unexpected error: %v", err)
	}
	if ok {
		t.Error("GetCache of a missing key: want ok=false")
	}
}

func TestCacheRoundTrips(t *testing.T) {
	s := newTestStore(t)
	want := []byte(`{"hello":"world"}`)

	if err := s.SetCache("k", want, time.Hour); err != nil {
		t.Fatalf("SetCache: %v", err)
	}

	got, ok, err := s.GetCache("k")
	if err != nil {
		t.Fatalf("GetCache: unexpected error: %v", err)
	}
	if !ok {
		t.Fatal("GetCache after SetCache: want ok=true")
	}
	if string(got) != string(want) {
		t.Errorf("GetCache = %q, want %q", got, want)
	}
}

func TestCacheExpiredEntryIsAMiss(t *testing.T) {
	s := newTestStore(t)
	if err := s.SetCache("k", []byte("v"), -time.Second); err != nil {
		t.Fatalf("SetCache: %v", err)
	}

	_, ok, err := s.GetCache("k")
	if err != nil {
		t.Fatalf("GetCache: unexpected error: %v", err)
	}
	if ok {
		t.Error("GetCache of an expired entry: want ok=false")
	}
}

func TestCacheSetOverwritesExistingKey(t *testing.T) {
	s := newTestStore(t)
	if err := s.SetCache("k", []byte("first"), time.Hour); err != nil {
		t.Fatalf("SetCache: %v", err)
	}
	if err := s.SetCache("k", []byte("second"), time.Hour); err != nil {
		t.Fatalf("SetCache: %v", err)
	}

	got, ok, err := s.GetCache("k")
	if err != nil || !ok {
		t.Fatalf("GetCache: err=%v ok=%v", err, ok)
	}
	if string(got) != "second" {
		t.Errorf("GetCache after overwrite = %q, want %q", got, "second")
	}
}
