package server

import (
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/gorilla/websocket"

	_ "modernc.org/sqlite"
)

// dialTestServer starts serve() with cfg on a free port and returns a
// connected WS client, following the same startup-poll pattern as
// TestServe_IngestsEveryConfiguredInstall.
func dialTestServer(t *testing.T, cfg Config) *websocket.Conn {
	t.Helper()
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	addr := listener.Addr().String()
	cfg.Version = "test"
	cfg.StartTime = time.Now().Unix()
	if cfg.DbPath == "" {
		cfg.DbPath = filepath.Join(t.TempDir(), "poe-info-service.db")
	}
	if cfg.ConfigFilePath == "" {
		cfg.ConfigFilePath = filepath.Join(t.TempDir(), config.FileName)
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
	t.Cleanup(func() { conn.Close() })
	return conn
}

// wsRequest sends a request over conn and returns its response payload/error.
func wsRequest(t *testing.T, conn *websocket.Conn, method string, payload any) (json.RawMessage, string) {
	t.Helper()
	msg := proto.Message{Type: proto.TypeRequest, ID: method, Method: method, Payload: mustMarshal(payload)}
	data, _ := json.Marshal(msg)
	if err := conn.WriteMessage(websocket.TextMessage, data); err != nil {
		t.Fatalf("write %s request: %v", method, err)
	}
	_, respData, err := conn.ReadMessage()
	if err != nil {
		t.Fatalf("read %s response: %v", method, err)
	}
	var resp proto.Message
	if err := json.Unmarshal(respData, &resp); err != nil {
		t.Fatalf("unmarshal %s response: %v", method, err)
	}
	return resp.Payload, resp.Error
}

// TestConfigList_IncludesInstallSettings proves config.list reports
// install_dirs, auto_detect_install_dir and executable_names as mutable
// entries reflecting the server's starting configuration — the shape
// l2p-poe's Settings > Game page now depends on to render itself as a thin
// proxy for poe-info-service's own config (see plan: "query for the config
// list ... all at once").
func TestConfigList_IncludesInstallSettings(t *testing.T) {
	dir := t.TempDir()
	conn := dialTestServer(t, Config{
		Installs:             []InstallTarget{{Dir: dir, LogPath: filepath.Join(dir, "logs", "Client.txt")}},
		AutoDetectInstallDir: false,
		ExecutableNames:      []string{"PathOfExile_x64Steam.exe"},
	})

	payload, errStr := wsRequest(t, conn, "config.list", map[string]any{})
	if errStr != "" {
		t.Fatalf("config.list error: %s", errStr)
	}

	var resp struct {
		Settings map[string]configEntry `json:"settings"`
	}
	if err := json.Unmarshal(payload, &resp); err != nil {
		t.Fatalf("unmarshal config.list payload: %v", err)
	}

	installDirs, ok := resp.Settings["install_dirs"]
	if !ok || !installDirs.Mutable {
		t.Fatalf("expected mutable install_dirs entry, got %+v (ok=%v)", installDirs, ok)
	}
	gotDirs, _ := installDirs.Value.([]any)
	if len(gotDirs) != 1 || gotDirs[0] != dir {
		t.Errorf("install_dirs = %v, want [%q]", installDirs.Value, dir)
	}

	autoDetect, ok := resp.Settings["auto_detect_install_dir"]
	if !ok || !autoDetect.Mutable || autoDetect.Value != false {
		t.Errorf("auto_detect_install_dir = %+v (ok=%v), want mutable=true value=false", autoDetect, ok)
	}

	execNames, ok := resp.Settings["executable_names"]
	if !ok || !execNames.Mutable {
		t.Fatalf("expected mutable executable_names entry, got %+v (ok=%v)", execNames, ok)
	}
}

// TestConfigSet_InstallDirs_StartsAndStopsTailers proves config.set on
// install_dirs reconciles the live tailer set (starts a tailer for a newly
// added dir, stops one for a removed dir) rather than requiring a restart,
// and persists the result to poe-info-service.toml.
func TestConfigSet_InstallDirs_StartsAndStopsTailers(t *testing.T) {
	keepDir := t.TempDir()
	dropDir := t.TempDir()
	addDir := t.TempDir()

	configPath := filepath.Join(t.TempDir(), config.FileName)
	conn := dialTestServer(t, Config{
		Installs: []InstallTarget{
			{Dir: keepDir, LogPath: filepath.Join(keepDir, "logs", "Client.txt")},
			{Dir: dropDir, LogPath: filepath.Join(dropDir, "logs", "Client.txt")},
		},
		ConfigFilePath: configPath,
	})

	want := []string{keepDir, addDir}
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "install_dirs", "value": want})
	if errStr != "" {
		t.Fatalf("config.set install_dirs error: %s", errStr)
	}

	payload, errStr := wsRequest(t, conn, "config.get", map[string]any{"key": "install_dirs"})
	if errStr != "" {
		t.Fatalf("config.get install_dirs error: %s", errStr)
	}
	var entry configEntry
	if err := json.Unmarshal(payload, &entry); err != nil {
		t.Fatalf("unmarshal config.get payload: %v", err)
	}
	gotAny, _ := entry.Value.([]any)
	got := make([]string, len(gotAny))
	for i, v := range gotAny {
		got[i] = v.(string)
	}
	if !sameSet(got, want) {
		t.Errorf("install_dirs after config.set = %v, want %v", got, want)
	}

	// Persisted to disk, matching ADR-006 (config.set persists immediately).
	data, err := os.ReadFile(configPath)
	if err != nil {
		t.Fatalf("read persisted config: %v", err)
	}
	for _, dir := range want {
		if !strings.Contains(string(data), dir) {
			t.Errorf("persisted config missing %q:\n%s", dir, data)
		}
	}
	if strings.Contains(string(data), dropDir) {
		t.Errorf("persisted config still lists removed dir %q:\n%s", dropDir, data)
	}
}

// TestConfigSet_InstallDirs_RejectsNonArray proves a malformed value is
// rejected rather than silently accepted or crashing the server.
func TestConfigSet_InstallDirs_RejectsNonArray(t *testing.T) {
	conn := dialTestServer(t, Config{})

	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "install_dirs", "value": "not-an-array"})
	if errStr == "" {
		t.Fatal("expected an error for a non-array install_dirs value, got none")
	}
}

func sameSet(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	set := make(map[string]bool, len(a))
	for _, v := range a {
		set[v] = true
	}
	for _, v := range b {
		if !set[v] {
			return false
		}
	}
	return true
}
