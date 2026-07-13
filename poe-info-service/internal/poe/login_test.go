package poe

import (
	"context"
	"errors"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"
	"time"
)

// fakeBrowser simulates a user completing the browser flow: instead of
// launching a real browser, it parses the authorize URL, extracts
// redirect_uri and state, and issues the HTTP request a real browser would
// send after the user logs in and consents — optionally mutating the state
// or forcing an oauth error to exercise the failure paths.
func fakeBrowser(t *testing.T, mutateState func(string) string, oauthError string) func(string) error {
	t.Helper()
	return func(rawAuthURL string) error {
		u, err := url.Parse(rawAuthURL)
		if err != nil {
			return err
		}
		q := u.Query()
		redirectURI := q.Get("redirect_uri")
		state := q.Get("state")
		if mutateState != nil {
			state = mutateState(state)
		}

		cbURL, err := url.Parse(redirectURI)
		if err != nil {
			return err
		}
		cbQuery := url.Values{}
		if oauthError != "" {
			cbQuery.Set("error", oauthError)
		} else {
			cbQuery.Set("code", "fake-auth-code")
		}
		cbQuery.Set("state", state)
		cbURL.RawQuery = cbQuery.Encode()

		go http.Get(cbURL.String())
		return nil
	}
}

func newTokenTestServer(t *testing.T) *httptest.Server {
	t.Helper()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Write([]byte(`{
			"access_token": "at-value",
			"refresh_token": "rt-value",
			"expires_in": 3600,
			"token_type": "Bearer",
			"scope": "account:leagues",
			"username": "SomeAccount",
			"sub": "uuid-1"
		}`))
	}))
	t.Cleanup(srv.Close)
	return srv
}

func TestLogin_HappyPath(t *testing.T) {
	tokenSrv := newTokenTestServer(t)
	client := NewClient(nil, WithTokenURL(tokenSrv.URL))

	tok, err := Login(context.Background(), client, LoginOptions{
		OpenBrowser: fakeBrowser(t, nil, ""),
	})
	if err != nil {
		t.Fatalf("Login: %v", err)
	}
	if tok.AccessToken != "at-value" || tok.Username != "SomeAccount" {
		t.Errorf("Login result = %+v, missing expected fields", tok)
	}
}

func TestLogin_StateMismatchIsRejected(t *testing.T) {
	tokenSrv := newTokenTestServer(t)
	client := NewClient(nil, WithTokenURL(tokenSrv.URL))

	_, err := Login(context.Background(), client, LoginOptions{
		OpenBrowser: fakeBrowser(t, func(string) string { return "wrong-state" }, ""),
	})
	if err == nil {
		t.Fatal("Login with mismatched state: want error, got nil")
	}
}

func TestLogin_OAuthErrorFromAuthServerIsSurfaced(t *testing.T) {
	tokenSrv := newTokenTestServer(t)
	client := NewClient(nil, WithTokenURL(tokenSrv.URL))

	_, err := Login(context.Background(), client, LoginOptions{
		OpenBrowser: fakeBrowser(t, nil, "access_denied"),
	})
	if err == nil {
		t.Fatal("Login with an access_denied callback: want error, got nil")
	}
}

func TestLogin_OpenBrowserErrorIsSurfaced(t *testing.T) {
	tokenSrv := newTokenTestServer(t)
	client := NewClient(nil, WithTokenURL(tokenSrv.URL))

	_, err := Login(context.Background(), client, LoginOptions{
		OpenBrowser: func(string) error { return errors.New("failed to open browser") },
	})
	if err == nil {
		t.Fatal("Login with a failing OpenBrowser: want error, got nil")
	}
}

// TestLogin_TimesOutIfNoCallbackArrives proves Login gives up (rather than
// hanging forever) when the caller's context deadline is reached before any
// callback arrives — using a context deadline well under the production
// loginTimeout so the test itself stays fast.
func TestLogin_TimesOutIfNoCallbackArrives(t *testing.T) {
	tokenSrv := newTokenTestServer(t)
	client := NewClient(nil, WithTokenURL(tokenSrv.URL))

	ctx, cancel := context.WithTimeout(context.Background(), 50*time.Millisecond)
	defer cancel()

	_, err := Login(ctx, client, LoginOptions{
		OpenBrowser: func(string) error { return nil }, // never actually calls back
	})
	if err == nil {
		t.Fatal("Login with no callback and a short deadline: want error, got nil")
	}
}
