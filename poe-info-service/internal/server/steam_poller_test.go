package server

import (
	"context"
	"database/sql"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
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
// watchSteamPresenceWithIntervals directly, bypassing serve()'s full
// startup (no listener, no tailers) — mirrors the bare &server{...}
// construction pattern used by TestWatchIdle-style tests elsewhere in this
// package. steamClient is pointed at official/miniprofile test servers
// instead of the real Steam hosts.
func newSteamTestServer(t *testing.T, official, miniprofile string, ids []string) *server {
	t.Helper()
	st, err := store.New(openSteamTestDB(t))
	if err != nil {
		t.Fatalf("store.New: %v", err)
	}
	srv := &server{
		hub:           hub.New(),
		store:         st,
		steamClient:   steam.NewClient(nil, steam.WithOfficialBaseURL(official), steam.WithMiniprofileBaseURL(miniprofile)),
		steamPresence: make(map[string]proto.SteamPresenceEntry),
	}
	srv.steamIDs.Store(ids)
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

func TestWatchSteamPresence_NoSubscribersNeverContactsSteam(t *testing.T) {
	official := &requestCounter{}
	miniprofile := &requestCounter{}
	officialSrv := httptest.NewServer(official.handler(testfixtures.SteamPlayerSummariesInGame))
	defer officialSrv.Close()
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, officialSrv.URL, miniprofileSrv.URL, []string{"76561197960287930"})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go watchSteamPresenceWithIntervals(ctx, srv, 10*time.Millisecond, 10*time.Millisecond)

	// No client ever subscribes to TopicSteamPresence. Give the poller
	// several ticks' worth of time to (incorrectly) fire before asserting.
	time.Sleep(150 * time.Millisecond)

	if n := official.n.Load(); n != 0 {
		t.Errorf("official API requests = %d, want 0 with no subscribers", n)
	}
	if n := miniprofile.n.Load(); n != 0 {
		t.Errorf("miniprofile requests = %d, want 0 with no subscribers", n)
	}
}

func TestWatchSteamPresence_SubscriberActivatesPolling(t *testing.T) {
	official := &requestCounter{}
	miniprofile := &requestCounter{}
	officialSrv := httptest.NewServer(official.handler(testfixtures.SteamPlayerSummariesInGame))
	defer officialSrv.Close()
	miniprofileSrv := httptest.NewServer(miniprofile.handler(testfixtures.SteamMiniprofileWithRichPresence))
	defer miniprofileSrv.Close()

	srv := newSteamTestServer(t, officialSrv.URL, miniprofileSrv.URL, []string{"76561197960287930"})

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	go watchSteamPresenceWithIntervals(ctx, srv, 10*time.Millisecond, 10*time.Millisecond)

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

	snapshot := srv.steamPresenceSnapshot()
	if len(snapshot.Entries) != 1 || snapshot.Entries[0].Status != proto.SteamPresenceStatusOK {
		t.Errorf("snapshot after poll = %+v, want one ok entry", snapshot.Entries)
	}
	if snapshot.Entries[0].RichPresence == "" {
		t.Errorf("snapshot entry %+v: want a populated RichPresence", snapshot.Entries[0])
	}
}
