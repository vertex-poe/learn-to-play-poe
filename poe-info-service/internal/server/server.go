package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"sync/atomic"
	"time"

	"github.com/MovingCairn/poe-info-service/config"
	"github.com/MovingCairn/poe-info-service/internal/creds"
	"github.com/MovingCairn/poe-info-service/internal/detect"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/parser"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/query"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
	"github.com/MovingCairn/poe-info-service/internal/steam"
	"github.com/MovingCairn/poe-info-service/internal/store"
	"github.com/MovingCairn/poe-info-service/internal/tailer"
	"github.com/gorilla/websocket"
	_ "modernc.org/sqlite"
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

// DefaultIdleTimeout is used when Config.IdleTimeout is unset (zero).
const DefaultIdleTimeout = 5 * time.Minute

// InstallTarget is one PoE install to ingest: a directory (identifies the
// installs row, matching the old C++ convention) paired with the Client.txt
// path to tail — normally Dir + "/logs/Client.txt", but callers may set
// LogPath to something else (see main.go's explicit --log-path override, a
// dev convenience documented in CONTRIBUTING.md).
type InstallTarget struct {
	Dir     string
	LogPath string
}

type Config struct {
	Version              string
	StartTime            int64
	Bind                 string // default "127.0.0.1"
	Port                 int
	ConfigFilePath       string          // exact path to the config file (ADR-006's sole user-facing config store) — may not be named poe-info-service.toml if --config overrode it
	DebugLogging         bool            // initial value of the debug_logging setting, read from poe-info-service.toml/flags at startup
	Installs             []InstallTarget // every install to ingest concurrently at startup — one tailer per entry; callers (main.go) have already filtered out candidates that don't exist on disk. More can be added/removed live afterward via config.set or auto-detection (see server.addInstall/removeInstall).
	AutoDetectInstallDir bool            // initial value of the auto_detect_install_dir setting
	ExecutableNames      []string        // initial value of the executable_names setting, used by the auto-detect loop
	SteamID              string          // initial value of the steam_id setting, used by watchRichPresence
	DbPath               string          // path to poe-info-service's sole SQLite database (game history + internal state)
	IdleTimeout          time.Duration   // shut down after this long with no client keep-alive or Client.txt activity; 0 uses DefaultIdleTimeout
}

// tailerHandle pairs a running tailer with the cancel func for the child
// context it was started with, so a single install can be torn down
// (config.set removing it) without cancelling every other install's tailer.
type tailerHandle struct {
	t       *tailer.Tailer
	logPath string
	cancel  context.CancelFunc
}

type server struct {
	cfg     Config
	hub     *hub.Hub
	db      *sql.DB
	store   *store.Store
	queryDB *query.DB

	rootCtx context.Context // parent for every tailer's child context; canceled on server shutdown

	tailersMu sync.Mutex
	tailers   map[string]*tailerHandle // keyed by install dir; mutated by serve()'s startup loop, config.set, and the auto-detect loop

	autoDetect      atomic.Bool  // current effective auto_detect_install_dir setting; client-settable via config.set
	executableNames atomic.Value // current effective executable_names setting ([]string); client-settable via config.set
	steamID         atomic.Value // current effective steam_id setting (string); client-settable via config.set

	steamClient *steam.Client

	richPresenceMu      sync.RWMutex
	richPresence        richPresenceState // hydrated from store.GetCache at startup, kept current by watchRichPresence
	richPresenceFetchMu sync.Mutex        // serializes fetch attempts and gates richPresenceRequestTTL across every trigger (request, zone transfer, poller)

	poeClient *poe.Client
	poeOAuth  poeOAuthState   // PoE OAuth token + login/refresh state; see poe_oauth.go
	poeQueue  *reqqueue.Queue // rate-limited priority queue for api.pathofexile.com requests (see poe_ratelimit.go, poe_profile.go); a separate instance from any future PoE Legacy API queue, per ROADMAP's "Reusable rate-limited priority request queue"

	started      time.Time
	shutdown     context.CancelFunc
	upgrader     websocket.Upgrader
	lastActivity atomic.Int64 // UnixNano of the last keep-alive from any connected client
	debugLogging atomic.Bool  // current effective debug_logging setting; client-settable via config.set (ADR-006)
}

// touch records a keep-alive from a connected client, per ADR-001. Called
// for an explicit keepalive and for any real usage (subscribe, unsubscribe,
// request) — but deliberately not for a bare ping, which is a connectivity
// check rather than evidence the client still needs the service running.
func (s *server) touch() {
	s.lastActivity.Store(time.Now().UnixNano())
}

// debugf logs a troubleshooting-oriented line, but only while debug_logging
// is enabled (see config.set in config.go, ADR-006) — keeps a normal run's
// log quiet while still letting a user flip on verbose tracing without
// restarting the service.
func (s *server) debugf(format string, args ...any) {
	if s.debugLogging.Load() {
		log.Printf("[debug] "+format, args...)
	}
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

// sqliteDSN builds the DSN shared by openDB and openReadDB. modernc.org/sqlite
// (the driver in use) does NOT support the mattn/go-sqlite3-style shorthand
// params (_journal_mode=, _synchronous=, _busy_timeout=, _foreign_keys=) —
// those are silently ignored as unknown query params, so this DSN previously
// ran in SQLite's default rollback journal + synchronous=FULL mode this whole
// time despite looking correct. The only pragma mechanism this driver
// supports is repeated _pragma=name(value); see modernc.org/sqlite's
// Driver.Open doc comment.
func sqliteDSN(path string) string {
	return path + "?_pragma=journal_mode(WAL)&_pragma=synchronous(NORMAL)&_pragma=busy_timeout(5000)&_pragma=foreign_keys(1)"
}

// openDB opens poe-info-service's sole SQLite database file for writes: the
// store package and all ingest writes (installs/writer/tailer bookkeeping)
// share this single connection, so writes serialize instead of racing each
// other — see the comment on serve()'s addInstallTarget loop. Creates the
// containing directory if it doesn't exist yet — --data-dir may point
// somewhere that's never been used before.
func openDB(path string) (*sql.DB, error) {
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		return nil, fmt.Errorf("create %q: %w", filepath.Dir(path), err)
	}
	db, err := sql.Open("sqlite", sqliteDSN(path))
	if err != nil {
		return nil, fmt.Errorf("open %q: %w", path, err)
	}
	db.SetMaxOpenConns(1) // single connection so ingest writes always serialize
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, fmt.Errorf("ping %q: %w", path, err)
	}
	return db, nil
}

// openReadDB opens a second connection pool to the same database file,
// dedicated to the query package's read-only client requests (log.session,
// log.sessions, chat.fetch, etc.). WAL mode (set via sqliteDSN, applies at
// the file level) lets any number of readers proceed concurrently with the
// single writer from openDB without blocking on it — before this split, a
// live session's "log.session" query shared openDB's one connection with
// ingest writes and could error out (busy_timeout exceeded) while a write
// batch held it, leaving the session log view stuck empty until some
// unrelated event retried the fetch.
func openReadDB(path string) (*sql.DB, error) {
	db, err := sql.Open("sqlite", sqliteDSN(path))
	if err != nil {
		return nil, fmt.Errorf("open %q: %w", path, err)
	}
	db.SetMaxOpenConns(readDBMaxConns)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, fmt.Errorf("ping %q: %w", path, err)
	}
	return db, nil
}

// readDBMaxConns bounds openReadDB's pool. SQLite WAL readers are cheap and
// don't contend with each other or the writer, so this just needs to be
// comfortably above the number of concurrent client read requests expected
// in practice, not tuned precisely.
const readDBMaxConns = 4

func serve(cfg Config, listener net.Listener) error {
	if cfg.DbPath == "" {
		return fmt.Errorf("no db path configured")
	}
	db, err := openDB(cfg.DbPath)
	if err != nil {
		return fmt.Errorf("open db: %w", err)
	}
	readDB, err := openReadDB(cfg.DbPath)
	if err != nil {
		db.Close()
		return fmt.Errorf("open read db: %w", err)
	}
	log.Printf("opened poe-info-service database %q", cfg.DbPath)

	st, err := store.New(db)
	if err != nil {
		return fmt.Errorf("init store: %w", err)
	}
	qdb, err := query.New(readDB)
	if err != nil {
		return fmt.Errorf("init query db: %w", err)
	}

	h := hub.New()
	ctx, cancel := context.WithCancel(context.Background())

	// Constructed before the ingest pipeline below so its debugLogging flag
	// exists in time to be threaded into broadcastLogEvents; tailer is filled
	// in afterward if a log path/install dir is configured.
	srv := &server{
		cfg:      cfg,
		hub:      h,
		db:       db,
		store:    st,
		queryDB:  qdb,
		rootCtx:  ctx,
		tailers:  make(map[string]*tailerHandle),
		started:  time.Now(),
		shutdown: cancel,
		upgrader: websocket.Upgrader{
			CheckOrigin: func(r *http.Request) bool { return true },
		},
	}
	srv.touch()
	srv.debugLogging.Store(cfg.DebugLogging)
	srv.autoDetect.Store(cfg.AutoDetectInstallDir)
	execNames := cfg.ExecutableNames
	if len(execNames) == 0 {
		execNames = config.DefaultExecutableNames()
	}
	srv.executableNames.Store(execNames)
	srv.steamID.Store(cfg.SteamID)
	srv.steamClient = steam.NewClient(nil)
	srv.hydrateRichPresenceCache()

	srv.poeClient = poe.NewClient(nil, poe.WithVersion(cfg.Version))
	srv.poeQueue = reqqueue.New(ctx, poeOAuthRateLimitHeaders)
	srv.hydratePoeOAuthToken()

	// One tailer per configured install, ingesting concurrently — each gets
	// its own installs row, writer, and event channel, all independent of
	// the others (see ingest.EnsureInstall/NewWriter, both keyed by
	// installID). They share openDB's single write connection (SetMaxOpenConns(1)
	// above), so writes across installs serialize, but broadcastLogEvents'
	// timer-based batch commit avoids the deadlock that a naive
	// channel-emptiness check would risk under that contention (see
	// batchFlushIdle's doc comment) regardless of how many goroutines share it.
	for _, inst := range cfg.Installs {
		if inst.LogPath == "" || inst.Dir == "" {
			continue
		}
		if err := srv.addInstallTarget(inst); err != nil {
			log.Printf("warn: cannot start ingest for %q: %v", inst.Dir, err)
		}
	}
	go watchIngestStatus(ctx, srv)
	go watchRichPresence(ctx, srv)

	idleTimeout := cfg.IdleTimeout
	if idleTimeout <= 0 {
		idleTimeout = DefaultIdleTimeout
	}
	tailerActivity := func() time.Time {
		var latest time.Time
		for _, t := range srv.tailerSnapshot() {
			if la := t.LastActivity(); la.After(latest) {
				latest = la
			}
		}
		return latest
	}
	go watchIdle(ctx, cancel, srv, tailerActivity, idleTimeout, idleCheckInterval)

	if srv.autoDetect.Load() {
		go watchAutoDetect(ctx, srv)
	}

	mux := http.NewServeMux()
	mux.HandleFunc("/ws", srv.handleWS)
	mux.HandleFunc("/health", srv.handleHealth)

	httpSrv := &http.Server{Handler: mux}
	log.Printf("poe-info-service v%s listening on %s, idle timeout %s", cfg.Version, listener.Addr(), idleTimeout)

	go func() {
		<-ctx.Done()
		log.Println("shutting down...")
		st.Checkpoint()
		httpSrv.Shutdown(context.Background())
		db.Close()
		readDB.Close()
	}()

	if err := httpSrv.Serve(listener); err != http.ErrServerClosed {
		cancel()
		return err
	}
	cancel()
	return nil
}

// addInstallTarget starts ingesting inst if it isn't already being tailed:
// ensures its installs row, starts its writer/tailer/broadcast pipeline (the
// same pipeline serve()'s startup loop always ran inline before install
// dirs could change at runtime), and records it in s.tailers under a child
// context of s.rootCtx so it can be torn down individually later via
// removeInstall, without cancelling every other install's tailer.
func (s *server) addInstallTarget(inst InstallTarget) error {
	// Normalize before using Dir as a dedup key / installs.path — see
	// ingest.NormalizeInstallPath's doc comment for why two spellings of the
	// same directory (e.g. forward-slash from config vs. backslash from
	// Windows auto-detection) previously compared unequal here, tailing the
	// same Client.txt twice and duplicating every session.
	inst.Dir = ingest.NormalizeInstallPath(inst.Dir)
	inst.LogPath = ingest.NormalizeInstallPath(inst.LogPath)

	s.tailersMu.Lock()
	if _, exists := s.tailers[inst.Dir]; exists {
		s.tailersMu.Unlock()
		return nil
	}
	s.tailersMu.Unlock()

	// installs.path stores the install directory (not the Client.txt path),
	// matching the convention the old C++ Database::upsertInstall used.
	// These all write, so they use s.db (the write pool) rather than
	// s.queryDB's read pool — see openDB/openReadDB's doc comments.
	installID, err := ingest.EnsureInstall(s.db, inst.Dir)
	if err != nil {
		return fmt.Errorf("ensure install row for %q: %w", inst.Dir, err)
	}
	writer, err := ingest.NewWriter(s.db, installID)
	if err != nil {
		return fmt.Errorf("start ingest writer for %q: %w", inst.Dir, err)
	}

	eventCh := make(chan string, 512)
	t := tailer.New(inst.LogPath, s.db, installID, eventCh)
	p := parser.New()
	tailerCtx, cancel := context.WithCancel(s.rootCtx)

	s.tailersMu.Lock()
	s.tailers[inst.Dir] = &tailerHandle{t: t, logPath: inst.LogPath, cancel: cancel}
	s.tailersMu.Unlock()

	log.Printf("ingest: starting backlog replay for %q (install %q)", inst.LogPath, inst.Dir)
	go t.Run(tailerCtx)
	go broadcastLogEvents(tailerCtx, s.hub, eventCh, p, writer, s.db, t.CaughtUp, s.debugLogging.Load, s.onAreaEntered(inst))
	return nil
}

// addInstall is addInstallTarget for a bare directory (auto-detection and
// config.set's install_dirs handler, neither of which has an explicit
// --log-path override to preserve) — Client.txt's location is always
// derived the standard way.
func (s *server) addInstall(dir string) error {
	return s.addInstallTarget(InstallTarget{Dir: dir, LogPath: filepath.Join(dir, "logs", "Client.txt")})
}

// removeInstall stops tailing dir, if it's currently being tailed. The
// installs row and any ingested history are left in place — this only
// stops watching for new lines.
func (s *server) removeInstall(dir string) {
	dir = ingest.NormalizeInstallPath(dir)
	s.tailersMu.Lock()
	defer s.tailersMu.Unlock()
	h, ok := s.tailers[dir]
	if !ok {
		return
	}
	h.cancel()
	delete(s.tailers, dir)
	log.Printf("ingest: stopped tailing %q", dir)
}

// tailerSnapshot returns every currently-tailed install's *tailer.Tailer, a
// stable slice safe to range over without holding tailersMu — used by
// read-only aggregation (aggregateProgress, tailerActivity) that would
// otherwise need to hold the lock for the duration of a callback.
func (s *server) tailerSnapshot() []*tailer.Tailer {
	s.tailersMu.Lock()
	defer s.tailersMu.Unlock()
	out := make([]*tailer.Tailer, 0, len(s.tailers))
	for _, h := range s.tailers {
		out = append(out, h.t)
	}
	return out
}

// installDirsList returns every currently-tailed install dir, sorted for
// stable output (config.list/config.get responses, persisted TOML).
func (s *server) installDirsList() []string {
	s.tailersMu.Lock()
	defer s.tailersMu.Unlock()
	out := make([]string, 0, len(s.tailers))
	for dir := range s.tailers {
		out = append(out, dir)
	}
	sort.Strings(out)
	return out
}

// tailerLogPaths returns the Client.txt path every currently-tailed install
// is watching, sorted for stable output — used for the debugging-oriented
// StatusPayload.LogPath field (see joinLogPaths).
func (s *server) tailerLogPaths() []string {
	s.tailersMu.Lock()
	defer s.tailersMu.Unlock()
	out := make([]string, 0, len(s.tailers))
	for _, h := range s.tailers {
		out = append(out, h.logPath)
	}
	sort.Strings(out)
	return out
}

// autoDetectInterval is how often watchAutoDetect rescans running
// processes — infrequent enough to be a negligible cost, frequent enough
// that a game launched just now is picked up within a few seconds, similar
// in spirit to the old C++ WindowTracker's 1s poll but not needing to be
// nearly that tight since there's no window-rect UI depending on it here.
const autoDetectInterval = 5 * time.Second

// watchAutoDetect periodically scans running processes for a match against
// the current executable_names setting (internal/detect) and starts tailing
// any newly-found install dir, persisting it and notifying clients. Exits
// as soon as auto_detect_install_dir is turned off, rather than idling —
// config.set's "auto_detect_install_dir" entry starts a fresh goroutine the
// next time it's turned on, so at most one of these ever runs at a time.
func watchAutoDetect(ctx context.Context, s *server) {
	ticker := time.NewTicker(autoDetectInterval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if !s.autoDetect.Load() {
				return
			}
			names, _ := s.executableNames.Load().([]string)
			dirs, err := detect.Scan(names)
			if err != nil {
				s.debugf("auto-detect scan failed: %v", err)
				continue
			}
			var added bool
			for _, dir := range dirs {
				s.tailersMu.Lock()
				_, exists := s.tailers[dir]
				s.tailersMu.Unlock()
				if exists {
					continue
				}
				if err := s.addInstall(dir); err != nil {
					log.Printf("warn: auto-detect: cannot start ingest for %q: %v", dir, err)
					continue
				}
				log.Printf("auto-detect: install directory detected: %q", dir)
				added = true
			}
			if added {
				if err := s.persistConfig(); err != nil {
					log.Printf("warn: auto-detect: failed to persist config: %v", err)
				}
				s.publishConfigChanged()
			}
		}
	}
}

// idleCheckInterval is how often watchIdle re-evaluates activity. It is well
// below DefaultIdleTimeout so the shutdown fires close to the configured
// deadline rather than up to a whole extra interval late.
const idleCheckInterval = 15 * time.Second

// watchIdle shuts the service down once idleTimeout has elapsed since the
// most recent keep-alive, from either axis ADR-001 requires: a connected
// client's activity (srv.lastActivity) or the log tailer picking up new
// Client.txt lines (tailerActivity), which stands in for the game itself
// being open even with zero addon clients connected. tailerActivity may be
// nil when no log path is configured, in which case only client activity is
// considered.
func watchIdle(ctx context.Context, cancel context.CancelFunc, srv *server, tailerActivity func() time.Time, idleTimeout, checkInterval time.Duration) {
	ticker := time.NewTicker(checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			lastActive := time.Unix(0, srv.lastActivity.Load())
			if tailerActivity != nil {
				if tActive := tailerActivity(); tActive.After(lastActive) {
					lastActive = tActive
				}
			}
			if time.Since(lastActive) >= idleTimeout {
				log.Printf("idle for %s with no client keep-alive or Client.txt activity; shutting down", idleTimeout)
				cancel()
				return
			}
		}
	}
}

// maxBatchEvents caps how many events broadcastLogEvents applies inside a
// single transaction before committing, even if eventCh still has more
// queued — bounds how long a batch (and the single shared DB connection,
// see openDB's SetMaxOpenConns(1)) can be held open at once.
const maxBatchEvents = 200

// batchFlushIdle bounds how long an open batch transaction can sit waiting
// for the next line before broadcastLogEvents commits it anyway.
//
// An earlier version decided this by checking len(eventCh) == 0 — a
// point-in-time snapshot that races with the tailer: if the tailer sends its
// last currently-available line right as that snapshot happens to read
// non-empty, the batch never gets a moment where the check succeeds, so the
// transaction stays open and this goroutine goes idle waiting on eventCh —
// while the tailer's own next saveOffset() call blocks forever waiting for
// the single pooled connection this goroutine is still holding via that
// open transaction. That's a real deadlock, reproduced with a real
// Client.txt: offset frozen forever, "percent never climbs" exactly as
// reported in production (see TestBroadcastLogEvents_DoesNotDeadlock...).
// A timer sidesteps the race entirely: no new line within batchFlushIdle
// means commit, full stop, regardless of channel state.
const batchFlushIdle = 100 * time.Millisecond

// broadcastLogEvents drains parsed Client.txt lines, applying each event to
// the database via w and then — only for overlay-relevant event types, and
// only once the tailer has caught up to EOF at least once this run —
// publishing it to the "clientlog" hub topic. The caughtUp gate prevents
// every service restart from replaying the whole backlog as "live" events.
//
// Writes are batched into transactions (up to maxBatchEvents, or fewer once
// batchFlushIdle passes with nothing new) instead of auto-committing each
// Exec separately: on a disk where each commit's fsync takes tens to
// hundreds of milliseconds, one-transaction-per-statement made backlog
// replay run at ~2 events/sec — for a multi-hundred-thousand-line real
// Client.txt, that's the "percent never climbs" bug reported in production,
// not a display bug. Batching keeps that same latency but pays it once per
// batch instead of once per statement, while the idle flush keeps live,
// real-time events (once backlog replay is caught up) from sitting
// uncommitted indefinitely waiting to reach maxBatchEvents.
//
// debugEnabled is checked before every diagnostic log below (see
// server.debugf) so a normal run stays quiet; when debug_logging is on, this
// is the primary place to see per-event write timing and periodic
// throughput, which is the main lever for diagnosing a stuck/slow-climbing
// backlog-replay percent (see ingestStatus).
//
// onLiveEvent, if non-nil, is called with every event that reaches the live
// broadcast below (same liveBroadcastTypes/caughtUp gate) — server.go's
// addInstallTarget wires this to onAreaEntered, so a zone-transfer event
// from the Steam-associated install can trigger an on-demand rich-presence
// fetch. Callers not wired to that (e.g. the direct-call tests in
// server_test.go) may pass nil.
func broadcastLogEvents(ctx context.Context, h *hub.Hub, eventCh <-chan string, p *parser.Parser, w *ingest.Writer, db *sql.DB, caughtUp func() bool, debugEnabled func() bool, onLiveEvent func(proto.ParsedEvent)) {
	const slowWriteThreshold = 20 * time.Millisecond
	const progressLogInterval = 5 * time.Second
	const stallLogInterval = 10 * time.Second
	var processed int
	lastProgressLog := time.Now()

	// currentLine/currentEventType/stepStartedNs record what this goroutine
	// is doing right now, so the stall watchdog below can report exactly
	// what it was stuck on — this is always on (not gated by debugEnabled)
	// because a hang like this needs to be catchable without having
	// pre-enabled debug logging before it happened.
	var currentLine atomic.Value
	var currentEventType atomic.Value
	var stepStartedNs atomic.Int64
	currentLine.Store("")
	currentEventType.Store("")

	go watchIngestStall(ctx, stallLogInterval, &currentLine, &currentEventType, &stepStartedNs)

	var tx *sql.Tx
	var batchCount int
	commitBatch := func() {
		if tx == nil {
			return
		}
		if err := tx.Commit(); err != nil {
			log.Printf("ingest: batch commit failed: %v", err)
		}
		tx = nil
		batchCount = 0
	}
	defer commitBatch()

	flushTimer := time.NewTimer(batchFlushIdle)
	defer flushTimer.Stop()
	stopFlushTimer := func() {
		if !flushTimer.Stop() {
			select {
			case <-flushTimer.C:
			default:
			}
		}
	}

	for {
		select {
		case line, ok := <-eventCh:
			if !ok {
				commitBatch()
				return
			}
			stopFlushTimer()

			currentLine.Store(line)
			currentEventType.Store("")
			stepStartedNs.Store(time.Now().UnixNano())

			for _, evt := range p.ParseLine(line) {
				currentEventType.Store(evt.Type)
				stepStartedNs.Store(time.Now().UnixNano())

				if tx == nil {
					var err error
					tx, err = db.Begin()
					if err != nil {
						log.Printf("ingest: begin batch failed: %v", err)
						continue
					}
					w.SetDB(tx)
				}

				writeStart := time.Now()
				if err := w.HandleEvent(evt); err != nil {
					log.Printf("ingest: failed to apply %s event: %v", evt.Type, err)
				}
				batchCount++

				if debugEnabled != nil && debugEnabled() {
					processed++
					if d := time.Since(writeStart); d > slowWriteThreshold {
						log.Printf("[debug] ingest: slow write for %s event: %s", evt.Type, d)
					}
					if since := time.Since(lastProgressLog); since >= progressLogInterval {
						log.Printf("[debug] ingest: processed %d events so far (%.0f events/sec over last %s)",
							processed, float64(processed)/since.Seconds(), since.Round(time.Second))
						processed = 0
						lastProgressLog = time.Now()
					}
				}

				if batchCount >= maxBatchEvents {
					commitBatch()
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
				if onLiveEvent != nil {
					onLiveEvent(evt)
				}
			}
			stepStartedNs.Store(0)
			flushTimer.Reset(batchFlushIdle)

		case <-flushTimer.C:
			commitBatch()
			flushTimer.Reset(batchFlushIdle)

		case <-ctx.Done():
			commitBatch()
			return
		}
	}
}

// watchIngestStall periodically checks whether broadcastLogEvents has been
// stuck on the same line/event for at least interval, logging a warning with
// exactly what it was doing if so. stepStartedNs == 0 means idle between
// lines (nothing to report); a nonzero value older than interval means the
// goroutine hasn't returned from ParseLine/HandleEvent in at least that long.
func watchIngestStall(ctx context.Context, interval time.Duration, currentLine, currentEventType *atomic.Value, stepStartedNs *atomic.Int64) {
	ticker := time.NewTicker(interval)
	defer ticker.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			started := stepStartedNs.Load()
			if started == 0 {
				continue // idle between lines, not stuck
			}
			if age := time.Since(time.Unix(0, started)); age >= interval {
				log.Printf("warn: ingest appears stalled for %s on line=%q event=%q",
					age.Round(time.Second), currentLine.Load(), currentEventType.Load())
			}
		}
	}
}

// ingestStatus derives the human-facing ingestion phase, message, and
// backlog-replay percent from the tailer's state: no tailer configured means
// nothing is happening yet; fileFound false means a tailer is configured but
// hasn't yet found Client.txt to open (e.g. the game has never run for this
// install) — reported as "waiting" rather than "ingesting" since there's
// nothing to show progress against. Otherwise the tailer's one-way caughtUp
// latch (see tailer.go) distinguishes backlog replay from live tailing.
// percent is only populated during backlog replay, and only once a file size
// is known.
func ingestStatus(hasTailer, fileFound, caughtUp bool, offset, size int64) (phase, message string, percent *float64) {
	if !hasTailer {
		return "waiting", "waiting", nil
	}
	if !fileFound {
		return "waiting", "waiting for log file", nil
	}
	if caughtUp {
		return "tailing", "waiting for game events", nil
	}
	if size > 0 {
		p := float64(offset) / float64(size) * 100
		percent = &p
	}
	return "ingesting", "processing game logs", percent
}

// aggregateProgress sums offset/size across every tailer (one per configured
// install) so ingestStatus can report a single overall phase/percent instead
// of one per install — a caught-up tailer contributes offset==size to both
// sums, so it doesn't skew the ratio once it's done. caughtUp is true only
// once every tailer has caught up: a partial replay (some installs done,
// others not) still counts as "ingesting" overall. fileFound follows the
// same all-or-nothing rule: if any configured install's Client.txt hasn't
// been found yet, the overall status isn't ready to claim real progress.
func aggregateProgress(tailers []*tailer.Tailer) (offset, size int64, fileFound, caughtUp bool) {
	fileFound = true
	caughtUp = true
	for _, t := range tailers {
		o, s := t.Progress()
		offset += o
		size += s
		if !t.FileFound() {
			fileFound = false
		}
		if !t.CaughtUp() {
			caughtUp = false
		}
	}
	return offset, size, fileFound, caughtUp
}

// joinLogPaths renders every currently-tailed install's Client.txt path into
// a single comma-separated string for the (unstructured, debugging-oriented)
// StatusPayload.LogPath field — none of poe-info-service's current clients
// parse it, they only read phase/message/percent (see MainWindow::applyStatusPayload).
func joinLogPaths(paths []string) string {
	return strings.Join(paths, ",")
}

// watchIngestStatus periodically checks the aggregate phase/percent across
// every currently-tailed install (see aggregateProgress) and publishes a
// "status" topic event — the same proto.StatusPayload shape the "status"
// request returns — whenever the phase changes or percent crosses into a new
// whole percent. This lets a client subscribe once instead of re-polling
// "status" for the (potentially long) duration of a Client.txt backlog
// replay; see MainWindow's use of it for the actual consumer.
//
// Runs for the server's whole lifetime rather than stopping once every
// install has caught up: installs can be added later (auto-detect,
// config.set), and a freshly added one starts out not caught up, which
// would otherwise need to flip the aggregate phase back from "tailing" to
// "ingesting" after this loop had already exited.
func watchIngestStatus(ctx context.Context, srv *server) {
	ticker := time.NewTicker(500 * time.Millisecond)
	defer ticker.Stop()

	lastPhase := ""
	lastWholePercent := -1

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			tailers := srv.tailerSnapshot()
			offset, size, fileFound, caughtUp := aggregateProgress(tailers)
			phase, message, percent := ingestStatus(len(tailers) > 0, fileFound, caughtUp, offset, size)

			wholePercent := -1
			if percent != nil {
				wholePercent = int(*percent)
			}
			if phase == lastPhase && wholePercent == lastWholePercent {
				continue
			}
			lastPhase, lastWholePercent = phase, wholePercent

			srv.debugf("status broadcast: phase=%q offset=%d size=%d wholePercent=%d", phase, offset, size, wholePercent)
			msg, _ := json.Marshal(proto.Message{
				Type:  proto.TypeEvent,
				Topic: proto.TopicStatus,
				Payload: mustMarshal(proto.StatusPayload{
					Version:   srv.cfg.Version,
					StartTime: srv.cfg.StartTime,
					LogPath:   joinLogPaths(srv.tailerLogPaths()),
					LogOffset: offset,
					Uptime:    time.Since(srv.started).Round(time.Second).String(),
					Phase:     phase,
					Message:   message,
					Percent:   percent,
				}),
			})
			srv.hub.Publish(proto.TopicStatus, msg)
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
		// A bare connectivity check is not, by itself, evidence the client
		// still needs the service running — it does not touch the idle
		// timer. Clients that want to keep the service alive send an
		// explicit keepalive (or any real request/subscription).
		s.send(c, proto.Message{Type: proto.TypePong, ID: msg.ID})

	case proto.TypeKeepalive:
		s.touch()
		s.send(c, proto.Message{Type: proto.TypeKeepalive, ID: msg.ID})

	case proto.TypeSubscribe:
		s.touch()
		if msg.Topic != "" {
			s.hub.Subscribe(c, msg.Topic)
			s.send(c, proto.Message{
				Type:    proto.TypeResponse,
				ID:      msg.ID,
				Payload: mustMarshal(map[string]bool{"subscribed": true}),
			})
		}

	case proto.TypeUnsubscribe:
		s.touch()
		if msg.Topic != "" {
			s.hub.Unsubscribe(c, msg.Topic)
		}

	case proto.TypeRequest:
		s.touch()
		// Requests on one connection are handled one at a time, in order (see
		// the read loop in handleWS) — a slow request here delays every
		// request queued behind it on the same connection, including ones
		// (like "status") that don't themselves touch the database. Logging
		// this duration is the most direct way to catch that starvation.
		if s.debugLogging.Load() {
			start := time.Now()
			s.handleRequest(c, msg)
			if d := time.Since(start); d > 20*time.Millisecond {
				log.Printf("[debug] slow request: method=%q id=%s took %s", msg.Method, msg.ID, d)
			}
		} else {
			s.handleRequest(c, msg)
		}
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
		tailers := s.tailerSnapshot()
		offset, size, fileFound, caughtUp := aggregateProgress(tailers)
		phase, message, percent := ingestStatus(len(tailers) > 0, fileFound, caughtUp, offset, size)
		if percent != nil {
			s.debugf("status request id=%s: phase=%q offset=%d size=%d percent=%.4f", msg.ID, phase, offset, size, *percent)
		} else {
			s.debugf("status request id=%s: phase=%q offset=%d size=%d percent=<nil>", msg.ID, phase, offset, size)
		}
		s.send(c, proto.Message{
			Type: proto.TypeResponse,
			ID:   msg.ID,
			Payload: mustMarshal(proto.StatusPayload{
				Version:   s.cfg.Version,
				StartTime: s.cfg.StartTime,
				LogPath:   joinLogPaths(s.tailerLogPaths()),
				LogOffset: offset,
				Uptime:    time.Since(s.started).Round(time.Second).String(),
				Phase:     phase,
				Message:   message,
				Percent:   percent,
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

	case "steam.presence":
		s.handleSteamPresence(c, msg)

	case "character.level":
		s.handleCharacterLevel(c, msg)

	case "character.class":
		s.handleCharacterClass(c, msg)

	case "poe.league":
		s.handleLeague(c, msg)

	case "credentials.store":
		s.handleCredentialsStore(c, msg)

	case "credentials.has":
		s.handleCredentialsHas(c, msg)

	case "credentials.delete":
		s.handleCredentialsDelete(c, msg)

	case "poe.oauth.login":
		s.handlePoeOAuthLogin(c, msg)

	case "poe.oauth.status":
		s.handlePoeOAuthStatus(c, msg)

	case "poe.oauth.logout":
		s.handlePoeOAuthLogout(c, msg)

	case "poe.accounts.list":
		s.handlePoeAccountsList(c, msg)

	case "poe.profile.locale":
		s.handlePoeProfileLocale(c, msg)

	case "poe.profile.twitch":
		s.handlePoeProfileTwitch(c, msg)

	case "poe.leagues.list":
		s.handlePoeLeaguesList(c, msg)

	case "poe.leagues.detail":
		s.handlePoeLeaguesDetail(c, msg)

	case "poe.ratelimit.status":
		s.handlePoeRateLimitStatus(c, msg)

	case "config.list":
		s.handleConfigList(c, msg)

	case "config.get":
		s.handleConfigGet(c, msg)

	case "config.set":
		s.handleConfigSet(c, msg)

	case "channels.register":
		s.handleChannelsRegister(c, msg)

	case "channels.rename":
		s.handleChannelsRename(c, msg)

	case "channels.delete":
		s.handleChannelsDelete(c, msg)

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
	closed, err := ingest.CloseOrphanSessions(s.db, params.RunningInstallPaths)
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

// handleCredentialsStore lets a client hand a secret (POESESSID today, future
// OAuth tokens later) to this service to own from then on. Per ADR-004, the
// value itself is never logged and never sent back to any client.
func (s *server) handleCredentialsStore(c *hub.Client, msg proto.Message) {
	var params struct {
		Key   string `json:"key"`
		Value string `json:"value"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Key == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: key required"})
		return
	}
	if err := creds.Store(creds.ServiceName, params.Key, params.Value); err != nil {
		log.Printf("credentials.store error id=%s key=%s: %v", msg.ID, params.Key, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
	})
}

// handleCredentialsHas reports only whether a credential is present, never
// its value — clients ask "do we have one stored", not "give it to me".
func (s *server) handleCredentialsHas(c *hub.Client, msg proto.Message) {
	var params struct {
		Key string `json:"key"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Key == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: key required"})
		return
	}
	_, err := creds.Get(creds.ServiceName, params.Key)
	if err != nil && err != creds.ErrNotFound {
		log.Printf("credentials.has error id=%s key=%s: %v", msg.ID, params.Key, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"present": err == nil}),
	})
}

func (s *server) handleCredentialsDelete(c *hub.Client, msg proto.Message) {
	var params struct {
		Key string `json:"key"`
	}
	if err := json.Unmarshal(msg.Payload, &params); err != nil || params.Key == "" {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: key required"})
		return
	}
	if err := creds.Delete(creds.ServiceName, params.Key); err != nil {
		log.Printf("credentials.delete error id=%s key=%s: %v", msg.ID, params.Key, err)
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(map[string]bool{"ok": true}),
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
