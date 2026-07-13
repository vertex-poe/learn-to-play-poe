package steam

import (
	"context"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

func TestSteamID3(t *testing.T) {
	tests := []struct {
		name    string
		id64    string
		want    string
		wantErr bool
	}{
		{name: "typical account", id64: "76561197960287930", want: "22202"},
		{name: "not a number", id64: "not-a-number", wantErr: true},
		{name: "below base offset", id64: "1", wantErr: true},
		{name: "exactly the base offset", id64: "76561197960265728", want: "0"},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			got, err := steamID3(tt.id64)
			if tt.wantErr {
				if err == nil {
					t.Fatalf("steamID3(%q) = %q, want error", tt.id64, got)
				}
				return
			}
			if err != nil {
				t.Fatalf("steamID3(%q) unexpected error: %v", tt.id64, err)
			}
			if got != tt.want {
				t.Errorf("steamID3(%q) = %q, want %q", tt.id64, got, tt.want)
			}
		})
	}
}

func TestValidateSteamID64(t *testing.T) {
	if _, err := ValidateSteamID64("76561197960287930"); err != nil {
		t.Errorf("ValidateSteamID64 of a well-formed id: unexpected error: %v", err)
	}
	if _, err := ValidateSteamID64("not-a-number"); err == nil {
		t.Error("ValidateSteamID64 of a non-numeric id: want error, got nil")
	}
	if _, err := ValidateSteamID64("1"); err == nil {
		t.Error("ValidateSteamID64 of an id below the base offset: want error, got nil")
	}
}

func newMiniprofileServer(t *testing.T, html string) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html")
		w.Write([]byte(html))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestFetchRichPresence(t *testing.T) {
	tests := []struct {
		name          string
		html          string
		knownGameName string
		want          string
	}{
		{
			name: "rich presence present, no known game name to check",
			html: testfixtures.SteamMiniprofileWithRichPresence,
			want: "SSF Ancestors: 92 Warden - The Sarn Encampment",
		},
		{
			name:          "rich presence present, known game name matches",
			html:          testfixtures.SteamMiniprofileWithRichPresence,
			knownGameName: "Path of Exile",
			want:          "SSF Ancestors: 92 Warden - The Sarn Encampment",
		},
		{
			name: "game with no rich presence span",
			html: testfixtures.SteamMiniprofileGameNoRichPresence,
			want: "",
		},
		{
			name: "no game at all",
			html: testfixtures.SteamMiniprofileNoGame,
			want: "",
		},
		{
			name:          "mismatch guard suppresses rich presence",
			html:          testfixtures.SteamMiniprofileMismatchedGame,
			knownGameName: "Path of Exile",
			want:          "",
		},
	}

	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			srv := newMiniprofileServer(t, tt.html)
			c := NewClient(nil, WithMiniprofileBaseURL(srv.URL))

			got, err := c.FetchRichPresence(context.Background(), "76561197960287930", tt.knownGameName)
			if err != nil {
				t.Fatalf("FetchRichPresence: unexpected error: %v", err)
			}
			if got != tt.want {
				t.Errorf("FetchRichPresence = %q, want %q", got, tt.want)
			}
		})
	}
}

func TestFetchRichPresenceBadSteamID(t *testing.T) {
	c := NewClient(nil, WithMiniprofileBaseURL("http://unused.invalid"))
	if _, err := c.FetchRichPresence(context.Background(), "not-a-number", ""); err == nil {
		t.Error("FetchRichPresence with an invalid steamid64: want error, got nil")
	}
}

func TestFetchRichPresenceNonOKStatus(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.WriteHeader(http.StatusInternalServerError)
	}))
	defer srv.Close()

	c := NewClient(nil, WithMiniprofileBaseURL(srv.URL))
	if _, err := c.FetchRichPresence(context.Background(), "76561197960287930", ""); err == nil {
		t.Error("FetchRichPresence against a 500: want error, got nil")
	}
}

func TestFetchRichPresenceRequestsExpectedURL(t *testing.T) {
	var gotPath string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		gotPath = r.URL.Path
		w.Write([]byte(testfixtures.SteamMiniprofileNoGame))
	}))
	defer srv.Close()

	c := NewClient(nil, WithMiniprofileBaseURL(srv.URL))
	if _, err := c.FetchRichPresence(context.Background(), "76561197960287930", ""); err != nil {
		t.Fatalf("FetchRichPresence: unexpected error: %v", err)
	}

	const wantPath = "/miniprofile/22202"
	if gotPath != wantPath {
		t.Errorf("requested path = %q, want %q", gotPath, wantPath)
	}
	if !strings.HasPrefix(wantPath, "/miniprofile/") {
		t.Fatalf("test sanity check failed: wantPath %q", wantPath)
	}
}

func TestParseRichPresence(t *testing.T) {
	tests := []struct {
		name      string
		raw       string
		wantOK    bool
		wantLg    string
		wantLevel int
		wantClass string
	}{
		{
			name:      "typical",
			raw:       "SSF Ancestors: 92 Warden - The Sarn Encampment",
			wantOK:    true,
			wantLg:    "SSF Ancestors",
			wantLevel: 92,
			wantClass: "Warden",
		},
		{
			name:      "single digit level",
			raw:       "Standard: 3 Witch - The Twilight Strand",
			wantOK:    true,
			wantLg:    "Standard",
			wantLevel: 3,
			wantClass: "Witch",
		},
		{
			name:      "no zone suffix",
			raw:       "SSF Ancestors: 92 Warden",
			wantOK:    true,
			wantLg:    "SSF Ancestors",
			wantLevel: 92,
			wantClass: "Warden",
		},
		{
			name: "empty",
			raw:  "",
		},
		{
			name: "unrecognized shape",
			raw:  "In Menus",
		},
	}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			league, level, class, ok := ParseRichPresence(tt.raw)
			if ok != tt.wantOK {
				t.Fatalf("ParseRichPresence(%q) ok = %v, want %v", tt.raw, ok, tt.wantOK)
			}
			if !tt.wantOK {
				return
			}
			if league != tt.wantLg || level != tt.wantLevel || class != tt.wantClass {
				t.Errorf("ParseRichPresence(%q) = (%q, %d, %q), want (%q, %d, %q)",
					tt.raw, league, level, class, tt.wantLg, tt.wantLevel, tt.wantClass)
			}
		})
	}
}
