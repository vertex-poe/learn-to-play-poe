package server

import (
	"time"

	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

// Tunables introduced by the PoE OAuth data-fetching work (the reqqueue-
// backed /profile endpoint and whatever follows it — characters, stash,
// leagues) are colocated here so they're easy to see in aggregate and
// adjust in one place. This is distinct from internal/poe/oauth.go's
// existing protocol constants (ClientID, Scopes, AuthURL/TokenURL/
// ProfileURL) — those are fixed facts about the OAuth protocol itself, not
// tunable knobs.
const (
	// poeProfileLocaleCacheTTL and poeProfileTwitchCacheTTL are the default
	// max-age a poe.profile.locale/poe.profile.twitch request accepts
	// before triggering a refetch, absent an explicit maxAgeSeconds
	// override. Both fields come from the same /profile call, so whichever
	// request actually triggers a refetch refreshes both — these only gate
	// when that's worth doing. locale is close to immutable in practice; a
	// Twitch link/unlink is a deliberate, if still infrequent, user action.
	poeProfileLocaleCacheTTL = 30 * 24 * time.Hour
	poeProfileTwitchCacheTTL = 7 * 24 * time.Hour

	// poeProfileMinRefetchAge is the floor a caller's requested
	// maxAgeSeconds is clamped to, regardless of how fresh they ask for —
	// prevents a caller from forcing repeated /profile calls against PoE's
	// rate-limited API.
	poeProfileMinRefetchAge = 1 * time.Hour

	// poeProfileStoreTTL is how long a fetched profile survives in
	// api_cache — generous on purpose, restart-survival only (so a client
	// asking right after a service restart sees the last-known data
	// instead of nothing), not the actual staleness gate (the
	// poeProfile*CacheTTL / maxAgeSeconds pair already handle that) — same
	// convention as steam.go's richPresenceCacheTTL.
	poeProfileStoreTTL = 90 * 24 * time.Hour

	// poeProfileLocaleFetchPriority and poeProfileTwitchFetchPriority are
	// each field's default reqqueue.Priority when a caller doesn't specify
	// its own (see poeProfileFieldRequest.Priority) — locale is commonly
	// needed for immediate UI decisions (e.g. localizing displayed text),
	// while a Twitch link is more of a nice-to-have background detail, so
	// it defaults lower.
	poeProfileLocaleFetchPriority = reqqueue.PriorityHigh
	poeProfileTwitchFetchPriority = reqqueue.PriorityLow

	// poeOAuthProfilePolicyHint groups every /profile fetch under one
	// reqqueue policy hint before any response has revealed the OAuth
	// API's real rate-limit policy name for this endpoint (see
	// reqqueue.Task.PolicyHint's doc comment) — a stable label, not a
	// prediction of the server's actual policy name.
	poeOAuthProfilePolicyHint = "poe-oauth:/profile"

	// poeProfileWaitTimeout bounds how long a wait:true poe.profile.*
	// request blocks for before falling back to a pending response.
	poeProfileWaitTimeout = 30 * time.Second
)
