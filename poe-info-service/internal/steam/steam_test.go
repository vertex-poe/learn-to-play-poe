package steam

import (
	"context"
	"net/http"
	"net/http/httptest"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

// newTestClient builds a Client wired to two independently controllable
// httptest.Servers, one per Steam data source, mirroring how a real Client
// talks to api.steampowered.com and steamcommunity.com separately.
func newTestClient(t *testing.T, official, miniprofile string) *Client {
	t.Helper()
	return NewClient(nil, WithOfficialBaseURL(official), WithMiniprofileBaseURL(miniprofile))
}

func htmlServer(t *testing.T, html string) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(html))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func jsonServer(t *testing.T, json string) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(json))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func failingServer(t *testing.T) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestFetchPresenceBothSourcesSucceedAndAgree(t *testing.T) {
	official := jsonServer(t, testfixtures.SteamPlayerSummariesInGame)
	mini := htmlServer(t, testfixtures.SteamMiniprofileWithRichPresence)
	c := newTestClient(t, official.URL, mini.URL)

	off, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}

	p, err := c.FetchPresence(context.Background(), "76561197960287930", off)
	if err != nil {
		t.Fatalf("FetchPresence: unexpected error: %v", err)
	}
	if p.GameName != "Path of Exile" || !p.InGame {
		t.Errorf("official fields = %+v, want GameName=Path of Exile, InGame=true", p)
	}
	if p.RichPresence != "SSF Ancestors: 92 Warden - The Sarn Encampment" {
		t.Errorf("RichPresence = %q, want the fixture's rich presence text", p.RichPresence)
	}
}

func TestFetchPresenceNoAPIKeyStillGetsRichPresence(t *testing.T) {
	mini := htmlServer(t, testfixtures.SteamMiniprofileWithRichPresence)
	c := newTestClient(t, "http://unused.invalid", mini.URL)

	// No official result at all (nil map) — mirrors what the poller passes
	// through when no steamApiKey credential is stored.
	p, err := c.FetchPresence(context.Background(), "76561197960287930", nil)
	if err != nil {
		t.Fatalf("FetchPresence: unexpected error: %v", err)
	}
	if p.GameName != "" || p.InGame {
		t.Errorf("official fields = %+v, want zero-valued without an API key", p)
	}
	if p.RichPresence != "SSF Ancestors: 92 Warden - The Sarn Encampment" {
		t.Errorf("RichPresence = %q, want the fixture's rich presence text even with no API key", p.RichPresence)
	}
}

func TestFetchPresenceMismatchGuardSuppressesRichPresence(t *testing.T) {
	official := jsonServer(t, testfixtures.SteamPlayerSummariesInGame) // reports "Path of Exile"
	mini := htmlServer(t, testfixtures.SteamMiniprofileMismatchedGame) // page says a different game
	c := newTestClient(t, official.URL, mini.URL)

	off, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}

	p, err := c.FetchPresence(context.Background(), "76561197960287930", off)
	if err != nil {
		t.Fatalf("FetchPresence: unexpected error: %v", err)
	}
	if p.GameName != "Path of Exile" {
		t.Errorf("official GameName = %q, want it unaffected by the scrape mismatch", p.GameName)
	}
	if p.RichPresence != "" {
		t.Errorf("RichPresence = %q, want suppressed by the mismatch guard", p.RichPresence)
	}
}

func TestFetchPresenceScrapeFailureStillReturnsOfficialFields(t *testing.T) {
	official := jsonServer(t, testfixtures.SteamPlayerSummariesInGame)
	mini := failingServer(t)
	c := newTestClient(t, official.URL, mini.URL)

	off, err := c.FetchOfficial(context.Background(), "key", []string{"76561197960287930"})
	if err != nil {
		t.Fatalf("FetchOfficial: unexpected error: %v", err)
	}

	p, err := c.FetchPresence(context.Background(), "76561197960287930", off)
	if err == nil {
		t.Fatal("FetchPresence with a failing scrape: want error, got nil")
	}
	if p.GameName != "Path of Exile" || !p.InGame {
		t.Errorf("official fields = %+v, want them populated despite the scrape failure", p)
	}
}
