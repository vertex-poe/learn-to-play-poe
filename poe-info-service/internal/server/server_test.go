package server

import (
	"bytes"
	"context"
	"database/sql"
	"encoding/json"
	"log"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/parser"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/schema"
	"github.com/MovingCairn/poe-info-service/internal/tailer"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"

	_ "modernc.org/sqlite"
)

// waitForCancel blocks until ctx is done or the timeout elapses, returning
// whether ctx was actually cancelled.
func waitForCancel(ctx context.Context, timeout time.Duration) bool {
	select {
	case <-ctx.Done():
		return true
	case <-time.After(timeout):
		return false
	}
}

// TestWatchIngestStall_LogsWhenStuckOnSameLine reproduces (at unit-test
// speed) the production symptom that motivated it: broadcastLogEvents can
// get stuck forever inside ParseLine/HandleEvent for some specific
// real-world line, which — since the tailer then blocks handing it the next
// line once eventCh's buffer fills — looks externally like a backlog-replay
// percent (see ingestStatus) frozen at the same value forever, with no other
// log output to explain why. This asserts the watchdog actually reports the
// stuck line/event once it's been "in flight" longer than the interval, and
// stays quiet while a line is still being processed within that window.
func TestWatchIngestStall_LogsWhenStuckOnSameLine(t *testing.T) {
	var buf bytes.Buffer
	origOutput := log.Writer()
	origFlags := log.Flags()
	log.SetOutput(&buf)
	log.SetFlags(0)
	t.Cleanup(func() {
		log.SetOutput(origOutput)
		log.SetFlags(origFlags)
	})

	var currentLine, currentEventType atomic.Value
	var stepStartedNs atomic.Int64
	currentLine.Store("2024/01/15 10:00:05 104 a [INFO] Client 1 : You have entered Lioneye's Watch.")
	currentEventType.Store("area_entered")
	stepStartedNs.Store(time.Now().UnixNano())

	const interval = 30 * time.Millisecond
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go watchIngestStall(ctx, interval, &currentLine, &currentEventType, &stepStartedNs)

	// Still "in flight" but younger than interval: must not warn yet.
	time.Sleep(interval / 2)
	if strings.Contains(buf.String(), "stalled") {
		t.Fatalf("watchIngestStall warned before interval elapsed: %q", buf.String())
	}

	// Now old enough: must warn, naming the exact line/event stuck on.
	deadline := time.Now().Add(2 * time.Second)
	for !strings.Contains(buf.String(), "stalled") && time.Now().Before(deadline) {
		time.Sleep(5 * time.Millisecond)
	}
	got := buf.String()
	if !strings.Contains(got, "stalled") {
		t.Fatalf("watchIngestStall never warned about the stall within 2s; log=%q", got)
	}
	if !strings.Contains(got, "Lioneye's Watch") || !strings.Contains(got, "area_entered") {
		t.Errorf("stall warning didn't name the stuck line/event: %q", got)
	}

	// Once the "line" finishes (stepStartedNs reset to 0, as
	// broadcastLogEvents does after each line), the watchdog must go quiet
	// again rather than keep reporting a stale stall.
	buf.Reset()
	stepStartedNs.Store(0)
	time.Sleep(3 * interval)
	if strings.Contains(buf.String(), "stalled") {
		t.Errorf("watchIngestStall kept warning after the line finished: %q", buf.String())
	}
}

// TestOpenDB_ActuallyAppliesWALAndPragmas guards against a real bug found in
// production: openDB's DSN used the mattn/go-sqlite3-style shorthand params
// (_journal_mode=WAL&_synchronous=NORMAL&...), but modernc.org/sqlite (the
// driver actually in use) only recognizes repeated _pragma=name(value)
// parameters — the shorthand ones are silently ignored as unknown query
// params, so the database ran in SQLite's default rollback-journal +
// synchronous=FULL mode this whole time despite the DSN looking correct.
// That's a meaningful chunk of the per-commit overhead behind the slow
// backlog-replay throughput fixed elsewhere this session (see
// broadcastLogEvents' batching). Confirms the pragmas actually took effect,
// not just that the DSN string contains particular substrings.
func TestOpenDB_ActuallyAppliesWALAndPragmas(t *testing.T) {
	dbPath := filepath.Join(t.TempDir(), "poe-info-service.db")
	db, err := openDB(dbPath)
	if err != nil {
		t.Fatalf("openDB: %v", err)
	}
	defer db.Close()

	var journalMode string
	if err := db.QueryRow("PRAGMA journal_mode").Scan(&journalMode); err != nil {
		t.Fatalf("query journal_mode: %v", err)
	}
	if !strings.EqualFold(journalMode, "wal") {
		t.Errorf("journal_mode = %q, want \"wal\"", journalMode)
	}

	var synchronous int
	if err := db.QueryRow("PRAGMA synchronous").Scan(&synchronous); err != nil {
		t.Fatalf("query synchronous: %v", err)
	}
	const synchronousNormal = 1 // SQLite's PRAGMA synchronous integer code for NORMAL (0=OFF, 1=NORMAL, 2=FULL, 3=EXTRA)
	if synchronous != synchronousNormal {
		t.Errorf("synchronous = %d, want %d (NORMAL)", synchronous, synchronousNormal)
	}

	var foreignKeys int
	if err := db.QueryRow("PRAGMA foreign_keys").Scan(&foreignKeys); err != nil {
		t.Fatalf("query foreign_keys: %v", err)
	}
	if foreignKeys != 1 {
		t.Errorf("foreign_keys = %d, want 1 (on)", foreignKeys)
	}
}

func TestWatchIdleShutsDownAfterTimeout(t *testing.T) {
	srv := &server{}
	srv.touch()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, nil, 30*time.Millisecond, 5*time.Millisecond)

	if !waitForCancel(ctx, time.Second) {
		t.Fatal("expected watchIdle to cancel the context after the idle timeout, but it did not")
	}
}

func TestWatchIdleResetsOnClientActivity(t *testing.T) {
	srv := &server{}
	srv.touch()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, nil, 200*time.Millisecond, 10*time.Millisecond)

	// Keep touching the server well inside the idle timeout so it never fires,
	// with generous margin for scheduling jitter.
	deadline := time.Now().Add(400 * time.Millisecond)
	for time.Now().Before(deadline) {
		srv.touch()
		time.Sleep(20 * time.Millisecond)
	}

	select {
	case <-ctx.Done():
		t.Fatal("watchIdle shut down despite ongoing client activity")
	default:
	}
}

// fakeTailerActivity stands in for tailer.Tailer.LastActivity in tests,
// letting us simulate Client.txt activity without a real log file.
type fakeTailerActivity struct {
	mu   sync.Mutex
	last time.Time
}

func (f *fakeTailerActivity) touch() {
	f.mu.Lock()
	defer f.mu.Unlock()
	f.last = time.Now()
}

func (f *fakeTailerActivity) LastActivity() time.Time {
	f.mu.Lock()
	defer f.mu.Unlock()
	return f.last
}

func TestWatchIdleKeptAliveByTailerActivity(t *testing.T) {
	srv := &server{}
	// Simulate a client that connected a long time ago and went idle.
	srv.lastActivity.Store(time.Now().Add(-time.Hour).UnixNano())

	activity := &fakeTailerActivity{}
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, activity.LastActivity, 200*time.Millisecond, 10*time.Millisecond)

	deadline := time.Now().Add(400 * time.Millisecond)
	for time.Now().Before(deadline) {
		activity.touch()
		time.Sleep(20 * time.Millisecond)
	}

	select {
	case <-ctx.Done():
		t.Fatal("watchIdle shut down despite ongoing Client.txt activity")
	default:
	}
}

func TestRouteMessagePingDoesNotTouch(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	srv.routeMessage(c, proto.Message{Type: proto.TypePing, ID: "1"})

	if srv.lastActivity.Load() != 0 {
		t.Fatal("expected a bare ping not to touch lastActivity")
	}
}

func TestRouteMessageKeepaliveTouches(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	before := time.Now()
	srv.routeMessage(c, proto.Message{Type: proto.TypeKeepalive, ID: "1"})

	got := srv.lastActivity.Load()
	if got == 0 {
		t.Fatal("expected keepalive to touch lastActivity")
	}
	if time.Unix(0, got).Before(before) {
		t.Fatalf("lastActivity %v looks stale relative to %v", time.Unix(0, got), before)
	}
}

func TestRouteMessageSubscribeTouches(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()

	srv.routeMessage(c, proto.Message{Type: proto.TypeSubscribe, Topic: "clientlog", ID: "1"})

	if srv.lastActivity.Load() == 0 {
		t.Fatal("expected subscribe to touch lastActivity")
	}
}

// newTestBroadcastDB returns a real schema'd sqlite db (on-disk, WAL mode —
// matching production's openDB) plus a ready Writer, for driving
// broadcastLogEvents directly in tests.
func newTestBroadcastDB(t *testing.T) (*sql.DB, *ingest.Writer) {
	t.Helper()
	dbPath := filepath.Join(t.TempDir(), "poe-info-service.db")
	dsn := dbPath + "?_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		t.Fatalf("open db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("ensure schema: %v", err)
	}
	installID, err := ingest.EnsureInstall(db, t.TempDir())
	if err != nil {
		t.Fatalf("ensure install: %v", err)
	}
	w, err := ingest.NewWriter(db, installID)
	if err != nil {
		t.Fatalf("new writer: %v", err)
	}
	return db, w
}

// TestBroadcastLogEvents_FlushesPendingBatchWhenIdle guards the fix for a
// real deadlock found in production ("percent never climbs"): a batch must
// get committed once nothing new arrives for a while, even though eventCh
// never goes empty from the goroutine's own observation and is never
// closed — see batchFlushIdle's doc comment for the full mechanism. Sends
// fewer events than maxBatchEvents and then simply stops, the same shape as
// a tailer that has drained everything currently available and gone quiet.
func TestBroadcastLogEvents_FlushesPendingBatchWhenIdle(t *testing.T) {
	db, w := newTestBroadcastDB(t)
	p := parser.New()
	eventCh := make(chan string, 8)
	h := hub.New()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go broadcastLogEvents(ctx, h, eventCh, p, w, db, func() bool { return true }, func() bool { return false })

	eventCh <- "2024/01/15 10:00:00 ***** LOG FILE OPENING *****"
	eventCh <- "2024/01/15 10:00:05 104 a [INFO] Client 1 : You have entered Lioneye's Watch."

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		var count int
		if err := db.QueryRow("SELECT COUNT(*) FROM sessions").Scan(&count); err == nil && count > 0 {
			return
		}
		time.Sleep(20 * time.Millisecond)
	}
	t.Fatal("session row never committed after going idle — batch was never flushed")
}

// TestBroadcastLogEvents_DoesNotDeadlockWhenChannelBufferOverflows reproduces
// the actual deadlock end to end: a real tailer, reading a file with more
// lines than eventCh's buffer, must block mid-send at some point — if that
// block happens to land right as broadcastLogEvents' batch-commit decision
// raced (the old len(eventCh)==0 check), the tailer's next saveOffset()
// (needing the single pooled connection broadcastLogEvents was still
// holding via an uncommitted transaction) and broadcastLogEvents itself
// (idle, waiting for a line the stuck tailer will never send) deadlocked
// forever. Uses a synthetic fixture sized well past the channel buffer, not
// real user log content.
func TestBroadcastLogEvents_DoesNotDeadlockWhenChannelBufferOverflows(t *testing.T) {
	dir := t.TempDir()
	logPath := filepath.Join(dir, "Client.txt")

	// Comfortably more lines than eventCh's buffer (below), so the tailer
	// must block sending into a full channel before this poll() returns.
	var sb strings.Builder
	for i := 0; i < 30; i++ {
		sb.WriteString(testfixtures.SampleSession)
	}
	if err := os.WriteFile(logPath, []byte(sb.String()), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	dbPath := filepath.Join(dir, "poe-info-service.db")
	dsn := dbPath + "?_pragma=journal_mode(WAL)&_pragma=busy_timeout(5000)"
	db, err := sql.Open("sqlite", dsn)
	if err != nil {
		t.Fatalf("open db: %v", err)
	}
	defer db.Close()
	db.SetMaxOpenConns(1)
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("ensure schema: %v", err)
	}
	installID, err := ingest.EnsureInstall(db, dir)
	if err != nil {
		t.Fatalf("ensure install: %v", err)
	}
	w, err := ingest.NewWriter(db, installID)
	if err != nil {
		t.Fatalf("new writer: %v", err)
	}
	p := parser.New()
	eventCh := make(chan string, 32) // small on purpose: forces the tailer to block sending well before this fixture's lines run out
	tl := tailer.New(logPath, db, installID, eventCh)
	h := hub.New()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)
	go broadcastLogEvents(ctx, h, eventCh, p, w, db, tl.CaughtUp, func() bool { return false })

	deadline := time.Now().Add(15 * time.Second)
	for time.Now().Before(deadline) {
		if tl.CaughtUp() {
			return
		}
		time.Sleep(50 * time.Millisecond)
	}
	offset, size := tl.Progress()
	t.Fatalf("deadlocked: never caught up within 15s (offset=%d size=%d)", offset, size)
}

func TestIngestStatus(t *testing.T) {
	tests := []struct {
		name        string
		hasTailer   bool
		caughtUp    bool
		offset      int64
		size        int64
		wantPhase   string
		wantMessage string
		wantPercent *float64
	}{
		{
			name:        "no tailer configured",
			hasTailer:   false,
			wantPhase:   "waiting",
			wantMessage: "waiting",
		},
		{
			name:        "caught up and tailing live",
			hasTailer:   true,
			caughtUp:    true,
			offset:      1000,
			size:        1000,
			wantPhase:   "tailing",
			wantMessage: "waiting for game events",
		},
		{
			name:        "ingesting backlog with known size",
			hasTailer:   true,
			caughtUp:    false,
			offset:      25,
			size:        100,
			wantPhase:   "ingesting",
			wantMessage: "processing game logs",
			wantPercent: floatPtr(25),
		},
		{
			name:        "ingesting backlog with size not yet known",
			hasTailer:   true,
			caughtUp:    false,
			offset:      0,
			size:        0,
			wantPhase:   "ingesting",
			wantMessage: "processing game logs",
			wantPercent: nil,
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			phase, message, percent := ingestStatus(tt.hasTailer, tt.caughtUp, tt.offset, tt.size)
			if phase != tt.wantPhase {
				t.Errorf("phase = %q, want %q", phase, tt.wantPhase)
			}
			if message != tt.wantMessage {
				t.Errorf("message = %q, want %q", message, tt.wantMessage)
			}
			if (percent == nil) != (tt.wantPercent == nil) {
				t.Fatalf("percent = %v, want %v", percent, tt.wantPercent)
			}
			if percent != nil && *percent != *tt.wantPercent {
				t.Errorf("percent = %v, want %v", *percent, *tt.wantPercent)
			}
		})
	}
}

func floatPtr(v float64) *float64 { return &v }

// newTestTailerDB mirrors internal/tailer's own test helper: a minimal
// installs table sufficient for a real *tailer.Tailer to load/save offsets.
func newTestTailerDB(t *testing.T) (*sql.DB, int64) {
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

// TestWatchIngestStatus_PublishesOnPhaseChangeAndStopsAtTailing drives a real
// *tailer.Tailer against a fixture Client.txt file, following the same
// pattern as internal/tailer's own fixture tests, to prove watchIngestStatus
// publishes a "status" topic event once backlog replay finishes (phase
// "tailing") and then stops — "tailing" is a one-way state (see
// tailer.CaughtUp) so there's nothing more to report about it this run.
func TestWatchIngestStatus_PublishesOnPhaseChangeAndStopsAtTailing(t *testing.T) {
	db, installID := newTestTailerDB(t)
	logPath := filepath.Join(t.TempDir(), "Client.txt")
	if err := os.WriteFile(logPath, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	out := make(chan string, 128)
	tl := tailer.New(logPath, db, installID, out)

	h := hub.New()
	c := hub.NewClient()
	defer c.Close()
	h.Subscribe(c, proto.TopicStatus)

	srv := &server{hub: h, tailers: []*tailer.Tailer{tl}, cfg: Config{Version: "test"}, started: time.Now()}

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go tl.Run(ctx)
	go watchIngestStatus(ctx, srv)

	// Drain the backlog lines so the tailer can reach EOF and flip caughtUp.
	drained := 0
	want := len(testfixtures.SampleSessionLines())
	deadline := time.After(3 * time.Second)
	for drained < want {
		select {
		case <-out:
			drained++
		case <-deadline:
			t.Fatalf("timed out draining backlog lines: got %d/%d", drained, want)
		}
	}

	// There may be zero or more "ingesting" percent-change broadcasts first
	// (depending on timing), followed by exactly one "tailing" broadcast.
	sawTailing := false
	deadline = time.After(2 * time.Second)
	for !sawTailing {
		select {
		case data := <-c.Send:
			var msg proto.Message
			if err := json.Unmarshal(data, &msg); err != nil {
				t.Fatalf("unmarshal event: %v", err)
			}
			if msg.Type != proto.TypeEvent || msg.Topic != proto.TopicStatus {
				t.Fatalf("got type=%q topic=%q, want type=%q topic=%q", msg.Type, msg.Topic, proto.TypeEvent, proto.TopicStatus)
			}
			var payload proto.StatusPayload
			if err := json.Unmarshal(msg.Payload, &payload); err != nil {
				t.Fatalf("unmarshal payload: %v", err)
			}
			if payload.Phase == "tailing" {
				sawTailing = true
			}
		case <-deadline:
			t.Fatal("timed out waiting for a status broadcast with phase=tailing")
		}
	}

	// Once "tailing" is reached, watchIngestStatus stops for good.
	select {
	case data := <-c.Send:
		t.Fatalf("unexpected extra status broadcast after reaching tailing: %s", data)
	case <-time.After(1200 * time.Millisecond):
	}
}

func TestWatchIdleFiresWhenTailerGoesQuiet(t *testing.T) {
	srv := &server{}
	srv.lastActivity.Store(time.Now().Add(-time.Hour).UnixNano())

	activity := &fakeTailerActivity{}
	activity.touch() // one recent burst, then silence

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	go watchIdle(ctx, cancel, srv, activity.LastActivity, 30*time.Millisecond, 5*time.Millisecond)

	if !waitForCancel(ctx, time.Second) {
		t.Fatal("expected watchIdle to cancel once tailer activity also went stale")
	}
}
