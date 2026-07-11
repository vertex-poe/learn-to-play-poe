package server

import (
	"encoding/json"
	"fmt"
	"log"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/steam"
)

// configEntry describes one entry in the config.list/config.get response: its
// current effective value, a human-facing description (mirroring the
// generated TOML comment), and whether config.set will accept writes to it.
type configEntry struct {
	Value       any    `json:"value"`
	Description string `json:"description"`
	Mutable     bool   `json:"mutable"`
}

// mutableSetting is a client-settable entry in poe-info-service.toml: get
// reads the live in-memory value, set validates and applies a new one (both
// updating server state and persisting to disk). Bind/Port are deliberately
// not in this registry — changing either live wouldn't take effect without
// rebinding the listener, so they're read-only over the WebSocket API today;
// changing them requires editing the file directly and restarting.
type mutableSetting struct {
	description string
	get         func(s *server) any
	set         func(s *server, raw json.RawMessage) error
}

// mutableSettingsRegistry builds the mutable-setting registry. It's a
// function rather than a package-level var because the
// "auto_detect_install_dir" entry's set closure calls watchAutoDetect,
// which (via publishConfigChanged -> configSnapshot) refers back to this
// registry — Go's package-initialization-cycle check treats any identifier
// referenced inside a var initializer's closures as a dependency even if
// never invoked during init, so a package-level var here would be a
// (spurious, since none of this runs at init time) cycle. A function
// sidesteps that check entirely; the map itself is cheap to rebuild per call.
func mutableSettingsRegistry() map[string]mutableSetting {
	return map[string]mutableSetting{
		"debug_logging": {
			description: "Enable verbose debug logging.",
			get:         func(s *server) any { return s.debugLogging.Load() },
			set: func(s *server, raw json.RawMessage) error {
				var v bool
				if err := json.Unmarshal(raw, &v); err != nil {
					return fmt.Errorf("debug_logging must be a boolean")
				}
				s.debugLogging.Store(v)
				return nil
			},
		},
		"install_dirs": {
			description: "PoE install directories to ingest Client.txt from.",
			get:         func(s *server) any { return s.installDirsList() },
			set: func(s *server, raw json.RawMessage) error {
				var want []string
				if err := json.Unmarshal(raw, &want); err != nil {
					return fmt.Errorf("install_dirs must be an array of strings")
				}
				return s.reconcileInstallDirs(want)
			},
		},
		"auto_detect_install_dir": {
			description: "Automatically add an install directory when a matching game process is seen running.",
			get:         func(s *server) any { return s.autoDetect.Load() },
			set: func(s *server, raw json.RawMessage) error {
				var v bool
				if err := json.Unmarshal(raw, &v); err != nil {
					return fmt.Errorf("auto_detect_install_dir must be a boolean")
				}
				wasOn := s.autoDetect.Swap(v)
				if v && !wasOn {
					go watchAutoDetect(s.rootCtx, s)
				}
				return nil
			},
		},
		"executable_names": {
			description: "Executable basenames to look for when auto-detecting an install directory.",
			get:         func(s *server) any { return s.currentExecutableNames() },
			set: func(s *server, raw json.RawMessage) error {
				var v []string
				if err := json.Unmarshal(raw, &v); err != nil {
					return fmt.Errorf("executable_names must be an array of strings")
				}
				s.executableNames.Store(v)
				return nil
			},
		},
		"steam_ids": {
			description: "Steam64 IDs to track combined official + rich-presence data for (see the steam.presence WS method).",
			get:         func(s *server) any { return s.currentSteamIDs() },
			set: func(s *server, raw json.RawMessage) error {
				var v []string
				if err := json.Unmarshal(raw, &v); err != nil {
					return fmt.Errorf("steam_ids must be an array of strings")
				}
				for _, id := range v {
					if _, err := steam.ValidateSteamID64(id); err != nil {
						return fmt.Errorf("steam_ids: %w", err)
					}
				}
				s.steamIDs.Store(v)
				return nil
			},
		},
	}
}

// configSnapshot returns every known config entry (mutable and read-only)
// keyed by its TOML/JSON name, reflecting the server's current effective
// state rather than just what's on disk.
func (s *server) configSnapshot() map[string]configEntry {
	entries := map[string]configEntry{
		"bind": {Value: s.cfg.Bind, Description: "Address the WebSocket server is bound to.", Mutable: false},
		"port": {Value: s.cfg.Port, Description: "TCP port the WebSocket server is listening on.", Mutable: false},
	}
	for key, setting := range mutableSettingsRegistry() {
		entries[key] = configEntry{Value: setting.get(s), Description: setting.description, Mutable: true}
	}
	return entries
}

func (s *server) handleConfigList(c *hub.Client, msg proto.Message) {
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"settings": s.configSnapshot()}),
	})
}

func (s *server) handleConfigGet(c *hub.Client, msg proto.Message) {
	var params struct {
		Key string `json:"key"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Key == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: key required"})
		return
	}
	entry, ok := s.configSnapshot()[params.Key]
	if !ok {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "unknown config key: " + params.Key})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(entry),
	})
}

// handleConfigSet applies a new value to a mutable setting in memory, then
// persists the full effective config to poe-info-service.toml. Per ADR-006,
// this is the only store for user-facing config — nothing is written to the
// database.
func (s *server) handleConfigSet(c *hub.Client, msg proto.Message) {
	var params struct {
		Key   string          `json:"key"`
		Value json.RawMessage `json:"value"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Key == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: key required"})
		return
	}

	setting, ok := mutableSettingsRegistry()[params.Key]
	if !ok {
		s.send(c, proto.Message{
			Type:  proto.TypeResponse,
			ID:    msg.ID,
			Error: params.Key + " is read-only over this API; edit " + s.cfg.ConfigFilePath + " directly and restart",
		})
		return
	}
	if err := setting.set(s, params.Value); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}

	if err := s.persistConfig(); err != nil {
		log.Printf("config.set: persist failed for key=%s: %v", params.Key, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "applied but failed to persist: " + err.Error()})
		return
	}

	log.Printf("config.set: %s = %s", params.Key, string(params.Value))
	s.publishConfigChanged()
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}

// persistConfig writes the server's current effective config to
// cfg.ConfigFilePath.
func (s *server) persistConfig() error {
	return config.Save(s.cfg.ConfigFilePath, config.Config{
		Bind:                 s.cfg.Bind,
		Port:                 s.cfg.Port,
		DebugLogging:         s.debugLogging.Load(),
		InstallDirs:          s.installDirsList(),
		AutoDetectInstallDir: s.autoDetect.Load(),
		ExecutableNames:      s.currentExecutableNames(),
		SteamIDs:             s.currentSteamIDs(),
	})
}

// publishConfigChanged pushes the current config snapshot to every client
// subscribed to proto.TopicConfig — called after any successful config.set
// and after the auto-detect loop adds a dir on its own, so a client never
// has to re-poll config.list to notice a change it didn't initiate itself.
func (s *server) publishConfigChanged() {
	msg, _ := json.Marshal(proto.Message{
		Type:    proto.TypeEvent,
		Topic:   proto.TopicConfig,
		Payload: mustMarshal(map[string]any{"settings": s.configSnapshot()}),
	})
	s.hub.Publish(proto.TopicConfig, msg)
}

// currentExecutableNames returns the effective executable_names setting,
// falling back to the canonical default list if it was never set (should
// only happen if executableNames.Store was somehow skipped at startup).
func (s *server) currentExecutableNames() []string {
	names, _ := s.executableNames.Load().([]string)
	if len(names) == 0 {
		return config.DefaultExecutableNames()
	}
	return names
}

// currentSteamIDs returns the effective steam_ids setting.
func (s *server) currentSteamIDs() []string {
	ids, _ := s.steamIDs.Load().([]string)
	return ids
}

// reconcileInstallDirs diffs want against the currently-tailed install dirs
// and adds/removes tailers for the delta — install_dirs's config.set is a
// whole-list replace from the client's point of view (it always sends its
// complete current list, see l2p-poe's SettingsPage), but under the hood
// that has to translate into starting/stopping only what actually changed.
func (s *server) reconcileInstallDirs(want []string) error {
	// Normalized the same way addInstallTarget normalizes s.tailers' keys,
	// so a config-file path spelled with different separators than what's
	// already tailed doesn't look like a spurious add+remove.
	wantSet := make(map[string]bool, len(want))
	for _, dir := range want {
		if dir != "" {
			wantSet[ingest.NormalizeInstallPath(dir)] = true
		}
	}

	for _, have := range s.installDirsList() {
		if !wantSet[have] {
			s.removeInstall(have)
		}
	}

	var firstErr error
	for dir := range wantSet {
		if err := s.addInstall(dir); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}
