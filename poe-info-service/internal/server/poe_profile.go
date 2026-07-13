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

// ensurePoeProfile returns sub's cached profile if it's within maxAge —
// priority never affects this cache check, only how a fetch is scheduled
// once one is actually needed — or enqueues (de-duplicated by sub, via
// reqqueue's Key-based merge) a fresh /profile fetch at the given priority
// through s.poeQueue and returns the resulting Waiter. If another request
// for the same sub is already queued/in flight, Submit promotes it to the
// higher of the two requested priorities rather than starting a second
// fetch. accessToken must be the currently active account's (see
// resolvePoeAccount) — if empty (a known but not-currently-authenticated
// account) and the cache is stale or missing, there is no way to freshen
// it, which the caller surfaces as an error.
func (s *server) ensurePoeProfile(sub, accessToken string, maxAge time.Duration, priority int) (cached poeProfileCacheEntry, waiter *reqqueue.Waiter, fresh bool) {
	if entry, ok := s.loadPoeProfileCache(sub); ok && time.Since(time.Unix(entry.FetchedAt, 0)) < maxAge {
		return entry, nil, true
	}
	if accessToken == "" {
		return poeProfileCacheEntry{}, nil, false
	}

	w := s.poeQueue.Submit(reqqueue.Task{
		Key:        poeProfileCacheKey(sub),
		Priority:   priority,
		PolicyHint: poeOAuthProfilePolicyHint,
		Exec: func(ctx context.Context) (any, http.Header, error) {
			profile, headers, err := s.poeClient.FetchProfile(ctx, accessToken)
			if err != nil {
				s.publishPoeProfileError(sub, err)
				return nil, headers, err
			}
			entry := poeProfileCacheEntry{
				UUID:      profile.UUID,
				Name:      profile.Name,
				Locale:    profile.Locale,
				FetchedAt: time.Now().Unix(),
			}
			if profile.Twitch != nil {
				entry.Twitch = profile.Twitch.Name
			}
			s.savePoeProfileCache(entry)
			s.publishPoeProfile(entry)
			return entry, headers, nil
		},
	})
	return poeProfileCacheEntry{}, w, false
}

func (s *server) publishPoeProfile(entry poeProfileCacheEntry) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeProfile,
		Payload: mustMarshal(proto.PoeProfilePayload{
			PoeUUID:   entry.UUID,
			Name:      entry.Name,
			Locale:    entry.Locale,
			Twitch:    entry.Twitch,
			FetchedAt: entry.FetchedAt,
		}),
	})
	s.hub.Publish(proto.TopicPoeProfile, msg)
}

func (s *server) publishPoeProfileError(sub string, fetchErr error) {
	msg, _ := json.Marshal(proto.Message{
		Type:  proto.TypeEvent,
		Topic: proto.TopicPoeProfile,
		Payload: mustMarshal(proto.PoeProfilePayload{
			PoeUUID: sub,
			Error:   fetchErr.Error(),
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
// default) delivery — see handlePoeProfileField.
type poeProfileFieldRequest struct {
	Account       string `json:"account"`
	MaxAgeSeconds int64  `json:"maxAgeSeconds"`
	Priority      int    `json:"priority"`
	Wait          bool   `json:"wait"`
}

// handlePoeProfileField serves both poe.profile.locale and
// poe.profile.twitch, differing only in defaultMaxAge/defaultPriority and
// which field of a fetched/cached profile to return (extract). A fresh
// cache hit responds immediately ("fresh") regardless of any requested
// priority — priority only ever influences how a needed fetch is scheduled
// relative to other queued PoE OAuth requests, never whether the cache is
// considered fresh enough. A stale-or-missing entry enqueues a fetch: with
// Wait=false (the default) the response is immediate ("pending") and the
// real value arrives later on TopicPoeProfile; with Wait=true, a bounded
// waiter goroutine blocks on the queued task instead of the WS connection's
// read loop (see internal/reqqueue and this feature's ROADMAP entry for why
// that distinction matters), responding "ok" once it completes or falling
// back to "pending" on timeout so the caller still gets the topic delivery.
func (s *server) handlePoeProfileField(c *hub.Client, msg proto.Message, defaultMaxAge time.Duration, defaultPriority int, extract func(poeProfileCacheEntry) string) {
	var params poeProfileFieldRequest
	if len(msg.Payload) > 0 {
		if err := json.Unmarshal(msg.Payload, &params); err != nil {
			s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "bad params: " + err.Error()})
			return
		}
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

	entry, waiter, fresh := s.ensurePoeProfile(sub, accessToken, maxAge, priority)
	if fresh {
		s.send(c, proto.Message{
			Type: proto.TypeResponse, ID: msg.ID,
			Payload: mustMarshal(proto.PoeProfileFieldPayload{Status: "fresh", Value: extract(entry), FetchedAt: entry.FetchedAt}),
		})
		return
	}
	if waiter == nil {
		s.send(c, proto.Message{Type: proto.TypeResponse, ID: msg.ID, Error: "no cached profile, and not authenticated for this account"})
		return
	}

	if !params.Wait {
		s.send(c, proto.Message{
			Type: proto.TypeResponse, ID: msg.ID,
			Payload: mustMarshal(proto.PoeProfileFieldPayload{Status: "pending"}),
		})
		return
	}

	go func() {
		ctx, cancel := context.WithTimeout(s.rootCtx, poeProfileWaitTimeout)
		defer cancel()
		result, err := waiter.Wait(ctx)
		if err != nil {
			s.send(c, proto.Message{
				Type: proto.TypeResponse, ID: msg.ID,
				Payload: mustMarshal(proto.PoeProfileFieldPayload{Status: "pending", Error: err.Error()}),
			})
			return
		}
		fetched := result.(poeProfileCacheEntry)
		s.send(c, proto.Message{
			Type: proto.TypeResponse, ID: msg.ID,
			Payload: mustMarshal(proto.PoeProfileFieldPayload{Status: "ok", Value: extract(fetched), FetchedAt: fetched.FetchedAt}),
		})
	}()
}

func (s *server) handlePoeProfileLocale(c *hub.Client, msg proto.Message) {
	s.handlePoeProfileField(c, msg, poeProfileLocaleCacheTTL, poeProfileLocaleFetchPriority, func(e poeProfileCacheEntry) string { return e.Locale })
}

func (s *server) handlePoeProfileTwitch(c *hub.Client, msg proto.Message) {
	s.handlePoeProfileField(c, msg, poeProfileTwitchCacheTTL, poeProfileTwitchFetchPriority, func(e poeProfileCacheEntry) string { return e.Twitch })
}
