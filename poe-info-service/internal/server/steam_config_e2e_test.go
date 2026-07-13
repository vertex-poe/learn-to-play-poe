package server

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// TestConfigList_IncludesSteamID proves config.list reports steam_id as a
// mutable entry, mirroring executable_names/install_dirs.
func TestConfigList_IncludesSteamID(t *testing.T) {
	conn := dialTestServer(t, Config{SteamID: "76561197960287930"})

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

	entry, ok := resp.Settings["steam_id"]
	if !ok || !entry.Mutable {
		t.Fatalf("expected mutable steam_id entry, got %+v (ok=%v)", entry, ok)
	}
	if entry.Value != "76561197960287930" {
		t.Errorf("steam_id = %v, want 76561197960287930", entry.Value)
	}
}

// TestConfigSet_SteamID_RejectsInvalidID proves a non-numeric or
// below-base-offset id is rejected at config.set time, rather than being
// accepted and only failing later inside a fetch.
func TestConfigSet_SteamID_RejectsInvalidID(t *testing.T) {
	tests := []struct {
		name string
		id   string
	}{
		{name: "non-numeric", id: "not-a-number"},
		{name: "below base offset", id: "1"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			conn := dialTestServer(t, Config{})
			_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_id", "value": tt.id})
			if errStr == "" {
				t.Fatalf("config.set steam_id=%v: want error, got none", tt.id)
			}
		})
	}
}

// TestConfigSet_SteamID_RejectsNonString mirrors
// TestConfigSet_InstallDirs_RejectsNonArray.
func TestConfigSet_SteamID_RejectsNonString(t *testing.T) {
	conn := dialTestServer(t, Config{})
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_id", "value": []string{"not-a-string"}})
	if errStr == "" {
		t.Fatal("expected an error for a non-string steam_id value, got none")
	}
}

// TestConfigSet_SteamID_AcceptsEmptyToUnset proves an empty string is
// accepted (steam_id is optional — no rich-presence fetch happens without
// one configured, see TestEnsureFreshRichPresence_NoSteamIDConfiguredIsNoop).
func TestConfigSet_SteamID_AcceptsEmptyToUnset(t *testing.T) {
	conn := dialTestServer(t, Config{SteamID: "76561197960287930"})
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_id", "value": ""})
	if errStr != "" {
		t.Fatalf("config.set steam_id=\"\": want no error, got %s", errStr)
	}
}

// TestConfigSet_SteamID_PersistsAndRoundTrips proves a valid steam_id
// config.set is both reflected immediately in config.get and written to
// disk, matching install_dirs' persistence contract (ADR-006).
func TestConfigSet_SteamID_PersistsAndRoundTrips(t *testing.T) {
	configPath := filepath.Join(t.TempDir(), config.FileName)
	conn := dialTestServer(t, Config{ConfigFilePath: configPath})

	const want = "76561197960287930"
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_id", "value": want})
	if errStr != "" {
		t.Fatalf("config.set steam_id error: %s", errStr)
	}

	payload, errStr := wsRequest(t, conn, "config.get", map[string]any{"key": "steam_id"})
	if errStr != "" {
		t.Fatalf("config.get steam_id error: %s", errStr)
	}
	var entry configEntry
	if err := json.Unmarshal(payload, &entry); err != nil {
		t.Fatalf("unmarshal config.get payload: %v", err)
	}
	if entry.Value != want {
		t.Errorf("steam_id after config.set = %v, want %v", entry.Value, want)
	}

	data, err := os.ReadFile(configPath)
	if err != nil {
		t.Fatalf("read persisted config: %v", err)
	}
	if !strings.Contains(string(data), want) {
		t.Errorf("persisted config missing %q:\n%s", want, data)
	}
}

// TestSteamPresence_PendingWithNoSteamIDConfigured proves steam.presence
// returns a "pending" status rather than an empty response or an error when
// no steam_id is configured.
func TestSteamPresence_PendingWithNoSteamIDConfigured(t *testing.T) {
	conn := dialTestServer(t, Config{})

	payload, errStr := wsRequest(t, conn, "steam.presence", map[string]any{})
	if errStr != "" {
		t.Fatalf("steam.presence error: %s", errStr)
	}
	var resp proto.RichPresencePayload
	if err := json.Unmarshal(payload, &resp); err != nil {
		t.Fatalf("unmarshal steam.presence payload: %v", err)
	}
	if resp.Status != proto.RichPresenceStatusPending {
		t.Errorf("steam.presence status = %q, want %q", resp.Status, proto.RichPresenceStatusPending)
	}
}
