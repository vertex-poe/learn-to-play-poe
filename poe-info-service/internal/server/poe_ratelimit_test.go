package server

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

func TestPoeOAuthRateLimitHeaders_NoPolicyHeader_NotOK(t *testing.T) {
	h := http.Header{}
	_, _, ok := poeOAuthRateLimitHeaders(h)
	if ok {
		t.Error("expected ok=false with no X-Rate-Limit-Policy header")
	}
}

// TestPoeOAuthRateLimitHeaders_ParsesMultiTierRule proves a single named
// rule carrying both a fast and slow tier (poe-apis.md §5.3) becomes two
// reqqueue.Rules, each paired with its matching -State entry.
func TestPoeOAuthRateLimitHeaders_ParsesMultiTierRule(t *testing.T) {
	h := http.Header{}
	h.Set("X-Rate-Limit-Policy", "account-data")
	h.Set("X-Rate-Limit-Rules", "IpRestriction")
	h.Set("X-Rate-Limit-IpRestriction", "10:5:10,30:10:300")
	h.Set("X-Rate-Limit-IpRestriction-State", "1:5:0,2:10:0")

	key, rules, ok := poeOAuthRateLimitHeaders(h)
	if !ok {
		t.Fatal("expected ok=true")
	}
	if key != "account-data" {
		t.Errorf("policy key = %q, want account-data", key)
	}
	if len(rules) != 2 {
		t.Fatalf("got %d rules, want 2", len(rules))
	}
	if rules[0].Hits != 10 || rules[0].Period != 5*time.Second || rules[0].Restriction != 10*time.Second || rules[0].StateHits != 1 {
		t.Errorf("fast tier = %+v", rules[0])
	}
	if rules[1].Hits != 30 || rules[1].Period != 10*time.Second || rules[1].Restriction != 300*time.Second || rules[1].StateHits != 2 {
		t.Errorf("slow tier = %+v", rules[1])
	}
}

// TestPoeOAuthRateLimitHeaders_MultipleRuleNames proves several named rules
// (e.g. an IP-scoped rule and an account-scoped rule under one policy) all
// get parsed.
func TestPoeOAuthRateLimitHeaders_MultipleRuleNames(t *testing.T) {
	h := http.Header{}
	h.Set("X-Rate-Limit-Policy", "account-data")
	h.Set("X-Rate-Limit-Rules", "IpRestriction, AccountRestriction")
	h.Set("X-Rate-Limit-IpRestriction", "20:5:30")
	h.Set("X-Rate-Limit-IpRestriction-State", "1:5:0")
	h.Set("X-Rate-Limit-AccountRestriction", "10:5:10")
	h.Set("X-Rate-Limit-AccountRestriction-State", "10:5:0")

	_, rules, ok := poeOAuthRateLimitHeaders(h)
	if !ok || len(rules) != 2 {
		t.Fatalf("ok=%v rules=%+v, want 2 rules", ok, rules)
	}
	names := map[string]bool{rules[0].Name: true, rules[1].Name: true}
	if !names["IpRestriction"] || !names["AccountRestriction"] {
		t.Errorf("rule names = %v, want both IpRestriction and AccountRestriction", names)
	}
}

func TestParsePoeRateLimitTriples_SkipsMalformedEntries(t *testing.T) {
	got := parsePoeRateLimitTriples("10:5:10,not-a-triple,30:10:300")
	if len(got) != 2 {
		t.Fatalf("got %d triples, want 2 (malformed entry skipped)", len(got))
	}
	if got[0].hits != 10 || got[1].hits != 30 {
		t.Errorf("triples = %+v", got)
	}
}

func TestParsePoeRateLimitTriples_Empty(t *testing.T) {
	if got := parsePoeRateLimitTriples(""); got != nil {
		t.Errorf("parsePoeRateLimitTriples(\"\") = %+v, want nil", got)
	}
}

// --- buildFetchCost ---

func TestBuildFetchCost_NilHeaders_NilCost(t *testing.T) {
	if got := buildFetchCost(nil); got != nil {
		t.Errorf("buildFetchCost(nil) = %+v, want nil (no response was ever received)", got)
	}
}

// TestBuildFetchCost_HeadersWithoutRateLimitInfo_StillReportsOneQuery proves
// a response that reached the server but, for whatever reason, carries no
// rate-limit headers still counts as one billed query — it just can't be
// labeled with a policy.
func TestBuildFetchCost_HeadersWithoutRateLimitInfo_StillReportsOneQuery(t *testing.T) {
	cost := buildFetchCost(http.Header{})
	if cost == nil {
		t.Fatal("buildFetchCost(empty headers) = nil, want a Cost with Queries=1")
	}
	if cost.API != "poe-oauth" || cost.Queries != 1 || cost.Policy != "" || cost.Rules != nil {
		t.Errorf("cost = %+v, want api=poe-oauth queries=1 policy=\"\" rules=nil", cost)
	}
}

func TestBuildFetchCost_FullHeaders_PopulatesPolicyAndRules(t *testing.T) {
	h := http.Header{}
	h.Set("X-Rate-Limit-Policy", "profile-policy")
	h.Set("X-Rate-Limit-Rules", "R")
	h.Set("X-Rate-Limit-R", "30:10:60")
	h.Set("X-Rate-Limit-R-State", "5:10:0")

	cost := buildFetchCost(h)
	if cost == nil {
		t.Fatal("buildFetchCost = nil, want a populated Cost")
	}
	if cost.Policy != "profile-policy" || cost.Queries != 1 {
		t.Errorf("cost = %+v, want policy=profile-policy queries=1", cost)
	}
	if len(cost.Rules) != 1 || cost.Rules[0].Limit != 30 || cost.Rules[0].Remaining != 25 || cost.Rules[0].PeriodSeconds != 10 {
		t.Errorf("cost.Rules = %+v, want one rule limit=30 remaining=25 periodSeconds=10", cost.Rules)
	}
	if cost.Rules[0].ResetsAt != 0 {
		t.Errorf("ResetsAt = %d, want 0 (not saturated)", cost.Rules[0].ResetsAt)
	}
}

// --- protoRateLimitRules ---

func TestProtoRateLimitRules_Saturated_SetsResetsAt(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	rules := []reqqueue.Rule{{Name: "R", Hits: 10, Period: 5 * time.Second, StateHits: 10}}
	got := protoRateLimitRules(rules, now)
	if len(got) != 1 {
		t.Fatalf("got %d rules, want 1", len(got))
	}
	if got[0].Remaining != 0 {
		t.Errorf("Remaining = %d, want 0 (saturated)", got[0].Remaining)
	}
	wantResetsAt := now.Add(5*time.Second + estimatedRateLimitBuffer).Unix()
	if got[0].ResetsAt != wantResetsAt {
		t.Errorf("ResetsAt = %d, want %d (Period + estimatedRateLimitBuffer)", got[0].ResetsAt, wantResetsAt)
	}
}

func TestProtoRateLimitRules_Headroom_NoResetsAt(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	rules := []reqqueue.Rule{{Name: "R", Hits: 10, Period: 5 * time.Second, StateHits: 3}}
	got := protoRateLimitRules(rules, now)
	if len(got) != 1 || got[0].Remaining != 7 {
		t.Fatalf("got %+v, want one rule with Remaining=7", got)
	}
	if got[0].ResetsAt != 0 {
		t.Errorf("ResetsAt = %d, want 0 (not saturated, no known reset time)", got[0].ResetsAt)
	}
}

// TestProtoRateLimitRules_OverSaturated_RemainingFlooredAtZero proves
// StateHits exceeding Hits (a burst that overran between this client's last
// two observations) never reports a negative Remaining.
func TestProtoRateLimitRules_OverSaturated_RemainingFlooredAtZero(t *testing.T) {
	rules := []reqqueue.Rule{{Name: "R", Hits: 10, Period: time.Second, StateHits: 15}}
	got := protoRateLimitRules(rules, time.Now())
	if len(got) != 1 || got[0].Remaining != 0 {
		t.Fatalf("got %+v, want Remaining=0 (floored, not negative)", got)
	}
}

// --- poe.ratelimit.status ---

// TestHandlePoeRateLimitStatus_NilQueue_ReturnsEmptyPolicies proves a
// server with no poeQueue configured (e.g. a narrowly-constructed test
// server) reports an empty policy list rather than panicking.
func TestHandlePoeRateLimitStatus_NilQueue_ReturnsEmptyPolicies(t *testing.T) {
	srv := &server{hub: hub.New()}
	c := hub.NewClient()
	defer c.Close()
	srv.handlePoeRateLimitStatus(c, proto.Message{Type: proto.TypeRequest, ID: "req-1"})

	resp := recvResponse(t, c)
	var payload proto.PoeRateLimitStatusPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if len(payload.Policies) != 0 {
		t.Errorf("Policies = %+v, want empty", payload.Policies)
	}
}

// TestHandlePoeRateLimitStatus_ReflectsQueueState is an end-to-end proof
// (real reqqueue.Queue, real httptest server) that a completed PoE OAuth
// fetch's rate-limit state shows up in poe.ratelimit.status afterward.
func TestHandlePoeRateLimitStatus_ReflectsQueueState(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Header().Set("X-Rate-Limit-Policy", "leagues-policy")
		w.Header().Set("X-Rate-Limit-Rules", "R")
		w.Header().Set("X-Rate-Limit-R", "10:5:30")
		w.Header().Set("X-Rate-Limit-R-State", "4:5:0")
		w.Write([]byte(`[{"id":"Standard","realm":"pc"}]`))
	}))
	defer srv.Close()

	s := newPoePublicLeaguesTestServer(t, srv.URL)
	c := hub.NewClient()
	defer c.Close()
	payloadBytes, _ := json.Marshal(poePublicLeaguesRequest{Wait: true})
	s.handlePoeLeaguesPublic(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})
	recvResponse(t, c) // drain the list response itself

	s.handlePoeRateLimitStatus(c, proto.Message{Type: proto.TypeRequest, ID: "req-2"})
	resp := recvResponse(t, c)
	var payload proto.PoeRateLimitStatusPayload
	if err := json.Unmarshal(resp.Payload, &payload); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	if len(payload.Policies) != 1 || payload.Policies[0].Policy != "leagues-policy" {
		t.Fatalf("Policies = %+v, want one entry for leagues-policy", payload.Policies)
	}
	if len(payload.Policies[0].Rules) != 1 || payload.Policies[0].Rules[0].Remaining != 6 {
		t.Errorf("Rules = %+v, want one rule with Remaining=6 (10-4)", payload.Policies[0].Rules)
	}
}

// TestPublishPoeRateLimitStatus_PushesToSubscribers proves a fetch's
// completion publishes an updated snapshot to TopicPoeRateLimit — a client
// watching that topic sees live budget without polling
// poe.ratelimit.status.
func TestPublishPoeRateLimitStatus_PushesToSubscribers(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Header().Set("X-Rate-Limit-Policy", "profile-policy")
		w.Header().Set("X-Rate-Limit-Rules", "R")
		w.Header().Set("X-Rate-Limit-R", "30:10:60")
		w.Header().Set("X-Rate-Limit-R-State", "1:10:0")
		w.Write([]byte(`{"uuid":"uuid-1","name":"SomeAccount","locale":"en_US"}`))
	}))
	defer srv.Close()

	s := newPoeProfileTestServer(t, srv.URL)
	s.setActiveToken("uuid-1", "SomeAccount", "token")

	c := hub.NewClient()
	defer c.Close()
	s.hub.Subscribe(c, proto.TopicPoeRateLimit)

	payloadBytes, _ := json.Marshal(poeProfileFieldRequest{Wait: true})
	s.handlePoeProfileLocale(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Payload: payloadBytes})

	// publishPoeRateLimitStatusAfter runs in its own goroutine, reading the
	// same Waiter the handler's own wait:true path does — both unblock off
	// the same closed channel with no ordering guarantee between them, so
	// this drains until both the request's own response and the
	// TopicPoeRateLimit push have arrived, in whichever order they land,
	// rather than assuming one specific order.
	var sawResponse, sawEvent bool
	var eventPayload proto.PoeRateLimitStatusPayload
	deadline := time.After(2 * time.Second)
	for !sawResponse || !sawEvent {
		select {
		case data := <-c.Send:
			var msg proto.Message
			if err := json.Unmarshal(data, &msg); err != nil {
				t.Fatalf("unmarshal message: %v", err)
			}
			switch {
			case msg.Type == proto.TypeResponse:
				sawResponse = true
			case msg.Type == proto.TypeEvent && msg.Topic == proto.TopicPoeRateLimit:
				sawEvent = true
				if err := json.Unmarshal(msg.Payload, &eventPayload); err != nil {
					t.Fatalf("unmarshal event payload: %v", err)
				}
			}
		case <-deadline:
			t.Fatalf("timed out: sawResponse=%v sawEvent=%v", sawResponse, sawEvent)
		}
	}

	if len(eventPayload.Policies) != 1 || eventPayload.Policies[0].Policy != "profile-policy" {
		t.Errorf("Policies = %+v, want one entry for profile-policy", eventPayload.Policies)
	}
}
