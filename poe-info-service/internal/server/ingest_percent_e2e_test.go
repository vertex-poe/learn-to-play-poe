package server

import (
	"encoding/json"
	"fmt"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
	"github.com/gorilla/websocket"
)

// TestStatusPercent_ClimbsDuringRealBacklogReplay_WithConcurrentClientTraffic
// reproduces a production bug report: the ingest backlog-replay percent
// reported via the "status" WS method appeared frozen instead of climbing.
//
// The mechanism: poe-info-service's db handle uses a single pooled
// connection (see openDB, SetMaxOpenConns(1)), shared by the ingest writer
// and every WS request handler. l2p-poe's MainWindow polls both
// "sessions.closeOrphans" (a DB write) and "status" (no DB access at all —
// just an atomic read off the tailer) once a second, back to back, on the
// SAME WebSocket connection. Because handleWS's per-connection read loop
// (see the `for { conn.ReadMessage(); ...; s.routeMessage(c, msg) }` loop)
// handles one message fully — synchronously, in that same goroutine —
// before reading the next, a "sessions.closeOrphans" call stuck waiting for
// the single DB connection (held almost continuously by the
// backlog-replaying ingest writer) blocks the "status" request queued right
// behind it. So its response, and the percent inside it, never arrives
// until the connection's backlog of unread requests drains — in practice,
// not until the whole Client.txt backlog finishes.
//
// This test drives the real HTTP/WS server (serve()) against a real,
// on-disk, WAL-mode database (matching openDB's DSN) with a large repeated
// Client.txt fixture, and a real websocket client that interleaves
// "sessions.closeOrphans" and "status" requests exactly like MainWindow's
// poll loop does, to reproduce the freeze under realistic conditions.
func TestStatusPercent_ClimbsDuringRealBacklogReplay_WithConcurrentClientTraffic(t *testing.T) {
	dir := t.TempDir()
	logPath := filepath.Join(dir, "Client.txt")

	// Repeat the fixture enough times that ingestion (real db writes, one
	// pooled connection, WAL mode) takes long enough in wall-clock time to
	// observe several distinct percent samples if progress is being
	// reported and delivered correctly.
	const repeats = 4000
	var sb strings.Builder
	for i := 0; i < repeats; i++ {
		sb.WriteString(testfixtures.SampleSession)
	}
	if err := os.WriteFile(logPath, []byte(sb.String()), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	addr := listener.Addr().String()

	cfg := Config{
		Version:    "test",
		StartTime:  time.Now().Unix(),
		InstallDir: dir,
		LogPath:    logPath,
		DbPath:     filepath.Join(dir, "poe-info-service.db"),
	}

	go serve(cfg, listener)
	t.Cleanup(func() { listener.Close() })

	wsURL := "ws://" + addr + "/ws"
	var conn *websocket.Conn
	for deadline := time.Now().Add(2 * time.Second); ; {
		conn, _, err = websocket.DefaultDialer.Dial(wsURL, nil)
		if err == nil {
			break
		}
		if time.Now().After(deadline) {
			t.Fatalf("dial %s: %v", wsURL, err)
		}
		time.Sleep(10 * time.Millisecond)
	}
	defer conn.Close()

	responses := make(chan proto.Message, 256)
	go func() {
		// Closing here (rather than from the main goroutine) is what makes
		// this safe: only the sender of a channel should close it. conn.Close()
		// below makes ReadMessage return an error, which ends this goroutine.
		defer close(responses)
		for {
			_, data, err := conn.ReadMessage()
			if err != nil {
				return
			}
			var m proto.Message
			if json.Unmarshal(data, &m) == nil {
				responses <- m
			}
		}
	}()

	send := func(id, method string, payload any) {
		p, _ := json.Marshal(payload)
		msg := proto.Message{Type: proto.TypeRequest, ID: id, Method: method, Payload: p}
		data, _ := json.Marshal(msg)
		conn.WriteMessage(websocket.TextMessage, data)
	}

	var mu sync.Mutex
	percentByReqID := map[string]*float64{}

	var wg sync.WaitGroup
	wg.Add(1)
	go func() {
		defer wg.Done()
		for r := range responses {
			if !strings.HasPrefix(r.ID, "status-") {
				continue
			}
			var status struct {
				Percent *float64 `json:"percent"`
			}
			if json.Unmarshal(r.Payload, &status) == nil {
				mu.Lock()
				percentByReqID[r.ID] = status.Percent
				mu.Unlock()
			}
		}
	}()

	// Mimic MainWindow.onPollTimer: once per (test-scale, faster-than-prod)
	// tick, send sessions.closeOrphans immediately followed by status, on
	// the same connection — exactly the interleaving that starves status
	// behind a blocked DB write in the original bug.
	deadline := time.Now().Add(15 * time.Second)
	for tick := 0; time.Now().Before(deadline); tick++ {
		send(fmt.Sprintf("orphans-%d", tick), "sessions.closeOrphans",
			map[string]any{"running_install_paths": []string{}})
		send(fmt.Sprintf("status-%d", tick), "status", map[string]any{})
		time.Sleep(100 * time.Millisecond)

		mu.Lock()
		n := len(percentByReqID)
		mu.Unlock()
		if n >= 8 {
			break // Enough samples gathered to judge whether percent climbs.
		}
	}

	conn.Close()
	wg.Wait()

	mu.Lock()
	defer mu.Unlock()
	distinct := map[float64]bool{}
	var withPercent int
	for _, p := range percentByReqID {
		if p != nil {
			withPercent++
			distinct[*p] = true
		}
	}
	t.Logf("collected %d status responses, %d with a percent field, %d distinct values: %v",
		len(percentByReqID), withPercent, len(distinct), percentByReqID)
	if len(percentByReqID) < 5 {
		t.Fatalf("only received %d status responses in 15s of polling (want >=5) — "+
			"status requests are being starved behind concurrent DB-bound requests", len(percentByReqID))
	}
	if len(distinct) < 3 {
		t.Fatalf("status percent did not climb: got %d distinct value(s) across %d responses (%d had a percent field) — samples=%v",
			len(distinct), len(percentByReqID), withPercent, percentByReqID)
	}
}
