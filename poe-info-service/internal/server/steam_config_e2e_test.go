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

// TestConfigList_IncludesSteamIDs proves config.list reports steam_ids as a
// mutable entry, mirroring executable_names/install_dirs.
func TestConfigList_IncludesSteamIDs(t *testing.T) {
	conn := dialTestServer(t, Config{SteamIDs: []string{"76561197960287930"}})

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

	entry, ok := resp.Settings["steam_ids"]
	if !ok || !entry.Mutable {
		t.Fatalf("expected mutable steam_ids entry, got %+v (ok=%v)", entry, ok)
	}
	got, _ := entry.Value.([]any)
	if len(got) != 1 || got[0] != "76561197960287930" {
		t.Errorf("steam_ids = %v, want [76561197960287930]", entry.Value)
	}
}

// TestConfigSet_SteamIDs_RejectsInvalidID proves a non-numeric or
// below-base-offset id is rejected at config.set time, rather than being
// accepted and only failing later inside a poll cycle.
func TestConfigSet_SteamIDs_RejectsInvalidID(t *testing.T) {
	tests := []struct {
		name string
		ids  []string
	}{
		{name: "non-numeric", ids: []string{"not-a-number"}},
		{name: "below base offset", ids: []string{"1"}},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			conn := dialTestServer(t, Config{})
			_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_ids", "value": tt.ids})
			if errStr == "" {
				t.Fatalf("config.set steam_ids=%v: want error, got none", tt.ids)
			}
		})
	}
}

// TestConfigSet_SteamIDs_RejectsNonArray mirrors
// TestConfigSet_InstallDirs_RejectsNonArray.
func TestConfigSet_SteamIDs_RejectsNonArray(t *testing.T) {
	conn := dialTestServer(t, Config{})
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_ids", "value": "not-an-array"})
	if errStr == "" {
		t.Fatal("expected an error for a non-array steam_ids value, got none")
	}
}

// TestConfigSet_SteamIDs_PersistsAndRoundTrips proves a valid steam_ids
// config.set is both reflected immediately in config.get and written to
// disk, matching install_dirs' persistence contract (ADR-006).
func TestConfigSet_SteamIDs_PersistsAndRoundTrips(t *testing.T) {
	configPath := filepath.Join(t.TempDir(), config.FileName)
	conn := dialTestServer(t, Config{ConfigFilePath: configPath})

	want := []string{"76561197960287930", "76561197960287931"}
	_, errStr := wsRequest(t, conn, "config.set", map[string]any{"key": "steam_ids", "value": want})
	if errStr != "" {
		t.Fatalf("config.set steam_ids error: %s", errStr)
	}

	payload, errStr := wsRequest(t, conn, "config.get", map[string]any{"key": "steam_ids"})
	if errStr != "" {
		t.Fatalf("config.get steam_ids error: %s", errStr)
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
	if len(got) != len(want) || got[0] != want[0] || got[1] != want[1] {
		t.Errorf("steam_ids after config.set = %v, want %v", got, want)
	}

	data, err := os.ReadFile(configPath)
	if err != nil {
		t.Fatalf("read persisted config: %v", err)
	}
	for _, id := range want {
		if !strings.Contains(string(data), id) {
			t.Errorf("persisted config missing %q:\n%s", id, data)
		}
	}
}

// TestSteamPresence_SynthesizesPendingForUnfetchedIDs proves steam.presence
// returns a "pending" entry for every configured id that hasn't been
// fetched yet (no poll cycle has run because dialTestServer's client never
// subscribes to TopicSteamPresence) — never an empty response or an error.
func TestSteamPresence_SynthesizesPendingForUnfetchedIDs(t *testing.T) {
	conn := dialTestServer(t, Config{SteamIDs: []string{"76561197960287930", "76561197960287931"}})

	payload, errStr := wsRequest(t, conn, "steam.presence", map[string]any{})
	if errStr != "" {
		t.Fatalf("steam.presence error: %s", errStr)
	}
	var resp proto.SteamPresencePayload
	if err := json.Unmarshal(payload, &resp); err != nil {
		t.Fatalf("unmarshal steam.presence payload: %v", err)
	}
	if len(resp.Entries) != 2 {
		t.Fatalf("steam.presence entries = %v, want 2", resp.Entries)
	}
	for _, e := range resp.Entries {
		if e.Status != proto.SteamPresenceStatusPending {
			t.Errorf("entry %+v: status = %q, want %q", e, e.Status, proto.SteamPresenceStatusPending)
		}
	}
}
