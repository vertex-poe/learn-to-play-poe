package server

import (
	"context"
	"database/sql"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"sync"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
	"github.com/MovingCairn/poe-info-service/internal/schema"
	"github.com/MovingCairn/poe-info-service/internal/store"
)

// newPoeProfileTestServer builds a *server with everything poe_profile.go
// touches: a real schema'd in-memory db, a real store.Store (so the generic
// TTL cache behaves exactly as in production), a real reqqueue.Queue using
// the real OAuth header parser, and a poeClient pointed at profileURL
// (normally an httptest.Server). rootCtx is cancelled on test cleanup,
// which also stops the queue's dispatch loop.
func newPoeProfileTestServer(t *testing.T, profileURL string) *server {
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
	st, err := store.New(db)
	if err != nil {
		t.Fatalf("new store: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	t.Cleanup(cancel)

	return &server{
		db:        db,
		store:     st,
		hub:       hub.New(),
		rootCtx:   ctx,
		poeClient: poe.NewClient(nil, poe.WithProfileURL(profileURL)),
		poeQueue:  reqqueue.New(ctx, poeOAuthRateLimitHeaders),
	}
}

func (s *server) setActiveToken(sub, username, accessToken string) {
	s.poeOAuth.mu.Lock()
	s.poeOAuth.token = &poe.Token{Sub: sub, Username: username, AccessToken: accessToken}
	s.poeOAuth.mu.Unlock()
}

func recvResponse(t *testing.T, c *hub.Client) proto.Message {
	t.Helper()
	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		return resp
	case <-time.After(2 * time.Second):
		t.Fatal("no response received")
		return proto.Message{}
	}
}

func decodeFieldPayload(t *testing.T, resp proto.Message) proto.PoeProfileFieldPayload {
	t.Helper()
	var payload proto.PoeProfileFieldPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	return payload
}

// --- resolvePoeAccount ---

func TestResolvePoeAccount_EmptySelector_UsesActiveAccount(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	s.setActiveToken("uuid-1", "SomeAccount", "the-token")

	name, sub, token, err := s.resolvePoeAccount("")
	if err != nil {
		t.Fatalf("resolvePoeAccount: %v", err)
	}
	if name != "SomeAccount" || sub != "uuid-1" || token != "the-token" {
		t.Errorf("got (%q, %q, %q), want (SomeAccount, uuid-1, the-token)", name, sub, token)
	}
}

func TestResolvePoeAccount_EmptySelector_NotAuthenticated_Errors(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	if _, _, _, err := s.resolvePoeAccount(""); err == nil {
		t.Error("expected an error with no active account, got nil")
	}
}

func TestResolvePoeAccount_SelectorMatchesActiveAccount_ReturnsAccessToken(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	s.setActiveToken("uuid-1", "SomeAccount", "the-token")
	if _, err := s.db.Exec(`INSERT INTO accounts(name, poe_uuid) VALUES(?, ?)`, "SomeAccount", "uuid-1"); err != nil {
		t.Fatalf("seed account: %v", err)
	}

	// Selector by name.
	name, sub, token, err := s.resolvePoeAccount("SomeAccount")
	if err != nil || name != "SomeAccount" || sub != "uuid-1" || token != "the-token" {
		t.Errorf("by name: got (%q,%q,%q,%v)", name, sub, token, err)
	}

	// Selector by uuid.
	name, sub, token, err = s.resolvePoeAccount("uuid-1")
	if err != nil || name != "SomeAccount" || sub != "uuid-1" || token != "the-token" {
		t.Errorf("by uuid: got (%q,%q,%q,%v)", name, sub, token, err)
	}
}

// TestResolvePoeAccount_SelectorMatchesKnownButInactiveAccount_NoAccessToken
// proves a selector naming a real, known account that isn't the currently
// signed-in one resolves successfully but with no access token — this
// service holds only one live credential at a time (ADR-005).
func TestResolvePoeAccount_SelectorMatchesKnownButInactiveAccount_NoAccessToken(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	s.setActiveToken("uuid-active", "ActiveAccount", "active-token")
	if _, err := s.db.Exec(`INSERT INTO accounts(name, poe_uuid) VALUES(?, ?)`, "OtherAccount", "uuid-other"); err != nil {
		t.Fatalf("seed account: %v", err)
	}

	name, sub, token, err := s.resolvePoeAccount("OtherAccount")
	if err != nil {
		t.Fatalf("resolvePoeAccount: %v", err)
	}
	if name != "OtherAccount" || sub != "uuid-other" || token != "" {
		t.Errorf("got (%q,%q,%q), want (OtherAccount, uuid-other, \"\")", name, sub, token)
	}
}

func TestResolvePoeAccount_UnknownSelector_Errors(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	if _, _, _, err := s.resolvePoeAccount("nobody-by-this-name"); err == nil {
		t.Error("expected an error for an unknown selector, got nil")
	}
}

// --- handlePoeProfileLocale / handlePoeProfileTwitch ---

func TestHandlePoeProfileLocale_CacheHit_ReturnsFresh(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	s.setActiveToken("uuid-1", "SomeAccount", "token")
	s.savePoeProfileCache(poeProfileCacheEntry{UUID: "uuid-1", Name: "SomeAccount", Locale: "en_US", FetchedAt: time.Now().Unix()})

	c := hub.NewClient()
	defer c.Close()
	s.handlePoeProfileLocale(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	payload := decodeFieldPayload(t, recvResponse(t, c))
	if payload.Status != "fresh" || payload.Value != "en_US" {
		t.Errorf("payload = %+v, want status=fresh value=en_US", payload)
	}
}

// TestHandlePoeProfileTwitch_NoCache_NoWait_ReturnsPendingThenPublishes proves
// the non-blocking path: an immediate "pending" response, followed by the
// real value arriving on TopicPoeProfile once the background fetch (against
// a real httptest server standing in for api.pathofexile.com) completes.
func TestHandlePoeProfileTwitch_NoCache_NoWait_ReturnsPendingThenPublishes(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"uuid":"uuid-1","name":"SomeAccount","locale":"en_US","twitch":{"name":"someaccount_tv"}}`))
	}))
	defer srv.Close()

	s := newPoeProfileTestServer(t, srv.URL)
	s.setActiveToken("uuid-1", "SomeAccount", "token")

	c := hub.NewClient()
	defer c.Close()
	s.hub.Subscribe(c, proto.TopicPoeProfile)

	s.handlePoeProfileTwitch(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	first := decodeFieldPayload(t, recvResponse(t, c))
	if first.Status != "pending" {
		t.Fatalf("first response status = %q, want pending", first.Status)
	}

	// The topic push (fetch completion) arrives next on the same channel.
	pushed := recvResponse(t, c)
	if pushed.Type != proto.TypeEvent || pushed.Topic != proto.TopicPoeProfile {
		t.Fatalf("expected a TopicPoeProfile event, got %+v", pushed)
	}
	var profilePayload proto.PoeProfilePayload
	if err := json.Unmarshal(pushed.Payload, &profilePayload); err != nil {
		t.Fatalf("unmarshal profile payload: %v", err)
	}
	if profilePayload.Twitch != "someaccount_tv" {
		t.Errorf("published Twitch = %q, want someaccount_tv", profilePayload.Twitch)
	}
}

// TestHandlePoeProfileLocale_Wait_ReturnsOkInline proves the blocking path
// delivers the fetched value directly on the same request/response, tagged
// "ok" rather than "pending".
func TestHandlePoeProfileLocale_Wait_ReturnsOkInline(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"uuid":"uuid-1","name":"SomeAccount","locale":"fr_FR"}`))
	}))
	defer srv.Close()

	s := newPoeProfileTestServer(t, srv.URL)
	s.setActiveToken("uuid-1", "SomeAccount", "token")

	c := hub.NewClient()
	defer c.Close()

	payloadBytes, _ := json.Marshal(poeProfileFieldRequest{Wait: true})
	s.handlePoeProfileLocale(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	payload := decodeFieldPayload(t, recvResponse(t, c))
	if payload.Status != "ok" || payload.Value != "fr_FR" {
		t.Errorf("payload = %+v, want status=ok value=fr_FR", payload)
	}
}

// TestHandlePoeProfileField_NoCacheNotAuthenticated_ReturnsError proves a
// selector naming a known-but-inactive account with no cached profile
// yields an explicit error rather than silently hanging or returning
// pending for a fetch that can never happen.
func TestHandlePoeProfileField_NoCacheNotAuthenticated_ReturnsError(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	s.setActiveToken("uuid-active", "ActiveAccount", "active-token")
	if _, err := s.db.Exec(`INSERT INTO accounts(name, poe_uuid) VALUES(?, ?)`, "OtherAccount", "uuid-other"); err != nil {
		t.Fatalf("seed account: %v", err)
	}

	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poeProfileFieldRequest{Account: "OtherAccount"})
	s.handlePoeProfileLocale(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error, got none")
	}
}

func TestHandlePoeProfileField_BadParams_ReturnsError(t *testing.T) {
	s := newPoeProfileTestServer(t, "")
	c := hub.NewClient()
	defer c.Close()
	s.handlePoeProfileLocale(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: json.RawMessage(`{not valid json`)})

	resp := recvResponse(t, c)
	if resp.Error == "" {
		t.Error("expected an error for malformed params, got none")
	}
}

// TestEnsurePoeProfile_PriorityAffectsDispatchOrderUnderContention is an
// end-to-end proof (real reqqueue.Queue, real OAuth header parser, real
// httptest server) that ensurePoeProfile's priority argument actually
// reaches the queue and governs dispatch order: with the shared /profile
// policy saturated, a high-priority fetch (locale's default) dispatches
// before a low-priority one (twitch's default) queued up behind the same
// gate, even though the low-priority one was submitted first.
func TestEnsurePoeProfile_PriorityAffectsDispatchOrderUnderContention(t *testing.T) {
	first := true
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		if first {
			first = false
			// Saturate the shared policy so subsequent same-hint fetches queue up.
			w.Header().Set("X-Rate-Limit-Policy", "profile-policy")
			w.Header().Set("X-Rate-Limit-Rules", "R")
			w.Header().Set("X-Rate-Limit-R", "1:1:5")
			w.Header().Set("X-Rate-Limit-R-State", "1:1:0")
		}
		w.Write([]byte(`{"uuid":"whoever","name":"Whoever","locale":"en_US"}`))
	}))
	defer srv.Close()

	s := newPoeProfileTestServer(t, srv.URL)

	// Seed the policy via a throwaway fetch so it's saturated before the
	// two contended fetches below.
	_, seedWaiter, seedFresh := s.ensurePoeProfile("seed-sub", "token", 0, reqqueue.PriorityMedium)
	if seedFresh || seedWaiter == nil {
		t.Fatal("expected the seed fetch to actually enqueue")
	}
	// The real header parser reads period/restriction as whole seconds
	// (poe-apis.md's actual wire format), so the smallest meaningful gate
	// this test can construct is 1s period + the 1s buffer = 2s — the
	// context needs comfortable headroom above that, not just barely over it.
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	if _, err := seedWaiter.Wait(ctx); err != nil {
		t.Fatalf("seed fetch: %v", err)
	}
	time.Sleep(20 * time.Millisecond)

	var mu sync.Mutex
	var order []string
	start := time.Now()
	record := func(name string) {
		mu.Lock()
		order = append(order, name)
		mu.Unlock()
	}

	// Low-priority "twitch" fetch for a different sub, submitted first.
	_, lowWaiter, _ := s.ensurePoeProfile("sub-low", "token", 0, poeProfileTwitchFetchPriority)
	// High-priority "locale" fetch for yet another sub, submitted second.
	_, highWaiter, _ := s.ensurePoeProfile("sub-high", "token", 0, poeProfileLocaleFetchPriority)

	go func() { lowWaiter.Wait(ctx); record("low") }()
	go func() { highWaiter.Wait(ctx); record("high") }()

	deadline := time.After(5 * time.Second)
	for {
		mu.Lock()
		done := len(order) == 2
		mu.Unlock()
		if done {
			break
		}
		select {
		case <-deadline:
			t.Fatal("fetches never completed")
		case <-time.After(10 * time.Millisecond):
		}
	}

	mu.Lock()
	defer mu.Unlock()
	if len(order) != 2 || order[0] != "high" || order[1] != "low" {
		t.Errorf("dispatch order = %v (took %v total), want [high low]", order, time.Since(start))
	}
}
