package server

import (
	"database/sql"
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
	"github.com/gorilla/websocket"

	_ "modernc.org/sqlite"
)

// TestServe_IngestsEveryConfiguredInstall drives the real HTTP/WS server
// (serve()) with two configured installs, both pointing at real, on-disk
// Client.txt fixtures, and proves both get ingested to completion — not just
// the first one in Config.Installs. This is the behavior a user with two
// simultaneously valid PoE installs (e.g. two SteamLibrary drives) relies on:
// before multi-install support, only the first configured install was ever
// tailed, so the second one's Client.txt (however real) sat untouched.
func TestServe_IngestsEveryConfiguredInstall(t *testing.T) {
	dir1 := t.TempDir()
	dir2 := t.TempDir()
	logPath1 := filepath.Join(dir1, "Client.txt")
	logPath2 := filepath.Join(dir2, "Client.txt")

	if err := os.WriteFile(logPath1, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log 1: %v", err)
	}
	if err := os.WriteFile(logPath2, []byte(testfixtures.SampleSession), 0644); err != nil {
		t.Fatalf("write log 2: %v", err)
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
			{Dir: dir1, LogPath: logPath1},
			{Dir: dir2, LogPath: logPath2},
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

	// Poll "status" until both installs' tailers have caught up (aggregate
	// phase == "tailing" — see aggregateProgress/ingestStatus).
	deadline := time.Now().Add(10 * time.Second)
	for {
		if time.Now().After(deadline) {
			t.Fatal("timed out waiting for both installs to finish backlog replay")
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

	// Both installs must have been ingested to completion — not just
	// installs.path rows, but each one's tailer fully drained its own
	// Client.txt (last_byte_offset caught up to file_size).
	db, err := sql.Open("sqlite", dbPath)
	if err != nil {
		t.Fatalf("open db for verification: %v", err)
	}
	defer db.Close()

	rows, err := db.Query(`SELECT path, last_byte_offset, file_size FROM installs ORDER BY path`)
	if err != nil {
		t.Fatalf("query installs: %v", err)
	}
	defer rows.Close()

	type installRow struct {
		path             string
		offset, fileSize int64
	}
	var got []installRow
	for rows.Next() {
		var r installRow
		if err := rows.Scan(&r.path, &r.offset, &r.fileSize); err != nil {
			t.Fatalf("scan install row: %v", err)
		}
		got = append(got, r)
	}

	if len(got) != 2 {
		t.Fatalf("expected 2 installs rows (one per configured install), got %d: %+v", len(got), got)
	}
	for _, r := range got {
		if r.fileSize == 0 {
			t.Errorf("install %q: file_size is 0, expected it to have been observed by its tailer", r.path)
		}
		if r.offset != r.fileSize {
			t.Errorf("install %q: last_byte_offset=%d does not match file_size=%d — backlog replay did not finish for this install",
				r.path, r.offset, r.fileSize)
		}
	}
}
