package server

import (
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/reqqueue"
)

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
