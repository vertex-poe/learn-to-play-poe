//go:build windows

package creds

import "testing"

// testService keeps this test's writes/deletes out of the real
// poe-info-service/poesessid credential entry, in case a developer's machine
// has a real session stored while `go test` runs.
const testService = "poe-info-service-test"

func TestStoreGetDelete(t *testing.T) {
	const key = "creds_test_roundtrip"
	t.Cleanup(func() { Delete(testService, key) })

	if _, err := Get(testService, key); err != ErrNotFound {
		t.Fatalf("Get before Store: got err %v, want ErrNotFound", err)
	}

	if err := Store(testService, key, "s3cr3t"); err != nil {
		t.Fatalf("Store: %v", err)
	}

	got, err := Get(testService, key)
	if err != nil {
		t.Fatalf("Get after Store: %v", err)
	}
	if got != "s3cr3t" {
		t.Fatalf("Get after Store: got %q, want %q", got, "s3cr3t")
	}

	if err := Store(testService, key, "updated"); err != nil {
		t.Fatalf("Store (overwrite): %v", err)
	}
	if got, err = Get(testService, key); err != nil || got != "updated" {
		t.Fatalf("Get after overwrite: got (%q, %v), want (%q, nil)", got, err, "updated")
	}

	if err := Delete(testService, key); err != nil {
		t.Fatalf("Delete: %v", err)
	}
	if _, err := Get(testService, key); err != ErrNotFound {
		t.Fatalf("Get after Delete: got err %v, want ErrNotFound", err)
	}

	if err := Delete(testService, key); err != nil {
		t.Fatalf("Delete (already gone): %v", err)
	}
}
