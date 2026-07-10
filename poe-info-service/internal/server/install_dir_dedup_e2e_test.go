package server

import (
	"database/sql"
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
	"github.com/gorilla/websocket"

	_ "modernc.org/sqlite"
)

// TestServe_DedupesInstallDirRegardlessOfSlashDirection is a regression test
// for a real production bug: the same physical install directory arrived
// once spelled with forward slashes (from poe-info-service.toml's
// install_dirs) and once with backslashes (from Windows
// process/auto-detection), compared unequal as the s.tailers map key in
// addInstallTarget, and so got tailed twice — two parallel writer/tailer
// pipelines independently replaying the same Client.txt, doubling every
// session (and inflating AFK/alt-tab totals beyond a session's actual
// length) in the database. Configuring the same directory under both
// spellings must collapse to a single installs row / single tailer.
func TestServe_DedupesInstallDirRegardlessOfSlashDirection(t *testing.T) {
	dir := t.TempDir()
	logPath := filepath.Join(dir, "Client.txt")
	if err := os.WriteFile(logPath, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log: %v", err)
	}

	altDir := strings.ReplaceAll(dir, `\`, `/`)
	if altDir == dir {
		t.Skip("temp dir contains no backslashes on this platform, nothing to dedupe")
	}

	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	addr := listener.Addr().String()

	dbPath := filepath.Join(t.TempDir(), "poe-info-service.db")
	cfg := Config{
		Version:   "test",
		StartTime: time.Now().Unix(),
		Installs: []InstallTarget{
			{Dir: dir, LogPath: logPath},
			{Dir: altDir, LogPath: logPath},
		},
		DbPath: dbPath,
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

	// Poll "status" until backlog replay has caught up (aggregate phase ==
	// "tailing" — see aggregateProgress/ingestStatus).
	deadline := time.Now().Add(10 * time.Second)
	for {
		if time.Now().After(deadline) {
			t.Fatal("timed out waiting for backlog replay to finish")
		}
		msg := proto.Message{Type: proto.TypeRequest, ID: "status", Method: "status"}
		data, _ := json.Marshal(msg)
		if err := conn.WriteMessage(websocket.TextMessage, data); err != nil {
			t.Fatalf("write status request: %v", err)
		}
		_, respData, err := conn.ReadMessage()
		if err != nil {
			t.Fatalf("read status response: %v", err)
		}
		var resp proto.Message
		if err := json.Unmarshal(respData, &resp); err != nil {
			t.Fatalf("unmarshal status response: %v", err)
		}
		var status proto.StatusPayload
		if err := json.Unmarshal(resp.Payload, &status); err != nil {
			t.Fatalf("unmarshal status payload: %v", err)
		}
		if status.Phase == "tailing" {
			break
		}
		time.Sleep(50 * time.Millisecond)
	}
	conn.Close()

	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		t.Fatalf("open db for verification: %v", err)
	}
	defer db.Close()

	var installCount int
	if err := db.QueryRow(`SELECT COUNT(*) FROM installs`).Scan(&installCount); err != nil {
		t.Fatalf("count installs: %v", err)
	}
	if installCount != 1 {
		t.Errorf("installs row count = %d, want 1 (both spellings should dedupe to the same install)", installCount)
	}

	// The tailer reaching EOF (phase=="tailing" above) doesn't guarantee the
	// writer's batched transaction for those lines has committed yet (see
	// broadcastLogEvents' batchFlushIdle) — poll briefly rather than
	// asserting on a single immediate read.
	var sessionCount int
	deadline = time.Now().Add(2 * time.Second)
	for {
		if err := db.QueryRow(`SELECT COUNT(*) FROM sessions`).Scan(&sessionCount); err != nil {
			t.Fatalf("count sessions: %v", err)
		}
		if sessionCount > 0 || time.Now().After(deadline) {
			break
		}
		time.Sleep(20 * time.Millisecond)
	}
	if sessionCount != 1 {
		t.Errorf("sessions row count = %d, want 1 — got duplicate sessions from the same Client.txt being tailed twice", sessionCount)
	}
}
