package store

import (
	"database/sql"
	"fmt"
	"time"
)

// Store wraps poe-info-service's own internal bookkeeping tables (state,
// api_cache) on the shared database — poe-info-service.db is one database,
// not two (ADR-006), so this shares its *sql.DB with the query package
// rather than opening a separate file.
type Store struct {
	db *sql.DB
}

// New ensures the state/api_cache tables exist on db and returns a Store
// wrapping it. Lifecycle (opening and closing the connection) belongs to
// whoever passed db in, not to Store.
func New(db *sql.DB) (*Store, error) {
	s := &Store{db: db}
	if err := s.migrate(); err != nil {
		return nil, fmt.Errorf("migrate: %w", err)
	}
	return s, nil
}

func (s *Store) migrate() error {
	_, err := s.db.Exec(`
		CREATE TABLE IF NOT EXISTS state (
			key   TEXT PRIMARY KEY,
			value TEXT NOT NULL
		);

		CREATE TABLE IF NOT EXISTS api_cache (
			key        TEXT PRIMARY KEY,
			value      BLOB NOT NULL,
			fetched_at INTEGER NOT NULL,
			expires_at INTEGER NOT NULL
		);
	`)
	return err
}

func (s *Store) GetState(key string) (value string, ok bool, err error) {
	err = s.db.QueryRow(`SELECT value FROM state WHERE key = ?`, key).Scan(&value)
	if err == sql.ErrNoRows {
		return "", false, nil
	}
	return value, err == nil, err
}

func (s *Store) SetState(key, value string) error {
	_, err := s.db.Exec(`INSERT OR REPLACE INTO state (key, value) VALUES (?, ?)`, key, value)
	return err
}

// Checkpoint flushes the WAL to the main DB file. Call before transferring
// ownership to a new server instance.
func (s *Store) Checkpoint() {
	s.db.Exec(`PRAGMA wal_checkpoint(TRUNCATE)`)
}

// GetCache returns the cached value stored under key, and ok=false if no
// row exists or the row's TTL (set via SetCache) has already elapsed — an
// expired row is treated as a miss rather than being pruned by GetCache
// itself, since callers are expected to overwrite it via SetCache on their
// next successful fetch regardless.
func (s *Store) GetCache(key string) (value []byte, ok bool, err error) {
	var expiresAt int64
	err = s.db.QueryRow(`SELECT value, expires_at FROM api_cache WHERE key = ?`, key).Scan(&value, &expiresAt)
	if err == sql.ErrNoRows {
		return nil, false, nil
	}
	if err != nil {
		return nil, false, err
	}
	if time.Now().Unix() > expiresAt {
		return nil, false, nil
	}
	return value, true, nil
}

// SetCache stores value under key, valid for ttl from now.
func (s *Store) SetCache(key string, value []byte, ttl time.Duration) error {
	now := time.Now().Unix()
	_, err := s.db.Exec(
		`INSERT OR REPLACE INTO api_cache (key, value, fetched_at, expires_at) VALUES (?, ?, ?, ?)`,
		key, value, now, now+int64(ttl.Seconds()),
	)
	return err
}
