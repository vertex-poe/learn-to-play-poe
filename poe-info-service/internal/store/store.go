package store

import (
	"database/sql"
	"fmt"
	"os"
	"path/filepath"

	_ "modernc.org/sqlite"
)

type Store struct {
	db *sql.DB
}

func Open(dir string) (*Store, error) {
	if err := os.MkdirAll(dir, 0755); err != nil {
		return nil, fmt.Errorf("create cache dir: %w", err)
	}

	path := filepath.Join(dir, "poe-info-service.db")
	dsn := path + "?_journal_mode=WAL&_synchronous=NORMAL&_busy_timeout=5000"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, fmt.Errorf("open db: %w", err)
	}
	db.SetMaxOpenConns(1) // single writer

	s := &Store{db: db}
	if err := s.migrate(); err != nil {
		db.Close()
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

func (s *Store) Close() error {
	s.Checkpoint()
	return s.db.Close()
}
