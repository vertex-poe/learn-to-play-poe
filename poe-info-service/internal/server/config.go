package server

import (
	"encoding/json"
	"fmt"
	"log"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
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

var mutableSettings = map[string]mutableSetting{
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
}

// configSnapshot returns every known config entry (mutable and read-only)
// keyed by its TOML/JSON name, reflecting the server's current effective
// state rather than just what's on disk.
func (s *server) configSnapshot() map[string]configEntry {
	entries := map[string]configEntry{
		"bind": {Value: s.cfg.Bind, Description: "Address the WebSocket server is bound to.", Mutable: false},
		"port": {Value: s.cfg.Port, Description: "TCP port the WebSocket server is listening on.", Mutable: false},
	}
	for key, setting := range mutableSettings {
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

	setting, ok := mutableSettings[params.Key]
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
		Bind:         s.cfg.Bind,
		Port:         s.cfg.Port,
		DebugLogging: s.debugLogging.Load(),
	})
}
