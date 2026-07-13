package poe

import (
	"testing"
	"time"
)

func TestNewToken_ComputesDerivedFields(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	resp := tokenResponse{
		AccessToken:  "at",
		RefreshToken: "rt",
		ExpiresIn:    3600,
		TokenType:    "Bearer",
		Scope:        "account:leagues account:stashes account:characters",
		Username:     "SomeAccount",
		Sub:          "uuid-1234",
	}

	tok := newToken(resp, now)

	if tok.AccessToken != "at" {
		t.Errorf("AccessToken = %q, want %q", tok.AccessToken, "at")
	}
	if tok.RefreshToken != "rt" {
		t.Errorf("RefreshToken = %q, want %q", tok.RefreshToken, "rt")
	}
	if tok.Birthday != now.Unix() {
		t.Errorf("Birthday = %d, want %d", tok.Birthday, now.Unix())
	}
	wantAccessExp := now.Unix() + 3600
	if tok.AccessExpiration != wantAccessExp {
		t.Errorf("AccessExpiration = %d, want %d", tok.AccessExpiration, wantAccessExp)
	}
	wantRefreshExp := now.Unix() + int64(7*24*time.Hour/time.Second)
	if tok.RefreshExpiration != wantRefreshExp {
		t.Errorf("RefreshExpiration = %d, want %d", tok.RefreshExpiration, wantRefreshExp)
	}
}

func TestToken_NeedsRefresh(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := Token{AccessExpiration: now.Unix() + 600} // 10 minutes out

	if tok.NeedsRefresh(now) {
		t.Error("NeedsRefresh true 10 minutes before expiry, want false")
	}
	if !tok.NeedsRefresh(now.Add(6 * time.Minute)) {
		t.Error("NeedsRefresh false inside the 5-minute early window, want true")
	}
	if !tok.NeedsRefresh(now.Add(10 * time.Minute)) {
		t.Error("NeedsRefresh false after expiry, want true")
	}
}

func TestToken_PastRefreshWindow(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := Token{RefreshExpiration: now.Unix() + int64(7*24*time.Hour/time.Second)}

	if tok.PastRefreshWindow(now) {
		t.Error("PastRefreshWindow true immediately after issuance, want false")
	}
	if tok.PastRefreshWindow(now.Add(6*24*time.Hour + 23*time.Hour)) {
		t.Error("PastRefreshWindow true just under 7 days, want false")
	}
	if !tok.PastRefreshWindow(now.Add(7*24*time.Hour + time.Second)) {
		t.Error("PastRefreshWindow false just over 7 days, want true")
	}
}

func TestToken_RefreshAt(t *testing.T) {
	tok := Token{AccessExpiration: 1_700_003_600}
	want := time.Unix(1_700_003_600-300, 0)
	if got := tok.RefreshAt(); !got.Equal(want) {
		t.Errorf("RefreshAt = %v, want %v", got, want)
	}
}
