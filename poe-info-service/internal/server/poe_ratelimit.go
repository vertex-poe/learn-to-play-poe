package server

import (
	"context"
	"encoding/json"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

// estimatedRateLimitBuffer mirrors internal/reqqueue's own unexported
// rateLimitBuffer — duplicated here rather than exported from reqqueue
// since it's purely a display estimate for proto.RateLimitRule.ResetsAt,
// not something that needs to stay mechanically wired to the queue's
// actual dispatch timing.
const estimatedRateLimitBuffer = 1 * time.Second

// poeOAuthRateLimitHeaders implements reqqueue.HeaderParser for the PoE
// OAuth API's named-policy rate-limit headers
// (_reference/poe-apis/poe-apis.md §5.1): X-Rate-Limit-Policy names the
// policy, X-Rate-Limit-Rules lists its rule names, and each rule has an
// X-Rate-Limit-{Name} (limit) / X-Rate-Limit-{Name}-State (current) pair —
// each itself a comma-separated list of hits:period:restriction triples,
// since a single rule commonly carries both a fast/burst and a
// slow/sustained tier (§5.3). Every triple in both lists becomes its own
// reqqueue.Rule.
func poeOAuthRateLimitHeaders(h http.Header) (string, []reqqueue.Rule, bool) {
	policyKey := h.Get("X-Rate-Limit-Policy")
	if policyKey == "" {
		return "", nil, false
	}

	var rules []reqqueue.Rule
	for _, name := range strings.Split(h.Get("X-Rate-Limit-Rules"), ",") {
		name = strings.TrimSpace(name)
		if name == "" {
			continue
		}
		limits := parsePoeRateLimitTriples(h.Get("X-Rate-Limit-" + name))
		states := parsePoeRateLimitTriples(h.Get("X-Rate-Limit-" + name + "-State"))
		for i, lim := range limits {
			r := reqqueue.Rule{Name: name, Hits: lim.hits, Period: lim.period, Restriction: lim.restriction}
			if i < len(states) {
				r.StateHits = states[i].hits
			}
			rules = append(rules, r)
		}
	}
	return policyKey, rules, true
}

type poeRateLimitTriple struct {
	hits        int
	period      time.Duration
	restriction time.Duration
}

// parsePoeRateLimitTriples parses a comma-separated list of
// "hits:period:restriction" triples (poe-apis.md §5), e.g.
// "30:10:60,60:120:600" — period/restriction are seconds on the wire.
// Any malformed entry is skipped rather than aborting the whole header.
func parsePoeRateLimitTriples(v string) []poeRateLimitTriple {
	if v == "" {
		return nil
	}
	var out []poeRateLimitTriple
	for _, part := range strings.Split(v, ",") {
		fields := strings.Split(strings.TrimSpace(part), ":")
		if len(fields) != 3 {
			continue
		}
		hits, err1 := strconv.Atoi(fields[0])
		periodSecs, err2 := strconv.Atoi(fields[1])
		restrictionSecs, err3 := strconv.Atoi(fields[2])
		if err1 != nil || err2 != nil || err3 != nil {
			continue
		}
		out = append(out, poeRateLimitTriple{
			hits:        hits,
			period:      time.Duration(periodSecs) * time.Second,
			restriction: time.Duration(restrictionSecs) * time.Second,
		})
	}
	return out
}

// buildFetchCost computes a proto.FetchCost for one response's raw headers,
// for a caller that just performed a real fetch against the PoE OAuth API.
// nil if headers is nil — no response was ever received (e.g. a network
// error before reaching PoE's servers at all), so nothing was actually
// billed against the API's rate limit. Queries is always 1 here since every
// fetch in this service is a single HTTP round-trip; Policy/Rules are only
// populated when headers actually carried the rate-limit headers (ok from
// poeOAuthRateLimitHeaders) — a response that reached the server but,
// for whatever reason, didn't carry them still cost a query, it's just an
// unlabeled one.
func buildFetchCost(headers http.Header) *proto.FetchCost {
	if headers == nil {
		return nil
	}
	cost := &proto.FetchCost{API: "poe-oauth", Queries: 1}
	if policy, rules, ok := poeOAuthRateLimitHeaders(headers); ok {
		cost.Policy = policy
		cost.Rules = protoRateLimitRules(rules, time.Now())
	}
	return cost
}

// protoRateLimitRules converts reqqueue.Rule values to the wire shape,
// estimating each saturated rule's reset time the same way
// reqqueue.computeDelay does internally (full Period plus a fixed buffer) —
// see proto.RateLimitRule's doc comment for why this is only ever an
// estimate, never an exact rolling-window computation.
func protoRateLimitRules(rules []reqqueue.Rule, now time.Time) []proto.RateLimitRule {
	out := make([]proto.RateLimitRule, 0, len(rules))
	for _, r := range rules {
		remaining := r.Hits - r.StateHits
		if remaining < 0 {
			remaining = 0
		}
		wire := proto.RateLimitRule{
			Name:          r.Name,
			Limit:         r.Hits,
			Remaining:     remaining,
			PeriodSeconds: int(r.Period / time.Second),
		}
		if remaining == 0 && r.Hits > 0 && r.Period > 0 {
			wire.ResetsAt = now.Add(r.Period + estimatedRateLimitBuffer).Unix()
		}
		out = append(out, wire)
	}
	return out
}

// poeRateLimitStatusPayload converts the queue's Policies() snapshot to the
// "poe.ratelimit.status"/TopicPoeRateLimit wire shape.
func poeRateLimitStatusPayload(reports []reqqueue.PolicyReport) proto.PoeRateLimitStatusPayload {
	now := time.Now()
	policies := make([]proto.PoeRateLimitPolicyPayload, 0, len(reports))
	for _, r := range reports {
		p := proto.PoeRateLimitPolicyPayload{Policy: r.Policy, Rules: protoRateLimitRules(r.Rules, now)}
		if r.NextAllowedAt.After(now) {
			p.NextAllowedAt = r.NextAllowedAt.Unix()
		}
		policies = append(policies, p)
	}
	return proto.PoeRateLimitStatusPayload{Policies: policies}
}

// handlePoeRateLimitStatus serves "poe.ratelimit.status": every PoE OAuth
// rate-limit policy this service has learned about so far, straight from
// s.poeQueue's own bookkeeping (see internal/reqqueue.Queue.Policies) — no
// PoE OAuth API call of its own, so this is always free to call.
func (s *server) handlePoeRateLimitStatus(c *hub.Client, msg proto.Message) {
	var reports []reqqueue.PolicyReport
	if s.poeQueue != nil {
		reports = s.poeQueue.Policies()
	}
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(poeRateLimitStatusPayload(reports)),
	})
}

// publishPoeRateLimitStatus pushes the queue's current full policy snapshot
// to every TopicPoeRateLimit subscriber — always the full snapshot rather
// than just the one policy that changed, since a client watching this topic
// wants one coherent picture, not a diff to merge itself.
func (s *server) publishPoeRateLimitStatus() {
	if s.poeQueue == nil {
		return
	}
	msg, _ := json.Marshal(proto.Message{
		Type:    proto.TypeEvent,
		Topic:   proto.TopicPoeRateLimit,
		Payload: mustMarshal(poeRateLimitStatusPayload(s.poeQueue.Policies())),
	})
	s.hub.Publish(proto.TopicPoeRateLimit, msg)
}

// publishPoeRateLimitStatusAfter waits (bounded by waitTimeout) for w to
// resolve, then publishes the queue's now-updated policy snapshot.
// reqqueue only updates its internal policy state (learned from a
// completed Task's response headers) *after* that Task's Exec returns —
// see internal/reqqueue.Queue.dispatch — so this must run after Wait
// unblocks, never called from inside Exec itself (which would only ever
// observe the state as of *before* this fetch). Called unconditionally
// right after every PoE OAuth fetch is submitted (ensurePoeProfile,
// submitLeaguesFetch), independent of whether the request that triggered
// the fetch itself asked to block on the same Waiter — multiple Waiters on
// one entry all resolve together (see reqqueue.Waiter.Wait's doc comment),
// so this never delays or duplicates the caller's own wait.
func (s *server) publishPoeRateLimitStatusAfter(w *reqqueue.Waiter, waitTimeout time.Duration) {
	go func() {
		ctx, cancel := context.WithTimeout(s.rootCtx, waitTimeout)
		defer cancel()
		w.Wait(ctx)
		s.publishPoeRateLimitStatus()
	}()
}
