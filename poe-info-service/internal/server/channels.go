package server

import (
	"encoding/json"

	"github.com/MovingCairn/poe-info-service/internal/channels"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// handleChannelsRegister backs the "channels.register" method: l2p-poe (or
// any client) telling poe-info-service that a chat channel number carries a
// user-defined label, optionally scoped to a date range. This is the
// replacement for the old --config-path handoff, where poe-info-service
// parsed l2p-poe.toml itself — channel labels are now pushed as data instead
// of a file path to go re-derive them from.
func (s *server) handleChannelsRegister(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Channel   int    `json:"channel"`
		Label     string `json:"label"`
		ValidFrom string `json:"valid_from"`
		ValidTo   string `json:"valid_to"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Label == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: channel and label required"})
		return
	}
	if err := channels.Register(s.db, params.Channel, params.Label, params.ValidFrom, params.ValidTo); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}

// handleChannelsRename backs "channels.rename": relabels an existing
// registration without touching its date range.
func (s *server) handleChannelsRename(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Channel   int    `json:"channel"`
		ValidFrom string `json:"valid_from"`
		ValidTo   string `json:"valid_to"`
		OldLabel  string `json:"old_label"`
		NewLabel  string `json:"new_label"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.OldLabel == "" || params.NewLabel == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: old_label and new_label required"})
		return
	}
	if err := channels.Rename(s.db, params.Channel, params.ValidFrom, params.ValidTo, params.OldLabel, params.NewLabel); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}

// handleChannelsDelete backs "channels.delete": removes one label
// registration. Not part of the l2p-poe startup push — offered for manual
// cleanup of a mis-registered label.
func (s *server) handleChannelsDelete(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Channel   int    `json:"channel"`
		Label     string `json:"label"`
		ValidFrom string `json:"valid_from"`
		ValidTo   string `json:"valid_to"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Label == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: channel and label required"})
		return
	}
	if err := channels.Delete(s.db, params.Channel, params.Label, params.ValidFrom, params.ValidTo); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}
