package server

import (
	"context"
	"encoding/json"
	"fmt"
	"log"
	"net/http"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

// poeProfileCacheEntry is what's cached under poeProfileCacheKey(sub) via
// internal/store's generic TTL cache (SetCache/GetCache) — a lightweight
// per-account blob, not a database table row; /profile is inherently
// account-scoped, so the cache key is the account's OAuth uuid (the `sub`
// claim, stable across renames), not its display name.
type poeProfileCacheEntry struct {
	UUID      string `json:"uuid"`
	Name      string `json:"name"`
	Locale    string `json:"locale"`
	Twitch    string `json:"twitch"` // display name; empty if not linked
	FetchedAt int64  `json:"fetchedAt"`
}

// profileFetchResult is what a profile fetch Task hands back through
// reqqueue.Waiter.Wait — the freshly fetched/cached entry plus this
// specific call's FetchCost, so handlePoeProfileField's wait:true path
// doesn't need to recompute either.
type profileFetchResult struct {
	entry poeProfileCacheEntry
	cost  *proto.FetchCost
}

func poeProfileCacheKey(sub string) string { return "poe:profile:" + sub }

func (s *server) loadPoeProfileCache(sub string) (poeProfileCacheEntry, bool) {
	raw, ok, err := s.store.GetCache(poeProfileCacheKey(sub))
	if err != nil || !ok {
		return poeProfileCacheEntry{}, false
	}
	var entry poeProfileCacheEntry
	if err := json.Unmarshal(raw, &entry); err != nil {
		return poeProfileCacheEntry{}, false
	}
	return entry, true
}

func (s *server) savePoeProfileCache(entry poeProfileCacheEntry) {
	data, err := json.Marshal(entry)
	if err != nil {
		return
	}
	if err := s.store.SetCache(poeProfileCacheKey(entry.UUID), data, poeProfileStoreTTL); err != nil {
		log.Printf("poe profile: cache write failed: %v", err)
	}
}

// resolvePoeAccount resolves selector — a poe_uuid or an accounts.name — to
// that account's name, poe_uuid, and (only if it's the currently
// OAuth-active account) a usable access token. An empty selector resolves
// directly to the active account. This service holds only one live OAuth
// credential at a time (ADR-005), so a fetch can only ever be performed for
// whichever account is presently signed in — a different, previously-known
// account can still be served from cache (accessToken returned empty), just
// never freshly fetched, until multi-account credential switching exists
// (see ROADMAP.md's "Multi-account PoE OAuth support").
func (s *server) resolvePoeAccount(selector string) (name, sub, accessToken string, err error) {
	snap := s.poeOAuthSnapshot()
	var activeSub, activeName, activeToken string
	if snap.token != nil {
		activeSub, activeName, activeToken = snap.token.Sub, snap.token.Username, snap.token.AccessToken
	}

	if selector == "" {
		if snap.token == nil {
			return "", "", "", fmt.Errorf("not authenticated")
		}
		return activeName, activeSub, activeToken, nil
	}

	if s.db == nil {
		return "", "", "", fmt.Errorf("no db configured")
	}
	var gotName, gotUUID string
	row := s.db.QueryRow(`SELECT name, COALESCE(poe_uuid, '') FROM accounts WHERE name = ? OR poe_uuid = ?`, selector, selector)
	if err := row.Scan(&gotName, &gotUUID); err != nil {
		return "", "", "", fmt.Errorf("unknown account %q", selector)
	}
	if gotUUID != "" && gotUUID == activeSub {
		return gotName, gotUUID, activeToken, nil
	}
	return gotName, gotUUID, "", nil
}

// ensurePoeProfile reports sub's cached profile (haveCache/isFresh describe
// it — see freshnessLabel) and, depending on fetchPolicy, may also enqueue
// (de-duplicated by sub, via reqqueue's Key-based merge) a fresh /profile
// fetch at the given priority through s.poeQueue, returning the resulting
// Waiter:
//
//   - fetchPolicyNever never enqueues anything — cached is whatever's on
//     hand, however stale, and the caller is on its own for freshening it.
//   - fetchPolicyAlways always enqueues, even over an already-fresh entry.
//   - fetchPolicyIfStale (the default) enqueues only when the cache is
//     missing or older than maxAge.
//
// priority never affects any of the above, only how a fetch (once decided)
// is scheduled relative to other queued PoE OAuth requests. accessToken
// must be the currently active account's (see resolvePoeAccount) — if
// empty (a known but not-currently-authenticated account) a fetch can never
// be performed regardless of fetchPolicy, so waiter is nil either way; the
// caller decides whether that's an error (nothing at all to serve) or
// simply "serve the stale copy" (something to serve, just not freshenable
// right now).
func (s *server) ensurePoeProfile(sub, accessToken string, maxAge time.Duration, priority int, fetchPolicy string) (cached poeProfileCacheEntry, haveCache bool, isFresh bool, waiter *reqqueue.Waiter) {
	entry, ok := s.loadPoeProfileCache(sub)
	haveCache = ok
	isFresh = ok && time.Since(time.Unix(entry.FetchedAt, 0)) < maxAge

	needFetch := fetchPolicy == fetchPolicyAlways || (!isFresh && fetchPolicy != fetchPolicyNever)
	if !needFetch || accessToken == "" {
		return entry, haveCache, isFresh, nil
	}

	w := s.poeQueue.Submit(reqqueue.Task{
		Key:        poeProfileCacheKey(sub),
		Priority:   priority,
		PolicyHint: poeOAuthProfilePolicyHint,
		Exec: func(ctx context.Context) (any, http.Header, error) {
			profile, headers, err := s.poeClient.FetchProfile(ctx, accessToken)
			cost := buildFetchCost(headers)
			if err != nil {
				s.publishPoeProfileError(sub, err, cost)
				return nil, headers, err
			}
			newEntry := poeProfileCacheEntry{
				UUID:      profile.UUID,
				Name:      profile.Name,
				Locale:    profile.Locale,
				FetchedAt: time.Now().Unix(),
			}
			if profile.Twitch != nil {
				newEntry.Twitch = profile.Twitch.Name
			}
			s.savePoeProfileCache(newEntry)
			s.publishPoeProfile(newEntry, cost)
			return profileFetchResult{entry: newEntry, cost: cost}, headers, nil
		},
	})
	s.publishPoeRateLimitStatusAfter(w, poeProfileWaitTimeout)
	return entry, haveCache, isFresh, w
}

func (s *server) publishPoeProfile(entry poeProfileCacheEntry, cost *proto.FetchCost) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeProfile,
		Payload: mustMarshal(proto.PoeProfilePayload{
			PoeUUID:   entry.UUID,
			Name:      entry.Name,
			Locale:    entry.Locale,
			Twitch:    entry.Twitch,
			FetchedAt: entry.FetchedAt,
			Cost:      cost,
		}),
	})
	s.hub.Publish(proto.TopicPoeProfile, msg)
}

func (s *server) publishPoeProfileError(sub string, fetchErr error, cost *proto.FetchCost) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeProfile,
		Payload: mustMarshal(proto.PoeProfilePayload{
			PoeUUID: sub,
			Error:   fetchErr.Error(),
			Cost:    cost,
		}),
	})
	s.hub.Publish(proto.TopicPoeProfile, msg)
}

// poeProfileFieldRequest is the shared "poe.profile.locale"/
// "poe.profile.twitch" request shape. Account is an optional selector (a
// poe_uuid or an accounts.name — see the "Account selector" ROADMAP entry);
// omitting it defaults to the currently OAuth-active account.
// MaxAgeSeconds overrides this field's default cache TTL, clamped to
// poeProfileMinRefetchAge — this only ever affects whether a fetch happens
// at all, never how it's scheduled. Priority overrides this field's default
// reqqueue.Priority (poeProfileLocaleFetchPriority/poeProfileTwitchFetchPriority)
// for the fetch itself, if one is needed; it has no effect at all on a
// cache hit. Wait chooses blocking (true) vs. non-blocking (false, the
// default) delivery — see handlePoeProfileField. Fetch overrides whether a
// stale/missing cache is even allowed to trigger a fetch at all — see
// ensurePoeProfile's doc comment for "never"/"ifStale"/"always"; empty
// defaults to "ifStale" (this field's original, only-ever behavior).
// IncludeCost asks for FetchCost reporting on a response that actually
// performed a fetch — see proto.FetchCost's doc comment for why this is
// opt-in.
type poeProfileFieldRequest struct {
	Account       string `json:"account"`
	MaxAgeSeconds int64  `json:"maxAgeSeconds"`
	Priority      int    `json:"priority"`
	Wait          bool   `json:"wait"`
	Fetch         string `json:"fetch"`
	IncludeCost   bool   `json:"includeCost"`
}

// handlePoeProfileField serves both poe.profile.locale and
// poe.profile.twitch, differing only in defaultMaxAge/defaultPriority and
// which field of a fetched/cached profile to return (extract). Response
// Status/Freshness/Fetching follow proto.PoeProfileFieldPayload's
// vocabulary: a cache hit (fresh or stale) with no fetch happening responds
// immediately with Freshness="fresh"/"stale" and Fetching=false — Value is
// populated either way, since a stale value is still worth returning
// (see ensurePoeProfile's fetchPolicyNever/"peek" case in particular). A
// genuine miss with nothing to serve and no way to fetch (not authenticated
// for this account) is the one case that still surfaces as an error, exactly
// as before this field existed. Otherwise a needed fetch enqueues: with
// Wait=false (the default) the response is immediate ("pending",
// Fetching=true, Value/FetchedAt still carrying whatever was cached before
// this fetch) and the real value arrives later on TopicPoeProfile; with
// Wait=true, a bounded waiter goroutine blocks on the queued task instead
// of the WS connection's read loop, responding "ok" once it completes or
// falling back to "pending" on timeout so the caller still gets the topic
// delivery.
func (s *server) handlePoeProfileField(c *hub.Client, msg proto.Message, defaultMaxAge time.Duration, defaultPriority int, extract func(poeProfileCacheEntry) string) {
	var params poeProfileFieldRequest
	if len(msg.Payload) > 0 {
		if err := json.Unmarshal(msg.Payload, &params); err != nil {
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
			return
		}
	}

	fetchPolicy, err := normalizeFetchPolicy(params.Fetch)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}

	_, sub, accessToken, err := s.resolvePoeAccount(params.Account)
	if err != nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: err.Error()})
		return
	}

	maxAge := defaultMaxAge
	if params.MaxAgeSeconds > 0 {
		maxAge = time.Duration(params.MaxAgeSeconds) * time.Second
	}
	if maxAge < poeProfileMinRefetchAge {
		maxAge = poeProfileMinRefetchAge
	}

	priority := defaultPriority
	if params.Priority != 0 {
		priority = params.Priority
	}

	entry, haveCache, isFresh, waiter := s.ensurePoeProfile(sub, accessToken, maxAge, priority, fetchPolicy)
	freshness := freshnessLabel(haveCache, isFresh)

	if waiter == nil {
		if !haveCache && fetchPolicy != fetchPolicyNever && accessToken == "" {
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no cached profile, and not authenticated for this account"})
			return
		}
		payload := proto.PoeProfileFieldPayload{Status: freshness, Freshness: freshness, Fetching: false}
		if haveCache {
			payload.Value = extract(entry)
			payload.FetchedAt = entry.FetchedAt
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	if !params.Wait {
		payload := proto.PoeProfileFieldPayload{Status: "pending", Freshness: freshness, Fetching: true}
		if haveCache {
			payload.Value = extract(entry)
			payload.FetchedAt = entry.FetchedAt
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
		return
	}

	go func() {
		ctx, cancel := context.WithTimeout(s.rootCtx, poeProfileWaitTimeout)
		defer cancel()
		result, err := waiter.Wait(ctx)
		if err != nil {
			payload := proto.PoeProfileFieldPayload{Status: "pending", Freshness: freshness, Fetching: true, Error: err.Error()}
			if haveCache {
				payload.Value = extract(entry)
				payload.FetchedAt = entry.FetchedAt
			}
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
			return
		}
		fetched := result.(profileFetchResult)
		payload := proto.PoeProfileFieldPayload{
			Status: "ok", Freshness: "fresh", Fetching: false,
			Value: extract(fetched.entry), FetchedAt: fetched.entry.FetchedAt,
		}
		if params.IncludeCost {
			payload.Cost = fetched.cost
		}
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Payload: mustMarshal(payload)})
	}()
}

func (s *server) handlePoeProfileLocale(c *hub.Client, msg proto.Message) {
	s.handlePoeProfileField(c, msg, poeProfileLocaleCacheTTL, poeProfileLocaleFetchPriority, func(e poeProfileCacheEntry) string { return e.Locale })
}

func (s *server) handlePoeProfileTwitch(c *hub.Client, msg proto.Message) {
	s.handlePoeProfileField(c, msg, poeProfileTwitchCacheTTL, poeProfileTwitchFetchPriority, func(e poeProfileCacheEntry) string { return e.Twitch })
}
