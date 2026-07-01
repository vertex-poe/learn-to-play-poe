package tailer

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"log"
	"os"
	"strconv"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/store"
)

const pollInterval = 250 * time.Millisecond

type Tailer struct {
	logPath string
	store   *store.Store
	out     chan<- string
}

func New(logPath string, st *store.Store, out chan<- string) *Tailer {
	return &Tailer{logPath: logPath, store: st, out: out}
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
			n, err := t.poll(ctx, offset)
			if err != nil {
				if !os.IsNotExist(err) {
					log.Printf("tailer: poll error: %v", err)
				}
				continue
			}
			if n > 0 {
				offset += n
				t.saveOffset(offset)
			}
		}
	}
}

// poll reads any new complete lines from the log file starting at offset.
// Returns the number of bytes consumed (up to and including the last newline).
func (t *Tailer) poll(ctx context.Context, offset int64) (int64, error) {
	f, err := os.Open(t.logPath)
	if err != nil {
		return 0, err
	}
	defer f.Close()

	fi, err := f.Stat()
	if err != nil {
		return 0, err
	}

	if fi.Size() < offset {
		// File was truncated or rotated; restart from the beginning.
		log.Printf("tailer: log file shrank (was %d, now %d), resetting", offset, fi.Size())
		t.saveOffset(0)
		return -offset, nil // caller adjusts offset to 0
	}

	if fi.Size() == offset {
		return 0, nil // nothing new
	}

	if _, err := f.Seek(offset, io.SeekStart); err != nil {
		return 0, err
	}

	reader := bufio.NewReader(f)
	var consumed int64

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
	val, ok, err := t.store.GetState("log_offset")
	if err != nil || !ok {
		return 0
	}
	n, _ := strconv.ParseInt(val, 10, 64)
	return n
}

func (t *Tailer) saveOffset(offset int64) {
	if err := t.store.SetState("log_offset", fmt.Sprintf("%d", offset)); err != nil {
		log.Printf("tailer: failed to save offset: %v", err)
	}
}
