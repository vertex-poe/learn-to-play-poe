package server

import (
	"context"
	"encoding/json"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/detect"
	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/ingest"
	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/steam"
)

const (
	// richPresenceCheckInterval is the ticker granularity watchRichPresence
	// re-evaluates at — deliberately much shorter than richPresencePollInterval
	// (the actual poll cadence, see below) so the goroutine stays responsive
	// to shutdown or a steam_id config change rather than blocking in a
	// single long sleep.
	richPresenceCheckInterval = 15 * time.Second

	// richPresencePollInterval is the default background poll cadence, now
	// that exactly one steamid is ever tracked (see isSteamInstall's doc
	// comment for why — this project supports only a single local
	// Steam-based PoE client today).
	richPresencePollInterval = 60 * time.Second

	// richPresenceRequestTTL bounds how often any trigger — a client
	// request, a Client.txt zone-transfer event from the Steam-associated
	// install, or the background poller — may cause an on-demand fetch: if
	// the last fetch attempt (regardless of what triggered it) was within
	// this long, the cached value is served/kept as-is instead of
	// contacting Steam again.
	richPresenceRequestTTL = 25 * time.Second

	// richPresenceCacheTTL is how long a fetched entry survives in
	// api_cache — generous on purpose, restart-survival only (so a client
	// asking right after a restart sees the last-known data instead of
	// "pending"), not intra-process deduplication (richPresenceRequestTTL
	// already handles that).
	richPresenceCacheTTL = 24 * time.Hour

	richPresenceCacheKey = "steam:richPresence"
)

// richPresenceState is the single tracked steam_id's most recently fetched
// rich-presence snapshot: the verbatim Steam text plus its parsed parts
// (league/level/class — see steam.ParseRichPresence; zone is intentionally
// not tracked here, since Client.txt zone-transfer events already cover it
// more authoritatively).
type richPresenceState struct {
	Raw       string
	League    string
	Level     int
	Class     string
	FetchedAt int64
	Status    string
	Error     string
}

// watchRichPresence periodically fetches rich presence for the configured
// steam_id and publishes any field that changed. Polling is subscriber-gated
// (ADR-007): Steam is a rate-limited, ToS-sensitive external resource, so an
// idle service with nobody listening on any of the rich-presence topics must
// not poll it.
func watchRichPresence(ctx context.Context, s *server) {
	watchRichPresenceWithIntervals(ctx, s, richPresenceCheckInterval, richPresencePollInterval)
}

// watchRichPresenceWithIntervals is watchRichPresence with its two intervals
// as parameters, so tests can drive the poller on a millisecond-scale
// cadence instead of waiting through the real 15s/60s production intervals.
func watchRichPresenceWithIntervals(ctx context.Context, s *server, checkInterval, pollInterval time.Duration) {
	ticker := time.NewTicker(checkInterval)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			if s.currentSteamID() == "" {
				continue
			}
			if !s.hub.HasSubscribers(proto.TopicSteamPresence) &&
				!s.hub.HasSubscribers(proto.TopicCharacterLevel) &&
				!s.hub.HasSubscribers(proto.TopicCharacterClass) &&
				!s.hub.HasSubscribers(proto.TopicLeague) {
				continue
			}
			if time.Since(s.lastRichPresenceFetch()) < pollInterval {
				continue
			}
			s.ensureFreshRichPresence(ctx)
		}
	}
}

func (s *server) lastRichPresenceFetch() time.Time {
	s.richPresenceMu.RLock()
	defer s.richPresenceMu.RUnlock()
	if s.richPresence.FetchedAt == 0 {
		return time.Time{}
	}
	return time.Unix(s.richPresence.FetchedAt, 0)
}

// ensureFreshRichPresence fetches and caches a new rich-presence snapshot
// only if the last fetch attempt (by any trigger) was more than
// richPresenceRequestTTL ago. Concurrent callers serialize on
// richPresenceFetchMu and re-check staleness once they acquire it, so a
// burst of triggers (e.g. a client request landing right after a zone
// transfer) collapses into a single outbound fetch.
func (s *server) ensureFreshRichPresence(ctx context.Context) {
	s.richPresenceFetchMu.Lock()
	defer s.richPresenceFetchMu.Unlock()

	if time.Since(s.lastRichPresenceFetch()) < richPresenceRequestTTL {
		return
	}
	s.fetchRichPresence(ctx)
}

// fetchRichPresence fetches the configured steam_id's rich-presence text,
// parses it, and stores/publishes whatever changed. A fetch error leaves the
// previous Raw/League/Level/Class in place (a transient Steam hiccup
// shouldn't blank out otherwise-still-valid data) but still updates
// FetchedAt/Status/Error, so richPresenceRequestTTL's gate isn't defeated by
// repeated failures.
func (s *server) fetchRichPresence(ctx context.Context) {
	id := s.currentSteamID()
	if id == "" {
		return
	}

	raw, err := s.steamClient.FetchRichPresence(ctx, id, "")
	now := time.Now()

	s.richPresenceMu.Lock()
	prev := s.richPresence
	next := prev
	next.FetchedAt = now.Unix()
	if err != nil {
		next.Status = proto.RichPresenceStatusError
		next.Error = err.Error()
	} else {
		next.Raw = raw
		next.Status = proto.RichPresenceStatusOK
		next.Error = ""
		league, level, class, ok := steam.ParseRichPresence(raw)
		if ok {
			next.League, next.Level, next.Class = league, level, class
		} else {
			next.League, next.Level, next.Class = "", 0, ""
		}
	}
	s.richPresence = next
	s.richPresenceMu.Unlock()

	if data, mErr := json.Marshal(next); mErr == nil {
		if cErr := s.store.SetCache(richPresenceCacheKey, data, richPresenceCacheTTL); cErr != nil {
			s.debugf("rich presence: cache write failed: %v", cErr)
		}
	}

	s.publishRichPresenceChanges(prev, next)
}

// publishRichPresenceChanges compares prev and next field-by-field,
// publishing only the topics whose value actually changed — a level-up must
// not also fire a spurious "league changed" event, and vice versa.
func (s *server) publishRichPresenceChanges(prev, next richPresenceState) {
	if prev.Raw != next.Raw || prev.Status != next.Status || prev.Error != next.Error {
		s.publish(proto.TopicSteamPresence, proto.RichPresencePayload{
			RichPresence: next.Raw,
			FetchedAt:    next.FetchedAt,
			Status:       next.Status,
			Error:        next.Error,
		})
	}
	if prev.League != next.League {
		s.publish(proto.TopicLeague, proto.LeaguePayload{
			League: next.League, Source: proto.RichPresenceSourceSteam, FetchedAt: next.FetchedAt,
		})
	}
	if prev.Level != next.Level {
		s.publish(proto.TopicCharacterLevel, proto.CharacterLevelPayload{
			Level: next.Level, Source: proto.RichPresenceSourceSteam, FetchedAt: next.FetchedAt,
		})
	}
	if prev.Class != next.Class {
		s.publish(proto.TopicCharacterClass, proto.CharacterClassPayload{
			Class: next.Class, Source: proto.RichPresenceSourceSteam, FetchedAt: next.FetchedAt,
		})
	}
}

// publish marshals payload into a proto.TypeEvent envelope and publishes it
// to topic — a small helper shared by every rich-presence topic above.
func (s *server) publish(topic string, payload any) {
	msg, _ := json.Marshal(proto.Message{
		Type:    proto.TypeEvent,
		Topic:   topic,
		Payload: mustMarshal(payload),
	})
	s.hub.Publish(topic, msg)
}

// hydrateRichPresenceCache loads any previously cached snapshot, so a client
// asking right after a service restart sees the last-known data instead of
// "pending" for a full poll cycle. Called once at startup, before any client
// can connect.
func (s *server) hydrateRichPresenceCache() {
	raw, ok, err := s.store.GetCache(richPresenceCacheKey)
	if err != nil || !ok {
		return
	}
	var entry richPresenceState
	if err := json.Unmarshal(raw, &entry); err != nil {
		return
	}
	s.richPresenceMu.Lock()
	s.richPresence = entry
	s.richPresenceMu.Unlock()
}

func (s *server) richPresenceSnapshot() richPresenceState {
	s.richPresenceMu.RLock()
	defer s.richPresenceMu.RUnlock()
	return s.richPresence
}

// richPresenceStatus returns snap's status, defaulting to "pending" for a
// zero-value snapshot (never fetched).
func richPresenceStatus(snap richPresenceState) string {
	if snap.Status == "" {
		return proto.RichPresenceStatusPending
	}
	return snap.Status
}

// requestFreshRichPresence is the shared prelude for every rich-presence
// request handler below: if a steam_id is configured, fetch first when the
// cached copy is more than richPresenceRequestTTL old (see
// ensureFreshRichPresence), then return the current snapshot. Unlike the old
// list-based steam.presence, which never fetched on request and required a
// prior subscription, every one of these requests always returns live-enough
// data on its own.
func (s *server) requestFreshRichPresence(ctx context.Context) richPresenceState {
	if s.currentSteamID() != "" {
		s.ensureFreshRichPresence(ctx)
	}
	return s.richPresenceSnapshot()
}

func (s *server) handleSteamPresence(c *hub.Client, msg proto.Message) {
	snap := s.requestFreshRichPresence(s.rootCtx)
	s.send(c, proto.Message{
		Type: proto.TypeResponse,
		ID:   msg.ID,
		Payload: mustMarshal(proto.RichPresencePayload{
			RichPresence: snap.Raw,
			FetchedAt:    snap.FetchedAt,
			Status:       richPresenceStatus(snap),
			Error:        snap.Error,
		}),
	})
}

func (s *server) handleCharacterLevel(c *hub.Client, msg proto.Message) {
	snap := s.requestFreshRichPresence(s.rootCtx)
	s.send(c, proto.Message{
		Type: proto.TypeResponse,
		ID:   msg.ID,
		Payload: mustMarshal(proto.CharacterLevelPayload{
			Level: snap.Level, Source: proto.RichPresenceSourceSteam, FetchedAt: snap.FetchedAt,
		}),
	})
}

func (s *server) handleCharacterClass(c *hub.Client, msg proto.Message) {
	snap := s.requestFreshRichPresence(s.rootCtx)
	s.send(c, proto.Message{
		Type: proto.TypeResponse,
		ID:   msg.ID,
		Payload: mustMarshal(proto.CharacterClassPayload{
			Class: snap.Class, Source: proto.RichPresenceSourceSteam, FetchedAt: snap.FetchedAt,
		}),
	})
}

func (s *server) handleLeague(c *hub.Client, msg proto.Message) {
	snap := s.requestFreshRichPresence(s.rootCtx)
	s.send(c, proto.Message{
		Type: proto.TypeResponse,
		ID:   msg.ID,
		Payload: mustMarshal(proto.LeaguePayload{
			League: snap.League, Source: proto.RichPresenceSourceSteam, FetchedAt: snap.FetchedAt,
		}),
	})
}

// isSteamInstall reports whether dir is the local Steam-based PoE client —
// this project supports only a single tracked steamid (and a single Steam
// Web API key isn't even used by rich presence at all — see
// CONTRIBUTING.md), so a Client.txt zone transfer only warrants an on-demand
// rich-presence fetch when it comes from that one Steam-associated install,
// not some other, non-Steam install that might also be tailed concurrently.
//
// Two independent signals, either sufficient on its own:
//   - dir's path lives under a Steam library ("…/steamapps/common/…", the
//     layout Steam always uses even for a custom library location).
//   - a currently running process matches one of execNames' Steam-flavored
//     entries (name containing "steam", e.g. PathOfExile_x64Steam.exe —
//     see config.DefaultExecutableNames) and resolves to this exact
//     directory.
func isSteamInstall(dir string, execNames []string) bool {
	if strings.Contains(strings.ToLower(dir), "steamapps") {
		return true
	}

	var steamNames []string
	for _, n := range execNames {
		if strings.Contains(strings.ToLower(n), "steam") {
			steamNames = append(steamNames, n)
		}
	}
	if len(steamNames) == 0 {
		return false
	}

	dirs, err := detect.Scan(steamNames)
	if err != nil {
		return false
	}
	want := ingest.NormalizeInstallPath(dir)
	for _, d := range dirs {
		if ingest.NormalizeInstallPath(d) == want {
			return true
		}
	}
	return false
}

// onAreaEntered returns broadcastLogEvents' live-event hook for one install:
// a zone transfer from the Steam-associated install (see isSteamInstall) is
// a strong signal the player's rich-presence text may have just changed, so
// it's worth an on-demand fetch — still subject to the same
// richPresenceRequestTTL gate every other trigger uses, so a burst of zone
// transfers doesn't spam Steam. Runs in its own goroutine since
// isSteamInstall does a process-table scan and fetchRichPresence makes an
// outbound HTTP call — neither should block the ingest pipeline.
func (s *server) onAreaEntered(inst InstallTarget) func(proto.ParsedEvent) {
	return func(evt proto.ParsedEvent) {
		if evt.Type != proto.EventAreaEntered {
			return
		}
		go func() {
			if !isSteamInstall(inst.Dir, s.currentExecutableNames()) {
				return
			}
			s.ensureFreshRichPresence(s.rootCtx)
		}()
	}
}
