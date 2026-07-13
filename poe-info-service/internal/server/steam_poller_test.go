package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/steam"
	"github.com/MovingCairn/poe-info-service/internal/store"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"

	_ "modernc.org/sqlite"
)

func openSteamTestDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	return db
}

// newSteamTestServer builds a minimal *server sufficient to drive
// watchRichPresenceWithIntervals directly, bypassing serve()'s full startup
// (no listener, no tailers) — mirrors the bare &server{...} construction
// pattern used by TestWatchIdle-style tests elsewhere in this package.
// steamClient is pointed at a miniprofile test server instead of the real
// Steam host.
func newSteamTestServer(t *testing.T, miniprofile string, id string) *server {
	t.Helper()
	st, err := store.New(openSteamTestDB(t))
	if err != nil {
		t.Fatalf("store.New: %v", err)
	}
	rootCtx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)
	srv := &server{
		hub:         hub.New(),
		store:       st,
		rootCtx:     rootCtx,
		steamClient: steam.NewClient(nil, steam.WithMiniprofileBaseURL(miniprofile)),
	}
	srv.steamID.Store(id)
	return srv
}

// requestCounter counts requests received by an httptest.Server, so tests
// can assert Steam was (or wasn't) actually contacted.
type requestCounter struct {
	n atomic.Int64
}

func (r *requestCounter) handler(body string) http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		r.n.Add(1)
		w.Write([]byte(body))
	}
}

func TestWatchRichPresence_NoSubscribersNeverContactsSteam(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "76561197960287930")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go watchRichPresenceWithIntervals(ctx, srv, 10*time.Millisecond, 10*time.Millisecond)

	// No client ever subscribes to any rich-presence topic. Give the poller
	// several ticks' worth of time to (incorrectly) fire before asserting.
	time.Sleep(150 * time.Millisecond)

	if n := miniprofile.n.Load(); n != 0 {
		t.Errorf("miniprofile requests = %d, want 0 with no subscribers", n)
	}
}

func TestWatchRichPresence_SubscriberActivatesPolling(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "76561197960287930")

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go watchRichPresenceWithIntervals(ctx, srv, 10*time.Millisecond, 10*time.Millisecond)

	c := hub.NewClient()
	defer c.Close()
	srv.hub.Subscribe(c, proto.TopicSteamPresence)

	deadline := time.Now().Add(2 * time.Second)
	for time.Now().Before(deadline) {
		if miniprofile.n.Load() > 0 {
			break
		}
		time.Sleep(10 * time.Millisecond)
	}

	if n := miniprofile.n.Load(); n == 0 {
		t.Fatal("miniprofile requests = 0, want at least one poll cycle to have run after subscribing")
	}

	snap := srv.richPresenceSnapshot()
	if snap.Status != proto.RichPresenceStatusOK {
		t.Errorf("snapshot after poll status = %q, want %q", snap.Status, proto.RichPresenceStatusOK)
	}
	if snap.Raw == "" {
		t.Error("snapshot after poll: want a populated Raw rich-presence string")
	}
	if snap.League == "" || snap.Level == 0 || snap.Class == "" {
		t.Errorf("snapshot after poll = %+v, want parsed league/level/class", snap)
	}
}

func TestEnsureFreshRichPresence_RespectsRequestTTL(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "76561197960287930")

	srv.ensureFreshRichPresence(context.Background())
	if n := miniprofile.n.Load(); n != 1 {
		t.Fatalf("miniprofile requests after first ensureFreshRichPresence = %d, want 1", n)
	}

	// A second call immediately after must be a no-op: the request-TTL gate
	// (25s in production) hasn't elapsed.
	srv.ensureFreshRichPresence(context.Background())
	if n := miniprofile.n.Load(); n != 1 {
		t.Errorf("miniprofile requests after second immediate ensureFreshRichPresence = %d, want still 1 (TTL gate)", n)
	}
}

func TestEnsureFreshRichPresence_NoSteamIDConfiguredIsNoop(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "")

	srv.ensureFreshRichPresence(context.Background())
	if n := miniprofile.n.Load(); n != 0 {
		t.Errorf("miniprofile requests with no steam_id configured = %d, want 0", n)
	}
	snap := srv.richPresenceSnapshot()
	if snap.Status != "" {
		t.Errorf("snapshot with no steam_id configured: status = %q, want unset", snap.Status)
	}
}

func TestPublishRichPresenceChanges_OnlyPublishesChangedFields(t *testing.T) {
	srv := &server{hub: hub.New()}

	var levelEvents, classEvents, leagueEvents, rawEvents int
	c := hub.NewClient()
	defer c.Close()
	srv.hub.Subscribe(c, proto.TopicCharacterLevel)
	srv.hub.Subscribe(c, proto.TopicCharacterClass)
	srv.hub.Subscribe(c, proto.TopicLeague)
	srv.hub.Subscribe(c, proto.TopicSteamPresence)

	drain := func() {
		for {
			select {
			case msg := <-c.Send:
				var m proto.Message
				if err := json.Unmarshal(msg, &m); err != nil {
					t.Fatalf("unmarshal published message: %v", err)
				}
				switch m.Topic {
				case proto.TopicCharacterLevel:
					levelEvents++
				case proto.TopicCharacterClass:
					classEvents++
				case proto.TopicLeague:
					leagueEvents++
				case proto.TopicSteamPresence:
					rawEvents++
				}
			default:
				return
			}
		}
	}

	prev := richPresenceState{}
	next := richPresenceState{Raw: "SSF Ancestors: 92 Warden - The Sarn Encampment", League: "SSF Ancestors", Level: 92, Class: "Warden", Status: proto.RichPresenceStatusOK}
	srv.publishRichPresenceChanges(prev, next)
	drain()

	if levelEvents != 1 || classEvents != 1 || leagueEvents != 1 || rawEvents != 1 {
		t.Fatalf("first publish: level=%d class=%d league=%d raw=%d, want 1 each", levelEvents, classEvents, leagueEvents, rawEvents)
	}

	levelEvents, classEvents, leagueEvents, rawEvents = 0, 0, 0, 0

	// A level-up only: league/class/raw are unchanged and must not re-fire.
	prev2 := next
	next2 := next
	next2.Level = 93
	next2.Raw = "SSF Ancestors: 93 Warden - The Sarn Encampment"
	srv.publishRichPresenceChanges(prev2, next2)
	drain()

	if levelEvents != 1 {
		t.Errorf("level-up publish: level events = %d, want 1", levelEvents)
	}
	if classEvents != 0 {
		t.Errorf("level-up publish: class events = %d, want 0 (class unchanged)", classEvents)
	}
	if leagueEvents != 0 {
		t.Errorf("level-up publish: league events = %d, want 0 (league unchanged)", leagueEvents)
	}
	if rawEvents != 1 {
		t.Errorf("level-up publish: raw events = %d, want 1 (raw text changed)", rawEvents)
	}
}

// --- leagueDetailFor / poe.league enrichment ---

// TestHandleLeague_EnrichesWithCachedDetail proves poe.league's response
// carries a zero-cost Detail field (see leagueDetailFor) when the leagues
// table already has a cached row matching the Steam-parsed current league
// name — no PoE OAuth API call involved, purely a DB read joined in.
func TestHandleLeague_EnrichesWithCachedDetail(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "76561197960287930")
	srv.db = openLeaguesTestDB(t)
	// SteamMiniprofileWithRichPresence's rich presence text ("SSF Ancestors:
	// 92 Warden - ...") parses to League="SSF Ancestors" — seed a matching
	// cached row under the default (pc) realm rich presence is assumed to
	// describe.
	if err := upsertLeagues(srv.db, []poe.League{{ID: "SSF Ancestors", Realm: defaultLeaguesRealm, Description: "cached detail"}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	c := hub.NewClient()
	defer c.Close()
	srv.handleLeague(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	var payload proto.LeaguePayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.League != "SSF Ancestors" {
		t.Fatalf("League = %q, want SSF Ancestors", payload.League)
	}
	if payload.Detail == nil || payload.Detail.Description != "cached detail" {
		t.Errorf("Detail = %+v, want the cached SSF Ancestors row", payload.Detail)
	}
}

// TestHandleLeague_NoCachedDetail_DetailNil proves poe.league still works
// (and never fetches) when nothing is cached for the current league yet.
func TestHandleLeague_NoCachedDetail_DetailNil(t *testing.T) {
	miniprofile := &requestCounter{}
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, miniprofileSrv.URL, "76561197960287930")
	srv.db = openLeaguesTestDB(t) // schema present, but no leagues rows at all

	c := hub.NewClient()
	defer c.Close()
	srv.handleLeague(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	var payload proto.LeaguePayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Detail != nil {
		t.Errorf("Detail = %+v, want nil with nothing cached", payload.Detail)
	}
}

// TestLeagueDetailFor_NoDB_ReturnsNil proves the enrichment helper itself
// degrades gracefully (no panic) when no db is configured at all — the
// common case for every other existing steam.go test in this file.
func TestLeagueDetailFor_NoDB_ReturnsNil(t *testing.T) {
	srv := &server{}
	if got := srv.leagueDetailFor("SSF Ancestors"); got != nil {
		t.Errorf("leagueDetailFor with no db = %+v, want nil", got)
	}
}

func TestLeagueDetailFor_EmptyName_ReturnsNil(t *testing.T) {
	srv := &server{db: openLeaguesTestDB(t)}
	if got := srv.leagueDetailFor(""); got != nil {
		t.Errorf("leagueDetailFor(\"\") = %+v, want nil", got)
	}
}
