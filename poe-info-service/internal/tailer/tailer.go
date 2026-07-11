package tailer

import (
	"bytes"
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

// pollChunkBytes bounds how many bytes a single poll() reads. Requesting
// this many bytes and getting back fewer is the caught-up signal (see Run):
// it means the file didn't have a full chunk waiting, so whatever we did
// read drained it to its current end. A real player's Client.txt writes are
// small and sporadic, so in practice a caught-up tailer sees reads of a few
// hundred bytes at most — nowhere near this chunk size — while backlog
// replay keeps requesting full chunks until it truly runs out.
const pollChunkBytes = 64 * 1024

// Tailer polls Client.txt for new lines, forwarding each complete line to
// out. Resume position and file bookkeeping (created/modified/size) live in
// the l2p database's installs row for this install, mirroring the columns
// the old C++ LogIngestWorker maintained.
type Tailer struct {
	logPath      string
	db           *sql.DB
	installID    int64
	out          chan<- string
	fileFound    atomic.Bool
	caughtUp     atomic.Bool
	lastActivity atomic.Int64 // UnixNano of the last poll that read new lines
	offset       atomic.Int64 // bytes consumed so far
	fileSize     atomic.Int64 // file size as of the most recent successful poll
}

func New(logPath string, db *sql.DB, installID int64, out chan<- string) *Tailer {
	return &Tailer{logPath: logPath, db: db, installID: installID, out: out}
}

// CaughtUp reports whether the tailer has drained the file to its current
// end at least once since this process started — a poll that requested
// pollChunkBytes and got back less (see poll/Run). Callers use this to avoid
// broadcasting backlog events (replayed after a restart) as if they were
// live. A one-way latch: once true, it stays true for the rest of this run.
func (t *Tailer) CaughtUp() bool { return t.caughtUp.Load() }

// FileFound reports whether the tailer has successfully opened the log file
// at least once since this process started. Client.txt may not exist yet at
// startup — until the game has run at least once for this install — and
// poll() returns early on os.Open failure without ever touching caughtUp, so
// callers need this to distinguish "still waiting for the file to appear"
// from genuine backlog replay. A one-way latch, like caughtUp.
func (t *Tailer) FileFound() bool { return t.fileFound.Load() }

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
			newOffset, drainedToEnd, err := t.poll(ctx, offset)
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
			}
			// drainedToEnd means this poll asked for pollChunkBytes and got
			// back less — the file didn't have a full chunk available, so
			// once processed we're at its current end. Checked independently
			// of whether any lines were actually read this tick (rather than
			// only when newOffset == offset) so a real player's small,
			// sporadic writes don't keep pushing this out forever: getting
			// one short line is itself the caught-up signal, same as getting
			// none.
			if !t.caughtUp.Load() && drainedToEnd {
				t.caughtUp.Store(true)
				log.Printf("tailer: caught up to EOF at offset %d for %s", offset, t.logPath)
			}
		}
	}
}

// poll reads up to pollChunkBytes of new, complete lines from the log file
// starting at offset, returning the absolute offset after the read and
// whether this read drained the file to its current end (asked for
// pollChunkBytes, got back less — see Run's use of this for the caught-up
// signal). If the file has shrunk (truncated or rotated), offset resets to 0.
func (t *Tailer) poll(ctx context.Context, offset int64) (int64, bool, error) {
	f, err := os.Open(t.logPath)
	if err != nil {
		return offset, false, err
	}
	defer f.Close()
	t.fileFound.Store(true)

	fi, err := f.Stat()
	if err != nil {
		return offset, false, err
	}
	t.fileSize.Store(fi.Size())

	if fi.Size() < offset {
		log.Printf("tailer: log file shrank (was %d, now %d), resetting", offset, fi.Size())
		offset = 0
	}
	available := fi.Size() - offset
	if available == 0 {
		return offset, true, nil // nothing new at all since the last poll
	}

	toRead := available
	if toRead > pollChunkBytes {
		toRead = pollChunkBytes
	}
	drainedToEnd := toRead < pollChunkBytes

	if _, err := f.Seek(offset, io.SeekStart); err != nil {
		return offset, false, err
	}
	chunk := make([]byte, toRead)
	if _, err := io.ReadFull(f, chunk); err != nil {
		return offset, false, err
	}

	consumed := offset
	rest := chunk
	for {
		idx := bytes.IndexByte(rest, '\n')
		if idx < 0 {
			// Incomplete trailing line in this chunk — don't advance past
			// it; wait for the next poll.
			break
		}
		line := rest[:idx+1]
		rest = rest[idx+1:]
		consumed += int64(len(line))

		// Report progress after each line, not just once poll() returns: a
		// single poll() call can still take a while against a slow consumer
		// (the t.out send below blocks until the DB writer drains it) —
		// without this, Progress() (and the reported percent) would sit
		// frozen until the whole chunk finishes. Stored before the send so a
		// receiver on t.out is guaranteed (by the channel happens-before
		// relationship) to observe it.
		t.offset.Store(consumed)

		trimmed := strings.TrimRight(string(line), "\r\n")
		if trimmed == "" {
			continue
		}

		select {
		case t.out <- trimmed:
		case <-ctx.Done():
			return consumed, false, nil
		}
	}

	return consumed, drainedToEnd, nil
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
