package tailer

import (
	"context"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

// drainLines collects exactly n lines from out, failing the test if that
// many don't arrive within timeout. Used instead of a fixed sleep because
// the tailer polls on pollInterval, not on a signal this test can hook.
func drainLines(t *testing.T, out <-chan string, n int, timeout time.Duration) []string {
	t.Helper()
	lines := make([]string, 0, n)
	deadline := time.After(timeout)
	for len(lines) < n {
		select {
		case l := <-out:
			lines = append(lines, l)
		case <-deadline:
			t.Fatalf("timed out after collecting %d/%d lines: %v", len(lines), n, lines)
		}
	}
	return lines
}

func waitForCaughtUp(t *testing.T, tl *Tailer, timeout time.Duration) {
	t.Helper()
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if tl.CaughtUp() {
			return
		}
		time.Sleep(10 * time.Millisecond)
	}
	t.Fatal("timed out waiting for tailer to catch up to EOF")
}

// assertNoMoreLines fails the test if another line arrives on out within
// timeout, used to catch a tailer re-emitting lines it already delivered.
func assertNoMoreLines(t *testing.T, out <-chan string, timeout time.Duration) {
	t.Helper()
	select {
	case l := <-out:
		t.Fatalf("unexpected extra line delivered: %q", l)
	case <-time.After(timeout):
	}
}

// TestTailer_ReadsFullFixtureToEOF writes the same Client.txt fixture used by
// internal/parser and internal/ingest to a real file and drives it through
// the actual polling/read code path (Tailer.Run -> poll), rather than
// feeding strings straight to a parser. It exists because no prior tailer
// test read more than a couple of hand-written lines, so a real multi-line,
// multi-poll file had never been exercised end to end.
func TestTailer_ReadsFullFixtureToEOF(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	if err := os.WriteFile(logPath, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	wantLines := testfixtures.SampleSessionLines()
	out := make(chan string, len(wantLines)+8)
	tl := New(logPath, db, installID, out)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	gotLines := drainLines(t, out, len(wantLines), 3*time.Second)
	waitForCaughtUp(t, tl, 2*time.Second)

	for i, want := range wantLines {
		if gotLines[i] != want {
			t.Errorf("line %d = %q, want %q", i, gotLines[i], want)
		}
	}
	assertNoMoreLines(t, out, 300*time.Millisecond)
}

// TestTailer_ProgressTracksBacklogAndReachesFileSizeOnceCaughtUp drives a
// real fixture file through Tailer.Run and asserts Progress() reports the
// full file size as both offset and size once backlog replay finishes —
// the basis for the percent-complete figure the "status" WS method reports
// while phase=="ingesting".
func TestTailer_ProgressTracksBacklogAndReachesFileSizeOnceCaughtUp(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	if err := os.WriteFile(logPath, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}
	wantSize := int64(len(testfixtures.SampleSession))

	wantLines := testfixtures.SampleSessionLines()
	out := make(chan string, len(wantLines)+8)
	tl := New(logPath, db, installID, out)

	if offset, size := tl.Progress(); offset != 0 || size != 0 {
		t.Fatalf("Progress() before any poll = (%d, %d), want (0, 0)", offset, size)
	}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	drainLines(t, out, len(wantLines), 3*time.Second)
	waitForCaughtUp(t, tl, 2*time.Second)

	offset, size := tl.Progress()
	if size != wantSize {
		t.Errorf("Progress() size = %d, want %d", size, wantSize)
	}
	if offset != size {
		t.Errorf("Progress() offset = %d, want it to equal size (%d) once caught up", offset, size)
	}
}

// TestTailer_ProgressUpdatesIncrementallyDuringBacklogReplay guards against a
// bug where a single poll() call drained every currently-available line in
// one shot and only reported Progress() once the whole call returned — for a
// large real Client.txt with a slow downstream consumer (out send blocks
// until drained), that left the reported backlog-replay percent frozen for
// the entire replay instead of climbing. Uses an unbuffered channel so the
// tailer goroutine is forced to block mid-poll (after the first line) until
// this test reads it, simulating that slow consumer.
func TestTailer_ProgressUpdatesIncrementallyDuringBacklogReplay(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	if err := os.WriteFile(logPath, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}
	wantSize := int64(len(testfixtures.SampleSession))
	wantLines := testfixtures.SampleSessionLines()
	if len(wantLines) < 2 {
		t.Fatalf("fixture too short to exercise mid-poll progress (%d lines)", len(wantLines))
	}

	out := make(chan string) // unbuffered: forces poll() to block after each line
	tl := New(logPath, db, installID, out)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	<-out // Received the first line; poll() stored progress before sending it.

	offset, size := tl.Progress()
	if size != wantSize {
		t.Errorf("Progress() size = %d, want %d", size, wantSize)
	}
	if offset <= 0 || offset >= size {
		t.Errorf("Progress() offset after 1/%d lines = %d, want strictly between 0 and %d",
			len(wantLines), offset, size)
	}

	for i := 1; i < len(wantLines); i++ {
		<-out
	}
	waitForCaughtUp(t, tl, 2*time.Second)
}

// TestTailer_CaughtUpDespiteContinuousSmallWrites proves the caught-up
// signal no longer needs a poll tick with literally zero new bytes. A real
// player's Client.txt writes are small and sporadic, but if the game were
// somehow writing on every single tick, the tailer must still recognize
// it's caught up rather than waiting forever for a truly quiet tick to
// arrive — see poll's drainedToEnd (a read shorter than the requested
// pollChunkBytes), which Run checks instead of "zero bytes this tick".
func TestTailer_CaughtUpDespiteContinuousSmallWrites(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	const initial = "2024/01/15 10:00:00 ***** LOG FILE OPENING *****\n"
	if err := os.WriteFile(logPath, []byte(initial), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	out := make(chan string, 64)
	tl := New(logPath, db, installID, out)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	drainLines(t, out, 1, 2*time.Second)
	waitForCaughtUp(t, tl, 2*time.Second)

	// Keep writing one small line every 100ms — faster than pollInterval
	// (250ms), so there's realistically always something new by the next
	// tick. Under the old "zero new bytes this tick" rule this would never
	// re-confirm caught-up; it must already be latched from above and must
	// stay latched (a one-way latch) throughout the trickle.
	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		t.Fatalf("reopen log for append: %v", err)
	}
	defer f.Close()
	for i := 0; i < 5; i++ {
		line := fmt.Sprintf("2024/01/15 10:00:%02d 1 a [INFO] Client 1 : AFK mode is now ON.\n", i+1)
		if _, err := f.WriteString(line); err != nil {
			t.Fatalf("append log: %v", err)
		}
		time.Sleep(100 * time.Millisecond)
		if !tl.CaughtUp() {
			t.Fatalf("tailer lost caught-up state during continuous small writes (iteration %d)", i)
		}
	}
	drainLines(t, out, 5, 2*time.Second)
}

// TestTailer_ResumesFromSavedOffsetAfterRestart simulates poe-info-service
// restarting mid-session: a Tailer catches up to the first half of the
// fixture, is torn down (offset persisted to the installs row, same as a
// real shutdown), the game keeps appending to Client.txt, and a brand new
// Tailer for the same install must resume from the saved offset — replaying
// none of the already-consumed lines and picking up only what's new.
func TestTailer_ResumesFromSavedOffsetAfterRestart(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")

	allLines := testfixtures.SampleSessionLines()
	split := len(allLines) / 2
	firstHalf, secondHalf := allLines[:split], allLines[split:]

	if err := os.WriteFile(logPath, []byte(strings.Join(firstHalf, "\n")+"\n"), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	out1 := make(chan string, len(firstHalf)+8)
	tl1 := New(logPath, db, installID, out1)
	ctx1, cancel1 := context.WithCancel(context.Background())
	go tl1.Run(ctx1)

	got1 := drainLines(t, out1, len(firstHalf), 3*time.Second)
	waitForCaughtUp(t, tl1, 2*time.Second)
	for i, want := range firstHalf {
		if got1[i] != want {
			t.Fatalf("first-half line %d = %q, want %q", i, got1[i], want)
		}
	}
	cancel1() // simulate the service process stopping

	f, err := os.OpenFile(logPath, os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		t.Fatalf("reopen log for append: %v", err)
	}
	if _, err := f.WriteString(strings.Join(secondHalf, "\n") + "\n"); err != nil {
		t.Fatalf("append log: %v", err)
	}
	if err := f.Close(); err != nil {
		t.Fatalf("close log: %v", err)
	}

	out2 := make(chan string, len(secondHalf)+8)
	tl2 := New(logPath, db, installID, out2) // same db/installID => resumes from the persisted offset
	ctx2, cancel2 := context.WithCancel(context.Background())
	defer cancel2()
	go tl2.Run(ctx2)

	got2 := drainLines(t, out2, len(secondHalf), 3*time.Second)
	waitForCaughtUp(t, tl2, 2*time.Second)
	for i, want := range secondHalf {
		if got2[i] != want {
			t.Errorf("second-half line %d = %q, want %q", i, got2[i], want)
		}
	}
	assertNoMoreLines(t, out2, 300*time.Millisecond)
}

// TestTailer_ResetsOffsetWhenFileShrinks covers the log-rotation branch in
// poll (fi.Size() < offset): PoE truncates Client.txt on some launches
// rather than always appending, and the tailer must restart from 0 instead
// of getting stuck seeking past the new EOF.
func TestTailer_ResetsOffsetWhenFileShrinks(t *testing.T) {
	db, installID := newTestDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")

	padding := make([]string, 5)
	for i := range padding {
		padding[i] = "2024/01/15 10:00:00 1 a [INFO] Client 1 : padding line to be truncated away"
	}
	if err := os.WriteFile(logPath, []byte(strings.Join(padding, "\n")+"\n"), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	out := make(chan string, 32)
	tl := New(logPath, db, installID, out)
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)

	drainLines(t, out, len(padding), 3*time.Second)
	waitForCaughtUp(t, tl, 2*time.Second)

	newLine := "2024/01/15 11:00:00 ***** LOG FILE OPENING *****"
	if err := os.WriteFile(logPath, []byte(newLine+"\n"), 0644); err != nil {
		t.Fatalf("truncate+rewrite log: %v", err)
	}

	got := drainLines(t, out, 1, 3*time.Second)
	if got[0] != newLine {
		t.Errorf("after truncation, got %q, want %q", got[0], newLine)
	}
	assertNoMoreLines(t, out, 300*time.Millisecond)
}
