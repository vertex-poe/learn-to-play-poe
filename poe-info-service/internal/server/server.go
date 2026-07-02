package server

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/parser"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/query"
	"github.com/MovingCairn/poe-info-service/internal/store"
	"github.com/MovingCairn/poe-info-service/internal/tailer"
	"github.com/gorilla/websocket"
)

// liveBroadcastTypes are the parsed-event types the old C++ LiveEventBus
// consumed for overlay/alert purposes. Event types outside this set (e.g.
// EventPlayed, EventPassivesSnapshot) are written to the database by the
// ingest writer but never published to the "clientlog" topic.
var liveBroadcastTypes = map[string]bool{
	proto.EventAreaEntered:        true,
	proto.EventLevelUp:            true,
	proto.EventCharacterDeath:     true,
	proto.EventAfkOn:              true,
	proto.EventAfkOff:             true,
	proto.EventWhisper:            true,
	proto.EventChat:               true,
	proto.EventAchievement:        true,
	proto.EventHideoutDiscovered:  true,
	proto.EventPvpQueue:           true,
	proto.EventPvpQueueCancelled:  true,
	proto.EventPassiveAllocated:   true,
	proto.EventPassiveUnallocated: true,
	proto.EventQuestEvent:         true,
	proto.EventGeneralEvent:       true,
	proto.EventSessionStart:       true,
	proto.EventLoginScreen:        true,
	proto.EventCharSelect:         true,
	proto.EventAltTabOut:          true,
	proto.EventAltTabBack:         true,
}

type Config struct {
	Version      string
	StartTime    int64
	Bind         string // default "127.0.0.1"
	Port         int
	CacheDir     string
	InstallDir   string         // PoE install directory; identifies the installs row (matches the old C++ convention)
	LogPath      string         // InstallDir + "/logs/Client.txt"
	DbPath       string         // path to the l2p SQLite database
	ChannelNames map[int]string // chat channel number -> user-defined label
}

type server struct {
	cfg      Config
	hub      *hub.Hub
	store    *store.Store
	queryDB  *query.DB
	started  time.Time
	shutdown context.CancelFunc
	upgrader websocket.Upgrader
}

// Run negotiates singleton ownership, then either starts serving or yields to
// the existing instance.
func Run(cfg Config) error {
	if cfg.Bind == "" {
		cfg.Bind = "127.0.0.1"
	}
	addr := fmt.Sprintf("%s:%d", cfg.Bind, cfg.Port)

	dialer := websocket.Dialer{HandshakeTimeout: 2 * time.Second}
	conn, _, err := dialer.Dial("ws://"+addr+"/ws", nil)
	if err != nil {
		log.Printf("no incumbent at %s (will bind): %v", addr, err)
	} else {
		shouldTakeOver, incumbentVer, negErr := negotiate(conn, cfg)
		conn.Close()
		if negErr != nil {
			log.Printf("negotiation error: %v; assuming no healthy incumbent", negErr)
		} else if !shouldTakeOver {
			log.Printf("existing service v%s is the authority; exiting", incumbentVer)
			return nil
		} else {
			log.Printf("requested step-down from v%s; waiting for port release", incumbentVer)
			time.Sleep(2 * time.Second)
		}
	}

	// Bind the port, retrying to give the incumbent time to release it.
	var listener net.Listener
	for attempt := range 10 {
		listener, err = net.Listen("tcp", addr)
		if err == nil {
			break
		}
		log.Printf("bind attempt %d/10: %v", attempt+1, err)
		time.Sleep(500 * time.Millisecond)
	}
	if err != nil {
		return fmt.Errorf("cannot bind %s after retries: %w", addr, err)
	}

	return serve(cfg, listener)
}

// negotiate sends our hello to the incumbent and returns whether we should take
// over. incumbentVer is always populated on a clean exchange.
func negotiate(conn *websocket.Conn, cfg Config) (shouldTakeOver bool, incumbentVer string, err error) {
	hello, _ := json.Marshal(proto.Message{
		Type:    proto.TypeHello,
		Payload: mustMarshal(proto.HelloPayload{Version: cfg.Version, StartTime: cfg.StartTime}),
	})
	if err = conn.WriteMessage(websocket.TextMessage, hello); err != nil {
		return false, "", err
	}

	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	_, data, err := conn.ReadMessage()
	if err != nil {
		return false, "", err
	}

	var reply proto.Message
	if err = json.Unmarshal(data, &reply); err != nil {
		return false, "", err
	}
	if reply.Type != proto.TypeHello {
		return false, "", fmt.Errorf("expected hello from incumbent, got %q", reply.Type)
	}

	var incumbentHello proto.HelloPayload
	if err = json.Unmarshal(reply.Payload, &incumbentHello); err != nil {
		return false, "", err
	}

	cmp := compareVersions(cfg.Version, incumbentHello.Version)
	isBetter := cmp > 0 || (cmp == 0 && cfg.StartTime < incumbentHello.StartTime)

	if isBetter {
		stepDown, _ := json.Marshal(proto.Message{Type: proto.TypeStepDown})
		conn.WriteMessage(websocket.TextMessage, stepDown)
		return true, incumbentHello.Version, nil
	}
	return false, incumbentHello.Version, nil
}

func serve(cfg Config, listener net.Listener) error {
	st, err := store.Open(cfg.CacheDir)
	if err != nil {
		return fmt.Errorf("open store: %w", err)
	}

	var qdb *query.DB
	if cfg.DbPath != "" {
		qdb, err = query.Open(cfg.DbPath)
		if err != nil {
			log.Printf("warn: cannot open l2p db %q: %v", cfg.DbPath, err)
		} else {
			log.Printf("opened l2p db %q", cfg.DbPath)
		}
	} else {
		log.Printf("no db path configured; log queries will fail")
	}

	h := hub.New()
	ctx, cancel := context.WithCancel(context.Background())

	if cfg.LogPath != "" && cfg.InstallDir != "" && qdb != nil {
		// installs.path stores the install directory (not the Client.txt path),
		// matching the convention the old C++ Database::upsertInstall used.
		installID, err := ingest.EnsureInstall(qdb.Raw(), cfg.InstallDir)
		if err != nil {
			log.Printf("warn: cannot ensure install row for %q: %v", cfg.InstallDir, err)
		} else {
			writer, err := ingest.NewWriter(qdb.Raw(), installID, cfg.ChannelNames)
			if err != nil {
				log.Printf("warn: cannot start ingest writer: %v", err)
			} else {
				eventCh := make(chan string, 512)
				t := tailer.New(cfg.LogPath, qdb.Raw(), installID, eventCh)
				p := parser.New()
				go t.Run(ctx)
				go broadcastLogEvents(ctx, h, eventCh, p, writer, t.CaughtUp)
			}
		}
	}

	srv := &server{
		cfg:      cfg,
		hub:      h,
		store:    st,
		queryDB:  qdb,
		started:  time.Now(),
		shutdown: cancel,
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool { return true },
		},
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", srv.handleWS)
	mux.HandleFunc("/health", srv.handleHealth)

	httpSrv := &http.Server{Handler: mux}
	log.Printf("poe-info-service v%s listening on %s", cfg.Version, listener.Addr())

	go func() {
		<-ctx.Done()
		log.Println("shutting down...")
		st.Checkpoint()
		httpSrv.Shutdown(context.Background())
		if qdb != nil {
			qdb.Close()
		}
		st.Close()
	}()

	if err := httpSrv.Serve(listener); err != http.ErrServerClosed {
		cancel()
		return err
	}
	cancel()
	return nil
}

// broadcastLogEvents drains parsed Client.txt lines, applying each event to
// the database via w and then — only for overlay-relevant event types, and
// only once the tailer has caught up to EOF at least once this run —
// publishing it to the "clientlog" hub topic. The caughtUp gate prevents
// every service restart from replaying the whole backlog as "live" events.
func broadcastLogEvents(ctx context.Context, h *hub.Hub, eventCh <-chan string, p *parser.Parser, w *ingest.Writer, caughtUp func() bool) {
	for {
		select {
		case line, ok := <-eventCh:
			if !ok {
				return
			}
			for _, evt := range p.ParseLine(line) {
				if err := w.HandleEvent(evt); err != nil {
					log.Printf("ingest: failed to apply %s event: %v", evt.Type, err)
				}

				if !liveBroadcastTypes[evt.Type] || !caughtUp() {
					continue
				}
				msg, _ := json.Marshal(proto.Message{
					Type:    proto.TypeEvent,
					Topic:   "clientlog",
					Payload: mustMarshal(evt),
				})
				h.Publish("clientlog", msg)
			}
		case <-ctx.Done():
			return
		}
	}
}

func (s *server) handleWS(w http.ResponseWriter, r *http.Request) {
	conn, err := s.upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("upgrade: %v", err)
		return
	}
	defer conn.Close()

	c := hub.NewClient()
	defer func() {
		c.Close()
		s.hub.UnsubscribeAll(c)
	}()

	// Writer goroutine drains the send channel to the WebSocket.
	go func() {
		for {
			select {
			case msg := <-c.Send:
				if err := conn.WriteMessage(websocket.TextMessage, msg); err != nil {
					conn.Close()
					return
				}
			case <-c.Done():
				return
			}
		}
	}()

	// Read the first message to determine whether this is a peer (service
	// negotiation) or a regular addon client.
	_, data, err := conn.ReadMessage()
	if err != nil {
		return
	}
	var first proto.Message
	if err := json.Unmarshal(data, &first); err != nil {
		return
	}

	if first.Type == proto.TypeHello {
		s.handlePeer(conn, first)
		return
	}

	log.Printf("client connected from %s, first message type=%q", r.RemoteAddr, first.Type)
	s.routeMessage(c, first)
	for {
		_, data, err := conn.ReadMessage()
		if err != nil {
			log.Printf("client %s disconnected: %v", r.RemoteAddr, err)
			return
		}
		var msg proto.Message
		if err := json.Unmarshal(data, &msg); err != nil {
			continue
		}
		s.routeMessage(c, msg)
	}
}

// handlePeer processes a version-negotiation connection from another service instance.
func (s *server) handlePeer(conn *websocket.Conn, peerHello proto.Message) {
	var peerPayload proto.HelloPayload
	if err := json.Unmarshal(peerHello.Payload, &peerPayload); err != nil {
		return
	}

	reply, _ := json.Marshal(proto.Message{
		Type:    proto.TypeHello,
		Payload: mustMarshal(proto.HelloPayload{Version: s.cfg.Version, StartTime: s.cfg.StartTime}),
	})
	if err := conn.WriteMessage(websocket.TextMessage, reply); err != nil {
		return
	}

	conn.SetReadDeadline(time.Now().Add(5 * time.Second))
	_, data, err := conn.ReadMessage()
	if err != nil {
		return
	}
	var decision proto.Message
	if err := json.Unmarshal(data, &decision); err != nil {
		return
	}

	if decision.Type == proto.TypeStepDown {
		log.Printf("peer v%s requested step-down", peerPayload.Version)
		notice, _ := json.Marshal(proto.Message{
			Type:    proto.TypeEvent,
			Topic:   "system",
			Payload: mustMarshal(map[string]string{"type": "shutdown", "reason": "version-upgrade"}),
		})
		s.hub.Publish("system", notice)
		go s.shutdown()
	}
}

func (s *server) routeMessage(c *hub.Client, msg proto.Message) {
	switch msg.Type {
	case proto.TypePing:
		s.send(c, proto.Message{Type: proto.TypePong, ID: msg.ID})

	case proto.TypeSubscribe:
		if msg.Topic != "" {
			s.hub.Subscribe(c, msg.Topic)
			s.send(c, proto.Message{
				Type:    proto.TypeResponse,
				ID:      msg.ID,
				Payload: mustMarshal(map[string]bool{"subscribed": true}),
			})
		}

	case proto.TypeUnsubscribe:
		if msg.Topic != "" {
			s.hub.Unsubscribe(c, msg.Topic)
		}

	case proto.TypeRequest:
		s.handleRequest(c, msg)
	}
}

func (s *server) handleRequest(c *hub.Client, msg proto.Message) {
	switch msg.Method {
	case "ping":
		s.send(c, proto.Message{
			Type:    proto.TypeResponse,
			ID:      msg.ID,
			Payload: mustMarshal(map[string]string{"pong": "ok"}),
		})

	case "status":
		val, _, _ := s.store.GetState("log_offset")
		var logOffset int64
		fmt.Sscanf(val, "%d", &logOffset)
		s.send(c, proto.Message{
			Type: proto.TypeResponse,
			ID:   msg.ID,
			Payload: mustMarshal(proto.StatusPayload{
				Version:   s.cfg.Version,
				StartTime: s.cfg.StartTime,
				LogPath:   s.cfg.LogPath,
				LogOffset: logOffset,
				Uptime:    time.Since(s.started).Round(time.Second).String(),
			}),
		})

	case "chat.messages":
		s.handleChatMessages(c, msg)

	case "dm.messages":
		s.handleDmMessages(c, msg)

	case "log.sessions":
		s.handleLogSessions(c, msg)

	case "log.session":
		s.handleLogSession(c, msg)

	case "log.zones":
		s.handleLogZones(c, msg)

	case "chat.dates":
		s.handleChatDates(c, msg)

	case "dm.partners":
		s.handleDmPartners(c, msg)

	case "sessions.closeOrphans":
		s.handleCloseOrphanSessions(c, msg)

	default:
		s.send(c, proto.Message{
			Type:  proto.TypeResponse,
			ID:    msg.ID,
			Error: "unknown method: " + msg.Method,
		})
	}
}

func (s *server) handleChatMessages(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Channels   []string `json:"channels"`
		IncludeDMs bool     `json:"include_dms"`
		Limit      int      `json:"limit"`
		Offset     int      `json:"offset"`
		FromDate   string   `json:"from_date"`
		ToDate     string   `json:"to_date"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	log.Printf("chat.messages request id=%s channels=%v includeDms=%v limit=%d offset=%d from=%q to=%q",
		msg.ID, params.Channels, params.IncludeDMs, params.Limit, params.Offset, params.FromDate, params.ToDate)
	records, err := s.queryDB.FetchChats(params.Channels, params.IncludeDMs, params.Limit, params.Offset, params.FromDate, params.ToDate)
	if err != nil {
		log.Printf("chat.messages error id=%s: %v", msg.ID, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	log.Printf("chat.messages ok id=%s: %d records", msg.ID, len(records))
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"records": records}),
	})
}

func (s *server) handleDmMessages(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		PlayerFilter string `json:"player_filter"`
		Limit        int    `json:"limit"`
		Offset       int    `json:"offset"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	records, err := s.queryDB.FetchWhispers(params.PlayerFilter, params.Limit, params.Offset)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"records": records}),
	})
}

func (s *server) handleLogSessions(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Limit  int `json:"limit"`
		Offset int `json:"offset"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	records, err := s.queryDB.FetchSessions(params.Limit, params.Offset)
	if err != nil {
		log.Printf("log.sessions error (limit=%d offset=%d): %v", params.Limit, params.Offset, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	log.Printf("log.sessions ok: %d records (limit=%d offset=%d)", len(records), params.Limit, params.Offset)
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"records": records}),
	})
}

func (s *server) handleLogSession(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		SessionID         int64 `json:"session_id"`
		ZoneLimit         int   `json:"zone_limit"`
		SessionEventLimit int   `json:"session_event_limit"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	data, err := s.queryDB.FetchSessionPageData(params.SessionID, params.SessionEventLimit, params.ZoneLimit)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(data),
	})
}

func (s *server) handleLogZones(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		SessionID int64 `json:"session_id"`
		Limit     int   `json:"limit"`
		Offset    int   `json:"offset"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	zones, err := s.queryDB.FetchZoneTransitions(params.SessionID, params.Limit, params.Offset)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"zones": zones}),
	})
}

func (s *server) handleChatDates(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		Channels   []string `json:"channels"`
		IncludeDMs bool     `json:"include_dms"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	dates, err := s.queryDB.FetchChatDates(params.Channels, params.IncludeDMs)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	if dates == nil {
		dates = []string{}
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"dates": dates}),
	})
}

func (s *server) handleDmPartners(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	partners, err := s.queryDB.FetchWhisperPartnersWithDates()
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	if partners == nil {
		partners = []query.PartnerRecord{}
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"partners": partners}),
	})
}

func (s *server) handleCloseOrphanSessions(c *hub.Client, msg proto.Message) {
	if s.queryDB == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no db configured"})
		return
	}
	var params struct {
		RunningInstallPaths []string `json:"running_install_paths"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
		return
	}
	closed, err := ingest.CloseOrphanSessions(s.queryDB.Raw(), params.RunningInstallPaths)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]any{"closed": closed}),
	})
}

func (s *server) handleHealth(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("Content-Type", "application/json")
	json.NewEncoder(w).Encode(map[string]string{
		"status":  "ok",
		"version": s.cfg.Version,
		"uptime":  time.Since(s.started).Round(time.Second).String(),
	})
}

func (s *server) send(c *hub.Client, msg proto.Message) {
	data, _ := json.Marshal(msg)
	select {
	case c.Send <- data:
	case <-c.Done():
	default:
		log.Printf("server: client buffer full, dropping response")
	}
}

func mustMarshal(v any) json.RawMessage {
	data, _ := json.Marshal(v)
	return data
}

func compareVersions(a, b string) int {
	pa := parseVersion(a)
	pb := parseVersion(b)
	for i := range 3 {
		if pa[i] != pb[i] {
			if pa[i] > pb[i] {
				return 1
			}
			return -1
		}
	}
	return 0
}

func parseVersion(v string) [3]int {
	var major, minor, patch int
	fmt.Sscanf(v, "%d.%d.%d", &major, &minor, &patch)
	return [3]int{major, minor, patch}
}
