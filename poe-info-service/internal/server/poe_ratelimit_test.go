package server

import (
	"net/http"
	"testing"
	"time"
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
