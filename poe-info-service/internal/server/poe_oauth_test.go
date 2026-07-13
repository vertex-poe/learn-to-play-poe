package server

import (
	"encoding/json"
	"testing"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/hub"
	"github.com/MovingCairn/poe-info-service/internal/poe"
	"github.com/MovingCairn/poe-info-service/internal/proto"
)

// Tests below exercise poe_oauth.go's in-memory logic only — never
// loadPoeOAuthToken/savePoeOAuthToken/clearPoeOAuthToken, which are the sole
// points this package touches the real OS credential store. That mirrors
// this package's existing convention of leaving the credentials.store/
// has/delete handlers themselves untested here (see CONTRIBUTING.md's
// "Windows Credential Manager during tests" note) — automated tests must
// never risk deleting or depending on a real stored credential.

func TestPoeOAuthStatusPayload_NoToken_Unauthorized(t *testing.T) {
	snap := poeOAuthSnapshot{}
	payload := snap.statusPayload(time.Now())

	if payload.Authorized {
		t.Error("Authorized = true with no token, want false")
	}
	if payload.Username != "" || payload.Scope != "" || payload.AccessExpiration != 0 {
		t.Errorf("expected no token metadata with no token, got %+v", payload)
	}
}

func TestPoeOAuthStatusPayload_ValidToken_Authorized(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := &poe.Token{
		Username:          "SomeAccount",
		Scope:             "account:leagues",
		AccessExpiration:  now.Unix() + 3600,
		RefreshExpiration: now.Unix() + int64(7*24*time.Hour/time.Second),
	}
	snap := poeOAuthSnapshot{token: tok}
	payload := snap.statusPayload(now)

	if !payload.Authorized {
		t.Fatal("Authorized = false with a valid token, want true")
	}
	if payload.Username != "SomeAccount" {
		t.Errorf("Username = %q, want SomeAccount", payload.Username)
	}
	if payload.Scope != "account:leagues" {
		t.Errorf("Scope = %q, want account:leagues", payload.Scope)
	}
	if payload.AccessExpiration != tok.AccessExpiration {
		t.Errorf("AccessExpiration = %d, want %d", payload.AccessExpiration, tok.AccessExpiration)
	}
}

// TestPoeOAuthStatusPayload_TokenPastRefreshWindow_Unauthorized proves a
// token that has outlived the assumed 7-day refresh-token lifetime reports
// as unauthorized (re-login required) even though a Token struct is still
// present — Authorized reflects whether the token is actually still usable,
// not merely whether one was ever obtained.
func TestPoeOAuthStatusPayload_TokenPastRefreshWindow_Unauthorized(t *testing.T) {
	now := time.Unix(1_700_000_000, 0)
	tok := &poe.Token{
		Username:          "StaleAccount",
		RefreshExpiration: now.Unix() - 1, // already expired
	}
	snap := poeOAuthSnapshot{token: tok}
	payload := snap.statusPayload(now)

	if payload.Authorized {
		t.Error("Authorized = true for a token past its refresh window, want false")
	}
	if payload.Username != "" {
		t.Errorf("Username = %q for an unauthorized status, want empty", payload.Username)
	}
}

func TestPoeOAuthStatusPayload_PassesThroughInProgressAndError(t *testing.T) {
	snap := poeOAuthSnapshot{inProgress: true, lastError: "network error"}
	payload := snap.statusPayload(time.Now())

	if !payload.InProgress {
		t.Error("InProgress = false, want true")
	}
	if payload.Error != "network error" {
		t.Errorf("Error = %q, want %q", payload.Error, "network error")
	}
}

// TestSchedulePoeOAuthRefresh_ReplacesExistingTimer proves a second call
// stops the first timer rather than leaving two scheduled refreshes racing
// each other (e.g. a login immediately followed by a startup hydration, or
// two logins in quick succession).
func TestSchedulePoeOAuthRefresh_ReplacesExistingTimer(t *testing.T) {
	srv := &server{}
	farFuture := time.Now().Add(24 * time.Hour).Unix()
	tok := poe.Token{AccessExpiration: farFuture}

	srv.schedulePoeOAuthRefresh(tok)
	first := srv.poeOAuth.refreshTimer
	if first == nil {
		t.Fatal("schedulePoeOAuthRefresh did not set a timer")
	}

	srv.schedulePoeOAuthRefresh(tok)
	second := srv.poeOAuth.refreshTimer
	if second == nil {
		t.Fatal("second schedulePoeOAuthRefresh did not set a timer")
	}
	if first == second {
		t.Error("second schedulePoeOAuthRefresh reused the same timer instead of replacing it")
	}
	// first should have been stopped by the second call — Stop returns
	// false if it already fired/was already stopped.
	if first.Stop() {
		t.Error("first timer was still active after being superseded")
	}

	t.Cleanup(func() { second.Stop() })
}

// TestHandlePoeOAuthLogin_AlreadyInProgress_DoesNotRestart proves a second
// login request while one is already running responds with started=false
// and leaves the in-flight flow's state untouched, rather than kicking off
// a second concurrent poe.Login (which would touch the real credential
// store on success — see this file's top comment for why that's avoided).
func TestHandlePoeOAuthLogin_AlreadyInProgress_DoesNotRestart(t *testing.T) {
	srv := &server{hub: hub.New()}
	srv.poeOAuth.inProgress = true

	c := hub.NewClient()
	defer c.Close()

	srv.handlePoeOAuthLogin(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.oauth.login"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		var payload struct {
			Started bool `json:"started"`
		}
		if err := json.Unmarshal(resp.Payload, &payload); err != nil {
			t.Fatalf("unmarshal payload: %v", err)
		}
		if payload.Started {
			t.Error("started = true while a login was already in progress, want false")
		}
	default:
		t.Fatal("handlePoeOAuthLogin sent no response")
	}

	if !srv.poeOAuth.inProgress {
		t.Error("inProgress flipped to false by the rejected second call")
	}
}

// TestHandlePoeOAuthStatus_ReflectsInMemoryState proves the poe.oauth.status
// handler serves straight from in-memory state (never touching the
// credential store itself — only the login/refresh/logout paths that
// populate that state do).
func TestHandlePoeOAuthStatus_ReflectsInMemoryState(t *testing.T) {
	srv := &server{hub: hub.New()}
	now := time.Now()
	srv.poeOAuth.token = &poe.Token{
		Username:          "SomeAccount",
		AccessExpiration:  now.Add(time.Hour).Unix(),
		RefreshExpiration: now.Add(24 * time.Hour).Unix(),
	}

	c := hub.NewClient()
	defer c.Close()

	srv.handlePoeOAuthStatus(c, proto.Message{Type: proto.TypeRequest, ID: "req-1", Method: "poe.oauth.status"})

	select {
	case data := <-c.Send:
		var resp proto.Message
		if err := json.Unmarshal(data, &resp); err != nil {
			t.Fatalf("unmarshal response: %v", err)
		}
		var payload proto.PoeOAuthStatusPayload
		if err := json.Unmarshal(resp.Payload, &payload); err != nil {
			t.Fatalf("unmarshal payload: %v", err)
		}
		if !payload.Authorized || payload.Username != "SomeAccount" {
			t.Errorf("status payload = %+v, want authorized=true username=SomeAccount", payload)
		}
	default:
		t.Fatal("handlePoeOAuthStatus sent no response")
	}
}
