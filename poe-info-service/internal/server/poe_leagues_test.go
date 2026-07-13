package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
	"github.com/MovingCairn/poe-info-service/internal/schema"
)

// openLeaguesTestDB returns an in-memory database with the real schema
// applied — upsertLeagues/queryLeaguesRows only ever touch poe-info-service's
// own SQLite database, so (unlike internal/creds-backed code) these are safe
// to exercise directly.
func openLeaguesTestDB(t *testing.T) *sql.DB {
	t.Helper()
	db, err := sql.Open("sqlite", "file:"+t.Name()+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("ensure schema: %v", err)
	}
	return db
}

// newPoeLeaguesTestServer builds a *server with everything poe_leagues.go
// touches: a real schema'd in-memory db, a real reqqueue.Queue using the
// real OAuth header parser, and a poeClient pointed at leaguesURL (normally
// an httptest.Server) — same shape as newPoeProfileTestServer in
// poe_profile_test.go.
func newPoeLeaguesTestServer(t *testing.T, leaguesURL string) *server {
	t.Helper()
	db := openLeaguesTestDB(t)

	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	return &server{
		db:        db,
		hub:       hub.New(),
		rootCtx:   ctx,
		poeClient: poe.NewClient(nil, poe.WithLeaguesURL(leaguesURL)),
		poeQueue:  reqqueue.New(ctx, poeOAuthRateLimitHeaders),
	}
}

// --- upsertLeagues / queryLeaguesRows ---

func TestUpsertLeagues_InsertsAndFlattensRules(t *testing.T) {
	db := openLeaguesTestDB(t)
	fetchedAt := time.Unix(1_700_000_000, 0)
	fetched := []poe.League{
		{ID: "SSF Ancestors", Realm: "pc", URL: "https://example.com", StartAt: "2024-01-01T00:00:00Z",
			Description: "desc", Rules: []poe.LeagueRule{{ID: "Hardcore"}, {ID: "NoParties"}}, Event: false},
	}

	if err := upsertLeagues(db, fetched, fetchedAt); err != nil {
		t.Fatalf("upsertLeagues: %v", err)
	}

	rows, oldest, err := queryLeaguesRows(db, "pc", "main")
	if err != nil {
		t.Fatalf("queryLeaguesRows: %v", err)
	}
	if len(rows) != 1 {
		t.Fatalf("got %d rows, want 1", len(rows))
	}
	got := rows[0]
	if got.Name != "SSF Ancestors" || got.Realm != "pc" || got.URL != "https://example.com" || got.Description != "desc" {
		t.Errorf("row = %+v, missing expected fields", got)
	}
	if len(got.Rules) != 2 || got.Rules[0] != "Hardcore" || got.Rules[1] != "NoParties" {
		t.Errorf("Rules = %v, want [Hardcore NoParties]", got.Rules)
	}
	if !oldest.Equal(fetchedAt) {
		t.Errorf("oldest fetchedAt = %v, want %v", oldest, fetchedAt)
	}
}

// TestUpsertLeagues_SameNameDifferentRealm_TwoRows proves a league name that
// repeats across realms (e.g. "Standard" on both pc and xbox) is stored as
// two distinct rows, not merged/collided into one.
func TestUpsertLeagues_SameNameDifferentRealm_TwoRows(t *testing.T) {
	db := openLeaguesTestDB(t)
	fetched := []poe.League{
		{ID: "Standard", Realm: "pc"},
		{ID: "Standard", Realm: "xbox"},
	}
	if err := upsertLeagues(db, fetched, time.Now()); err != nil {
		t.Fatalf("upsertLeagues: %v", err)
	}

	var count int
	if err := db.QueryRow(`SELECT COUNT(*) FROM leagues WHERE name = 'Standard'`).Scan(&count); err != nil {
		t.Fatalf("count: %v", err)
	}
	if count != 2 {
		t.Fatalf("got %d Standard rows, want 2 (one per realm)", count)
	}
}

// TestUpsertLeagues_ExistingLeague_UpdatesInPlace proves a second fetch for
// the same (name, realm) refreshes the row rather than duplicating it.
func TestUpsertLeagues_ExistingLeague_UpdatesInPlace(t *testing.T) {
	db := openLeaguesTestDB(t)
	first := time.Unix(1_700_000_000, 0)
	if err := upsertLeagues(db, []poe.League{{ID: "SSF Ancestors", Realm: "pc", Description: "old"}}, first); err != nil {
		t.Fatalf("first upsertLeagues: %v", err)
	}
	second := first.Add(time.Hour)
	if err := upsertLeagues(db, []poe.League{{ID: "SSF Ancestors", Realm: "pc", Description: "new"}}, second); err != nil {
		t.Fatalf("second upsertLeagues: %v", err)
	}

	var count int
	if err := db.QueryRow(`SELECT COUNT(*) FROM leagues`).Scan(&count); err != nil {
		t.Fatalf("count: %v", err)
	}
	if count != 1 {
		t.Fatalf("got %d rows, want 1 (updated in place)", count)
	}

	rows, oldest, err := queryLeaguesRows(db, "pc", "main")
	if err != nil {
		t.Fatalf("queryLeaguesRows: %v", err)
	}
	if len(rows) != 1 || rows[0].Description != "new" {
		t.Errorf("rows = %+v, want Description=new", rows)
	}
	if !oldest.Equal(second) {
		t.Errorf("fetched_at = %v, want %v (the refreshed timestamp)", oldest, second)
	}
}

// TestQueryLeaguesRows_FiltersByRealmAndEventFlag proves the realm and
// type=="event" filters actually narrow the result set.
func TestQueryLeaguesRows_FiltersByRealmAndEventFlag(t *testing.T) {
	db := openLeaguesTestDB(t)
	fetched := []poe.League{
		{ID: "Standard", Realm: "pc", Event: false},
		{ID: "Flashback Event", Realm: "pc", Event: true},
		{ID: "Standard", Realm: "xbox", Event: false},
	}
	if err := upsertLeagues(db, fetched, time.Now()); err != nil {
		t.Fatalf("upsertLeagues: %v", err)
	}

	pcMain, _, err := queryLeaguesRows(db, "pc", "main")
	if err != nil {
		t.Fatalf("queryLeaguesRows pc/main: %v", err)
	}
	if len(pcMain) != 1 || pcMain[0].Name != "Standard" {
		t.Errorf("pc/main = %+v, want just Standard", pcMain)
	}

	pcEvent, _, err := queryLeaguesRows(db, "pc", "event")
	if err != nil {
		t.Fatalf("queryLeaguesRows pc/event: %v", err)
	}
	if len(pcEvent) != 1 || pcEvent[0].Name != "Flashback Event" {
		t.Errorf("pc/event = %+v, want just Flashback Event", pcEvent)
	}

	xboxMain, _, err := queryLeaguesRows(db, "xbox", "main")
	if err != nil {
		t.Fatalf("queryLeaguesRows xbox/main: %v", err)
	}
	if len(xboxMain) != 1 || xboxMain[0].Realm != "xbox" {
		t.Errorf("xbox/main = %+v, want just the xbox Standard row", xboxMain)
	}
}

// TestQueryLeaguesRows_Empty_ZeroTimeOldest proves an empty result set
// reports a zero-value oldest timestamp rather than an error, so
// ensureLeagues's freshness check correctly treats "no cache at all" as not
// fresh regardless of maxAge.
func TestQueryLeaguesRows_Empty_ZeroTimeOldest(t *testing.T) {
	db := openLeaguesTestDB(t)
	rows, oldest, err := queryLeaguesRows(db, "pc", "main")
	if err != nil {
		t.Fatalf("queryLeaguesRows: %v", err)
	}
	if len(rows) != 0 {
		t.Errorf("got %d rows, want 0", len(rows))
	}
	if !oldest.IsZero() {
		t.Errorf("oldest = %v, want zero value", oldest)
	}
}

// --- handlePoeLeaguesList ---

func TestHandlePoeLeaguesList_CacheHit_ReturnsFresh(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	if err := upsertLeagues(s.db, []poe.League{{ID: "Standard", Realm: "pc"}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	c := hub.NewClient()
	defer c.Close()
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	var payload proto.PoeLeaguesPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Status != "fresh" || len(payload.Leagues) != 1 || payload.Leagues[0].Name != "Standard" {
		t.Errorf("payload = %+v, want status=fresh with the seeded Standard league", payload)
	}
}

// TestHandlePoeLeaguesList_NoCache_NoWait_ReturnsPendingThenPublishes proves
// the non-blocking path: an immediate "pending" response, followed by the
// fetched list arriving on TopicPoeLeagues once the background fetch
// (against a real httptest server standing in for api.pathofexile.com)
// completes — mirroring TestHandlePoeProfileTwitch_NoCache_NoWait_....
func TestHandlePoeLeaguesList_NoCache_NoWait_ReturnsPendingThenPublishes(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc","event":false}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)

	c := hub.NewClient()
	defer c.Close()
	s.hub.Subscribe(c, proto.TopicPoeLeagues)

	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	first := recvResponse(t, c)
	var firstPayload proto.PoeLeaguesPayload
	if err := json.Unmarshal(first.Payload, &firstPayload); err != nil {
		t.Fatalf("unmarshal first payload: %v", err)
	}
	if firstPayload.Status != "pending" {
		t.Fatalf("first response status = %q, want pending", firstPayload.Status)
	}

	pushed := recvResponse(t, c)
	if pushed.Type != proto.TypeEvent || pushed.Topic != proto.TopicPoeLeagues {
		t.Fatalf("expected a TopicPoeLeagues event, got %+v", pushed)
	}
	var pushedPayload proto.PoeLeaguesPayload
	if err := json.Unmarshal(pushed.Payload, &pushedPayload); err != nil {
		t.Fatalf("unmarshal pushed payload: %v", err)
	}
	if pushedPayload.Status != "ok" || len(pushedPayload.Leagues) != 1 || pushedPayload.Leagues[0].Name != "Standard" {
		t.Errorf("pushed payload = %+v, want status=ok with the fetched Standard league", pushedPayload)
	}
}

// TestHandlePoeLeaguesList_Wait_ReturnsOkInline proves the blocking path
// delivers the fetched list directly on the same request/response, tagged
// "ok" rather than "pending".
func TestHandlePoeLeaguesList_Wait_ReturnsOkInline(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"leagues":[{"id":"SSF Standard","realm":"pc","event":false}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)

	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeaguesRequest{Wait: true})
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	resp := recvResponse(t, c)
	var payload proto.PoeLeaguesPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Status != "ok" || len(payload.Leagues) != 1 || payload.Leagues[0].Name != "SSF Standard" {
		t.Errorf("payload = %+v, want status=ok with the fetched SSF Standard league", payload)
	}
}

// TestHandlePoeLeaguesList_NoDB_ReturnsError proves a server with no db
// configured reports an error rather than panicking.
func TestHandlePoeLeaguesList_NoDB_ReturnsError(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()
	srv.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error with no db configured, got none")
	}
}

func TestHandlePoeLeaguesList_BadParams_ReturnsError(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	c := hub.NewClient()
	defer c.Close()
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: json.RawMessage(`{not valid json`)})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error for malformed params, got none")
	}
}

// TestHandlePoeLeaguesList_DefaultsRealmAndType proves an empty request uses
// defaultLeaguesRealm/defaultLeaguesType (pc/main) — a cache seeded under
// those defaults is served as a hit with no realm/type specified.
func TestHandlePoeLeaguesList_DefaultsRealmAndType(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	if err := upsertLeagues(s.db, []poe.League{{ID: "Standard", Realm: defaultLeaguesRealm, Event: false}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	c := hub.NewClient()
	defer c.Close()
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	var payload proto.PoeLeaguesPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if payload.Status != "fresh" || len(payload.Leagues) != 1 {
		t.Errorf("payload = %+v, want a fresh hit against the default realm/type", payload)
	}
}

func decodeLeaguesPayload(t *testing.T, resp proto.Message) proto.PoeLeaguesPayload {
	t.Helper()
	var payload proto.PoeLeaguesPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	return payload
}

func decodeLeagueDetailPayload(t *testing.T, resp proto.Message) proto.PoeLeagueDetailPayload {
	t.Helper()
	var payload proto.PoeLeagueDetailPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	return payload
}

// --- fetchPolicy: never / always ---

// TestEnsureLeagues_FetchPolicyNever_StaleCache_ReturnsStaleWithoutFetch
// mirrors TestEnsurePoeProfile_FetchPolicyNever_StaleCache_ReturnsStaleWithoutFetch
// for the leagues path.
func TestEnsureLeagues_FetchPolicyNever_StaleCache_ReturnsStaleWithoutFetch(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	stale := time.Now().Add(-48 * time.Hour)
	if err := upsertLeagues(s.db, []poe.League{{ID: "Standard", Realm: "pc"}}, stale); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	leagues, haveCache, isFresh, fetchedAt, waiter := s.ensureLeagues("pc", "main", "", time.Hour, poeLeaguesFetchPriority, fetchPolicyNever)
	if waiter != nil {
		t.Error("fetchPolicyNever submitted a fetch, want none")
	}
	if !haveCache || isFresh {
		t.Errorf("haveCache=%v isFresh=%v, want haveCache=true isFresh=false (stale)", haveCache, isFresh)
	}
	if len(leagues) != 1 || leagues[0].Name != "Standard" {
		t.Errorf("leagues = %+v, want the stale cached Standard row", leagues)
	}
	// fetched_at round-trips through an RFC3339 (second-precision, UTC)
	// TEXT column, so compare at second granularity rather than exact
	// equality.
	if fetchedAt.Unix() != stale.Unix() {
		t.Errorf("fetchedAt = %v, want %v", fetchedAt, stale)
	}
}

func TestEnsureLeagues_FetchPolicyNever_NoCache_NoFetch(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	_, haveCache, _, _, waiter := s.ensureLeagues("pc", "main", "", time.Hour, poeLeaguesFetchPriority, fetchPolicyNever)
	if waiter != nil {
		t.Error("fetchPolicyNever submitted a fetch, want none")
	}
	if haveCache {
		t.Error("haveCache = true with nothing ever cached, want false")
	}
}

// TestEnsureLeagues_FetchPolicyAlways_FetchesEvenWhenFresh mirrors the
// profile-path equivalent.
func TestEnsureLeagues_FetchPolicyAlways_FetchesEvenWhenFresh(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	if err := upsertLeagues(s.db, []poe.League{{ID: "Standard", Realm: "pc"}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	_, haveCache, isFresh, _, waiter := s.ensureLeagues("pc", "main", "", 24*time.Hour, poeLeaguesFetchPriority, fetchPolicyAlways)
	if waiter == nil {
		t.Fatal("fetchPolicyAlways did not submit a fetch over a fresh cache entry")
	}
	if !haveCache || !isFresh {
		t.Errorf("haveCache=%v isFresh=%v, want both true (the cache genuinely was fresh)", haveCache, isFresh)
	}
}

// TestHandlePoeLeaguesList_FetchNever_PeekNeverCallsRemote proves the
// "never" fetch policy skips the HTTP call end-to-end through the handler.
func TestHandlePoeLeaguesList_FetchNever_PeekNeverCallsRemote(t *testing.T) {
	var calls int
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		calls++
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeaguesRequest{Fetch: "never"})
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeaguesPayload(t, recvResponse(t, c))
	if payload.Status != "miss" || payload.Freshness != "miss" || payload.Fetching {
		t.Errorf("payload = %+v, want status=freshness=miss fetching=false", payload)
	}
	if calls != 0 {
		t.Errorf("remote called %d times, want 0 (fetch:\"never\" must never fetch)", calls)
	}
}

func TestHandlePoeLeaguesList_BadFetchValue_ReturnsError(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeaguesRequest{Fetch: "whenever"})
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error for an unknown fetch value, got none")
	}
}

// --- includeCost ---

func TestHandlePoeLeaguesList_Wait_IncludeCost_ReturnsCost(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Header().Set("X-Rate-Limit-Policy", "leagues-policy")
		w.Header().Set("X-Rate-Limit-Rules", "R")
		w.Header().Set("X-Rate-Limit-R", "10:5:30")
		w.Header().Set("X-Rate-Limit-R-State", "3:5:0")
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeaguesRequest{Wait: true, IncludeCost: true})
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeaguesPayload(t, recvResponse(t, c))
	if payload.Cost == nil {
		t.Fatal("Cost = nil, want it populated when includeCost=true")
	}
	if payload.Cost.API != "poe-oauth" || payload.Cost.Policy != "leagues-policy" || payload.Cost.Queries != 1 {
		t.Errorf("Cost = %+v, want api=poe-oauth policy=leagues-policy queries=1", payload.Cost)
	}
}

func TestHandlePoeLeaguesList_Wait_NoIncludeCost_CostOmitted(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeaguesRequest{Wait: true})
	s.handlePoeLeaguesList(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeaguesPayload(t, recvResponse(t, c))
	if payload.Cost != nil {
		t.Errorf("Cost = %+v, want nil without includeCost", payload.Cost)
	}
}

// --- queryLeagueByName ---

func TestQueryLeagueByName_Found(t *testing.T) {
	db := openLeaguesTestDB(t)
	if err := upsertLeagues(db, []poe.League{{ID: "SSF Standard", Realm: "pc", Description: "desc"}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	league, _, haveCache, err := queryLeagueByName(db, "SSF Standard", "pc")
	if err != nil {
		t.Fatalf("queryLeagueByName: %v", err)
	}
	if !haveCache || league.Description != "desc" {
		t.Errorf("league=%+v haveCache=%v, want the seeded row", league, haveCache)
	}
}

func TestQueryLeagueByName_NotFound(t *testing.T) {
	db := openLeaguesTestDB(t)
	_, _, haveCache, err := queryLeagueByName(db, "Nonexistent", "pc")
	if err != nil {
		t.Fatalf("queryLeagueByName: %v", err)
	}
	if haveCache {
		t.Error("haveCache = true for a name never upserted, want false")
	}
}

// --- poe.leagues.detail ---

func TestHandlePoeLeaguesDetail_CacheHit_ReturnsFresh(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	if err := upsertLeagues(s.db, []poe.League{{ID: "Standard", Realm: "pc"}, {ID: "Hardcore", Realm: "pc"}}, time.Now()); err != nil {
		t.Fatalf("seed leagues: %v", err)
	}

	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeagueDetailRequest{Name: "Hardcore"})
	s.handlePoeLeaguesDetail(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeagueDetailPayload(t, recvResponse(t, c))
	if payload.Status != "fresh" || payload.League == nil || payload.League.Name != "Hardcore" {
		t.Errorf("payload = %+v, want a fresh hit on Hardcore", payload)
	}
}

func TestHandlePoeLeaguesDetail_NameRequired_ReturnsError(t *testing.T) {
	s := newPoeLeaguesTestServer(t, "")
	c := hub.NewClient()
	defer c.Close()
	s.handlePoeLeaguesDetail(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error with no name given, got none")
	}
}

// TestHandlePoeLeaguesDetail_Wait_SharesBulkFetchWithList proves a detail
// refresh reuses the exact same underlying bulk fetch poe.leagues.list
// uses (same reqqueue dedup key) — asserted indirectly by seeding the
// remote server to return several leagues and confirming the requested one
// is projected out correctly, plus that the fetch only hits the remote once
// even though the response covers every league, not just the one asked
// about.
func TestHandlePoeLeaguesDetail_Wait_ReturnsRequestedLeagueFromBulkFetch(t *testing.T) {
	var calls int
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		calls++
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"},{"id":"Hardcore","realm":"pc","description":"hc desc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeagueDetailRequest{Name: "Hardcore", Wait: true})
	s.handlePoeLeaguesDetail(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeagueDetailPayload(t, recvResponse(t, c))
	if payload.Status != "ok" || payload.League == nil || payload.League.Name != "Hardcore" || payload.League.Description != "hc desc" {
		t.Errorf("payload = %+v, want Hardcore projected out of the bulk fetch", payload)
	}
	if calls != 1 {
		t.Errorf("remote called %d times, want 1 (one bulk fetch, not a per-league call)", calls)
	}

	// The bulk fetch's other league (Standard) should also now be cached,
	// proving this really was the shared bulk fetch and not some
	// single-league-only call.
	if _, _, haveCache, err := queryLeagueByName(s.db, "Standard", "pc"); err != nil || !haveCache {
		t.Errorf("Standard cached=%v err=%v, want it cached too (from the same bulk fetch)", haveCache, err)
	}
}

// TestHandlePoeLeaguesDetail_Wait_NameNotInBulkResult_ReturnsMiss proves a
// refresh that completes without ever finding the requested name (wrong
// type/realm, or the league doesn't exist) reports a clean "miss" rather
// than an error.
func TestHandlePoeLeaguesDetail_Wait_NameNotInBulkResult_ReturnsMiss(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeagueDetailRequest{Name: "Totally Made Up League", Wait: true})
	s.handlePoeLeaguesDetail(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeagueDetailPayload(t, recvResponse(t, c))
	if payload.Status != "miss" || payload.Freshness != "miss" || payload.League != nil {
		t.Errorf("payload = %+v, want a clean miss", payload)
	}
}

func TestHandlePoeLeaguesDetail_NoCache_NoWait_ReturnsPending(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"}]}`))
	}))
	defer srv.Close()

	s := newPoeLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeLeagueDetailRequest{Name: "Standard"})
	s.handlePoeLeaguesDetail(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeLeagueDetailPayload(t, recvResponse(t, c))
	if payload.Status != "pending" || !payload.Fetching {
		t.Errorf("payload = %+v, want status=pending fetching=true", payload)
	}
}
