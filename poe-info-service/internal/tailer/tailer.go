package tailer

import (
	"bufio"
	"context"
	"database/sql"
	"io"
	"log"
	"os"
	"strings"
	"sync/atomic"
	"time"
)

const pollInterval = 250 * time.Millisecond

// Tailer polls Client.txt for new lines, forwarding each complete line to
// out. Resume position and file bookkeeping (created/modified/size) live in
// the l2p database's installs row for this install, mirroring the columns
// the old C++ LogIngestWorker maintained.
type Tailer struct {
	logPath      string
	db           *sql.DB
	installID    int64
	out          chan<- string
	caughtUp     atomic.Bool
	lastActivity atomic.Int64 // UnixNano of the last poll that read new lines
	offset       atomic.Int64 // bytes consumed so far
	fileSize     atomic.Int64 // file size as of the most recent successful poll
}

func New(logPath string, db *sql.DB, installID int64, out chan<- string) *Tailer {
	return &Tailer{logPath: logPath, db: db, installID: installID, out: out}
}

// CaughtUp reports whether the tailer has drained the file to EOF at least
// once since this process started. Callers use this to avoid broadcasting
// backlog events (replayed after a restart) as if they were live.
func (t *Tailer) CaughtUp() bool { return t.caughtUp.Load() }

// LastActivity returns the time new lines were last read from the log file,
// or the zero Time if none have been read yet this run. Per ADR-001, this
// stands in for "the game itself is open" as an implicit keep-alive even
// when no addon client is connected.
func (t *Tailer) LastActivity() time.Time {
	ns := t.lastActivity.Load()
	if ns == 0 {
		return time.Time{}
	}
	return time.Unix(0, ns)
}

// Progress returns bytes consumed and the log file's size as of the most
// recent poll, for computing backlog-replay percentage. Both are zero until
// the first poll succeeds.
func (t *Tailer) Progress() (offset, size int64) {
	return t.offset.Load(), t.fileSize.Load()
}

func (t *Tailer) Run(ctx context.Context) {
	offset := t.loadOffset()
	log.Printf("tailer: starting at offset %d for %s", offset, t.logPath)

	ticker := time.NewTicker(pollInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			newOffset, err := t.poll(ctx, offset)
			if err != nil {
				if !os.IsNotExist(err) {
					log.Printf("tailer: poll error: %v", err)
				}
				continue
			}
			t.offset.Store(newOffset)
			if newOffset != offset {
				offset = newOffset
				t.saveOffset(offset)
				t.lastActivity.Store(time.Now().UnixNano())
			} else if !t.caughtUp.Load() {
				t.caughtUp.Store(true)
				log.Printf("tailer: caught up to EOF at offset %d for %s", offset, t.logPath)
			}
		}
	}
}

// poll reads any new complete lines from the log file starting at offset and
// returns the absolute offset after the read (unchanged if nothing new was
// available). If the file has shrunk (truncated or rotated), it resets to 0.
func (t *Tailer) poll(ctx context.Context, offset int64) (int64, error) {
	f, err := os.Open(t.logPath)
	if err != nil {
		return offset, err
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return offset, err
	}
	t.fileSize.Store(fi.Size())

	if fi.Size() < offset {
		log.Printf("tailer: log file shrank (was %d, now %d), resetting", offset, fi.Size())
		offset = 0
	}
	if fi.Size() == offset {
		return offset, nil
	}

	if _, err := f.Seek(offset, io.SeekStart); err != nil {
		return offset, err
	}

	reader := bufio.NewReader(f)
	consumed := offset

	for {
		line, err := reader.ReadString('\n')
		if err == io.EOF {
			// Incomplete line — don't advance past it; wait for the next poll.
			break
		}
		if err != nil {
			return consumed, err
		}

		consumed += int64(len(line))

		trimmed := strings.TrimRight(line, "\r\n")
		if trimmed == "" {
			continue
		}

		select {
		case t.out <- trimmed:
		case <-ctx.Done():
			return consumed, nil
		}
	}

	return consumed, nil
}

func (t *Tailer) loadOffset() int64 {
	var offset int64
	if err := t.db.QueryRow(
		"SELECT last_byte_offset FROM installs WHERE id=?", t.installID,
	).Scan(&offset); err != nil {
		log.Printf("tailer: failed to load offset: %v", err)
	}
	return offset
}

func (t *Tailer) saveOffset(offset int64) {
	var createdAt, modifiedAt, size int64
	if fi, err := os.Stat(t.logPath); err == nil {
		createdAt = fileCreatedAt(fi)
		modifiedAt = fi.ModTime().Unix()
		size = fi.Size()
	}
	if _, err := t.db.Exec(
		`UPDATE installs SET file_created_at=?, file_modified_at=?, file_size=?, last_byte_offset=? WHERE id=?`,
		createdAt, modifiedAt, size, offset, t.installID,
	); err != nil {
		log.Printf("tailer: failed to save offset: %v", err)
	}
}
