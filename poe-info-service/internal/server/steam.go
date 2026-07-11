package server

import (
	"context"
	"encoding/json"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/creds"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/steam"
)

const (
	// steamPollCheckInterval is the ticker granularity watchSteamPresence
	// re-evaluates at — deliberately much shorter than steamPollBaseInterval
	// (the actual per-cycle cadence, see below) so the goroutine stays
	// responsive to shutdown, a steam_ids config change, or a client
	// subscribing/unsubscribing, rather than blocking in a single long
	// sleep the way the reference implementation's poll loop does.
	steamPollCheckInterval = 30 * time.Second

	// steamPollBaseInterval is the minimum spacing between full poll cycles
	// per tracked id; the effective cadence is this multiplied by the
	// number of currently configured steam_ids, matching the reference
	// implementation's 30s * len(userIDs) — more tracked ids means each one
	// is polled less often, rather than polling all of them more
	// aggressively.
	steamPollBaseInterval = 30 * time.Second

	// steamCacheTTL is how long a fetched entry survives in api_cache.
	// Generous on purpose: its only job is restart-survival (so a client
	// asking steam.presence right after a restart sees the last-known data
	// instead of "pending" for up to a full poll cycle), not intra-process
	// deduplication — the poller's own cadence already handles that.
	steamCacheTTL = 24 * time.Hour

	// steamAPIKeyCredKey is the credentials.store key a client stores a
	// Steam Web API key under to unlock official-API fields. See
	// CONTRIBUTING.md's "Steam presence" section for the full contract.
	steamAPIKeyCredKey = "steamApiKey"

	steamCacheKeyPrefix = "steam:presence:"
)

// watchSteamPresence periodically fetches Steam presence for every
// currently configured steam_ids entry and publishes the result to
// proto.TopicSteamPresence. It only ever contacts Steam while at least one
// client is subscribed to that topic (Hub.HasSubscribers) — Steam is a
// rate-limited, ToS-sensitive external resource, so an idle service with
// nobody listening must not poll it. See docs/decisions/007 for the general
// policy this follows.
func watchSteamPresence(ctx context.Context, s *server) {
	watchSteamPresenceWithIntervals(ctx, s, steamPollCheckInterval, steamPollBaseInterval)
}

// watchSteamPresenceWithIntervals is watchSteamPresence with its two
// intervals as parameters, so tests can drive the poller on a
// millisecond-scale cadence instead of waiting through the real 30s-and-up
// production intervals.
func watchSteamPresenceWithIntervals(ctx context.Context, s *server, checkInterval, baseInterval time.Duration) {
	ticker := time.NewTicker(checkInterval)
	defer ticker.Stop()

	var lastFetch time.Time
	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if !s.hub.HasSubscribers(proto.TopicSteamPresence) {
				continue
			}
			ids := s.currentSteamIDs()
			if len(ids) == 0 {
				continue
			}
			effective := baseInterval * time.Duration(len(ids))
			if !lastFetch.IsZero() && time.Since(lastFetch) < effective {
				continue
			}
			s.runSteamPollCycle(ctx, ids)
			lastFetch = time.Now()
		}
	}
}

// runSteamPollCycle fetches fresh presence for every id in ids: one batched
// official-API call (skipped entirely if no steamApiKey credential is
// stored — that's an expected steady state, not logged as an error), then
// one throttled rich-presence scrape per id, in sequence rather than in
// parallel so every outbound call still passes through the client's single
// shared throttle. Each result is written to the in-memory snapshot,
// persisted to the cache for restart-survival, and the full snapshot is
// published once at the end.
func (s *server) runSteamPollCycle(ctx context.Context, ids []string) {
	apiKey, err := creds.Get(creds.ServiceName, steamAPIKeyCredKey)
	if err != nil && err != creds.ErrNotFound {
		s.debugf("steam presence: reading steamApiKey credential failed: %v", err)
	}

	official, err := s.steamClient.FetchOfficial(ctx, apiKey, ids)
	if err != nil {
		// Official fields just stay empty for this cycle; the per-id scrape
		// below still runs regardless — the two sources degrade
		// independently (see internal/steam's package doc).
		s.debugf("steam presence: official API fetch failed: %v", err)
		official = nil
	}

	for _, id := range ids {
		if ctx.Err() != nil {
			return
		}
		entry := s.fetchSteamEntry(ctx, id, official)
		s.storeSteamEntry(entry)
	}

	s.publishSteamPresence()
}

// fetchSteamEntry fetches one id's combined presence, translating a
// steam.Client error into a Status: "error" entry rather than propagating
// it — one id's fetch failure must never abort the poll cycle for the
// others.
func (s *server) fetchSteamEntry(ctx context.Context, id string, official map[string]steam.OfficialResult) proto.SteamPresenceEntry {
	presence, err := s.steamClient.FetchPresence(ctx, id, official)
	if err != nil {
		return proto.SteamPresenceEntry{
			SteamID64: id,
			FetchedAt: time.Now().Unix(),
			Status:    proto.SteamPresenceStatusError,
			Error:     err.Error(),
		}
	}
	return proto.SteamPresenceEntry{
		SteamID64:    presence.SteamID64,
		PersonaName:  presence.PersonaName,
		GameName:     presence.GameName,
		GameAppID:    presence.GameAppID,
		InGame:       presence.InGame,
		RichPresence: presence.RichPresence,
		FetchedAt:    time.Now().Unix(),
		Status:       proto.SteamPresenceStatusOK,
	}
}

func (s *server) storeSteamEntry(entry proto.SteamPresenceEntry) {
	s.steamMu.Lock()
	s.steamPresence[entry.SteamID64] = entry
	s.steamMu.Unlock()

	if raw, err := json.Marshal(entry); err == nil {
		if err := s.store.SetCache(steamCacheKeyPrefix+entry.SteamID64, raw, steamCacheTTL); err != nil {
			s.debugf("steam presence: cache write failed for %s: %v", entry.SteamID64, err)
		}
	}
}

// hydrateSteamPresenceCache loads any previously cached entry for each
// configured id, so a client asking steam.presence right after a service
// restart sees the last-known data instead of "pending" for a full poll
// cycle. Called once at startup, before any client can connect.
func (s *server) hydrateSteamPresenceCache(ids []string) {
	for _, id := range ids {
		raw, ok, err := s.store.GetCache(steamCacheKeyPrefix + id)
		if err != nil || !ok {
			continue
		}
		var entry proto.SteamPresenceEntry
		if err := json.Unmarshal(raw, &entry); err != nil {
			continue
		}
		s.steamPresence[id] = entry
	}
}

func (s *server) publishSteamPresence() {
	msg, _ := json.Marshal(proto.Message{
		Type:    proto.TypeEvent,
		Topic:   proto.TopicSteamPresence,
		Payload: mustMarshal(s.steamPresenceSnapshot()),
	})
	s.hub.Publish(proto.TopicSteamPresence, msg)
}

// steamPresenceSnapshot builds the current steam.presence response: one
// entry per configured id, in configured order, synthesizing a "pending"
// entry for any id that hasn't been fetched (or cache-hydrated) yet.
func (s *server) steamPresenceSnapshot() proto.SteamPresencePayload {
	ids := s.currentSteamIDs()

	s.steamMu.RLock()
	defer s.steamMu.RUnlock()

	entries := make([]proto.SteamPresenceEntry, 0, len(ids))
	for _, id := range ids {
		if entry, ok := s.steamPresence[id]; ok {
			entries = append(entries, entry)
			continue
		}
		entries = append(entries, proto.SteamPresenceEntry{
			SteamID64: id,
			Status:    proto.SteamPresenceStatusPending,
		})
	}
	return proto.SteamPresencePayload{Entries: entries}
}

// handleSteamPresence returns the server's current in-memory snapshot. It
// never triggers a fetch itself — only watchSteamPresence does, gated on
// having at least one subscriber to proto.TopicSteamPresence — so a client
// must subscribe to that topic to get live data; requesting steam.presence
// without subscribing will keep seeing "pending"/stale entries.
func (s *server) handleSteamPresence(c *hub.Client, msg proto.Message) {
	s.send(c, proto.Message{
		Type:    proto.TypeResponse,
		ID:      msg.ID,
		Payload: mustMarshal(s.steamPresenceSnapshot()),
	})
}
