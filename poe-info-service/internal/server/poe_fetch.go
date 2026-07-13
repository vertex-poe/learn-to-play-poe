package server

import "fmt"

// Fetch policy values for ensurePoeProfile/ensureLeagues's fetchPolicy
// parameter (the request-level "fetch" field on poe.profile.*/
// poe.leagues.*) — orthogonal to maxAge. maxAge decides whether an existing
// cache entry counts as fresh; fetchPolicy decides whether a stale or
// missing entry is even allowed to trigger a real, rate-limit-budget-
// spending fetch:
//
//   - fetchPolicyIfStale (default): today's original behavior — fetch only
//     if the cache is missing or older than maxAge.
//   - fetchPolicyNever: a read-only peek — return whatever's cached (fresh,
//     stale, or nothing at all), and never submit a fetch, regardless of
//     maxAge.
//   - fetchPolicyAlways: force a fresh fetch even if the cache already
//     looks fresh enough.
const (
	fetchPolicyIfStale = "ifStale"
	fetchPolicyNever   = "never"
	fetchPolicyAlways  = "always"
)

// normalizeFetchPolicy validates a request's raw "fetch" field, defaulting
// an empty string to fetchPolicyIfStale (preserving every existing caller's
// behavior from before this field existed).
func normalizeFetchPolicy(raw string) (string, error) {
	switch raw {
	case "":
		return fetchPolicyIfStale, nil
	case fetchPolicyIfStale, fetchPolicyNever, fetchPolicyAlways:
		return raw, nil
	default:
		return "", fmt.Errorf("unknown fetch policy %q (want %q, %q, or %q)", raw, fetchPolicyIfStale, fetchPolicyNever, fetchPolicyAlways)
	}
}

// freshnessLabel derives the "fresh"/"stale"/"miss" vocabulary (see
// proto.PoeProfileFieldPayload's doc comment) from a cache lookup's two
// booleans: haveCache (something is cached at all) and isFresh (it's within
// the requested max-age). Shared by ensurePoeProfile/handlePoeProfileField
// and ensureLeagues/handlePoeLeaguesList/handlePoeLeaguesDetail so the two
// endpoints can't drift on what these words mean.
func freshnessLabel(haveCache, isFresh bool) string {
	switch {
	case !haveCache:
		return "miss"
	case isFresh:
		return "fresh"
	default:
		return "stale"
	}
}
