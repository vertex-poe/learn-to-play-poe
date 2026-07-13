package poe

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"testing"
)

func TestClient_AuthorizeURL_NeverIncludesClientSecret(t *testing.T) {
	c := NewClient(nil)
	authURL := c.AuthorizeURL("http://127.0.0.1:12345/auth/path-of-exile", "state-value", "challenge-value")

	u, err := url.Parse(authURL)
	if err != nil {
		t.Fatalf("parse authorize URL: %v", err)
	}
	q := u.Query()

	if q.Has("client_secret") {
		t.Error("AuthorizeURL included client_secret — must never be sent by a public client")
	}
	if got := q.Get("client_id"); got != ClientID {
		t.Errorf("client_id = %q, want %q", got, ClientID)
	}
	if got := q.Get("code_challenge_method"); got != "S256" {
		t.Errorf("code_challenge_method = %q, want S256", got)
	}
	if got := q.Get("response_type"); got != "code" {
		t.Errorf("response_type = %q, want code", got)
	}
	if got := q.Get("redirect_uri"); got != "http://127.0.0.1:12345/auth/path-of-exile" {
		t.Errorf("redirect_uri = %q, want the exact loopback URI passed in", got)
	}
	if got := q.Get("scope"); got != "account:leagues account:stashes account:characters" {
		t.Errorf("scope = %q, want the space-separated Scopes list", got)
	}
}

// tokenEndpointRecorder is a fake token endpoint that records the form
// params of the last request and replies with a canned tokenResponse (or a
// canned error status), letting tests assert on exactly what ExchangeCode
// and Refresh send.
type tokenEndpointRecorder struct {
	lastForm   url.Values
	statusCode int
	body       string
}

func (r *tokenEndpointRecorder) handler() http.HandlerFunc {
	return func(w http.ResponseWriter, req *http.Request) {
		body, _ := io.ReadAll(req.Body)
		r.lastForm, _ = url.ParseQuery(string(body))
		if r.statusCode != 0 {
			w.WriteHeader(r.statusCode)
		}
		w.Write([]byte(r.body))
	}
}

func TestClient_ExchangeCode_SendsExpectedParamsAndParsesResponse(t *testing.T) {
	rec := &tokenEndpointRecorder{body: `{
		"access_token": "at-value",
		"refresh_token": "rt-value",
		"expires_in": 3600,
		"token_type": "Bearer",
		"scope": "account:leagues",
		"username": "SomeAccount",
		"sub": "uuid-1"
	}`}
	srv := httptest.NewServer(rec.handler())
	defer srv.Close()

	c := NewClient(nil, WithTokenURL(srv.URL))
	tok, err := c.ExchangeCode(context.Background(), "http://127.0.0.1:9999/auth/path-of-exile", "the-code", "the-verifier")
	if err != nil {
		t.Fatalf("ExchangeCode: %v", err)
	}

	if tok.AccessToken != "at-value" || tok.RefreshToken != "rt-value" || tok.Username != "SomeAccount" || tok.Sub != "uuid-1" {
		t.Errorf("ExchangeCode result = %+v, missing expected fields", tok)
	}
	if tok.AccessExpiration <= tok.Birthday {
		t.Errorf("AccessExpiration (%d) not after Birthday (%d)", tok.AccessExpiration, tok.Birthday)
	}

	if got := rec.lastForm.Get("grant_type"); got != "authorization_code" {
		t.Errorf("grant_type = %q, want authorization_code", got)
	}
	if got := rec.lastForm.Get("code"); got != "the-code" {
		t.Errorf("code = %q, want the-code", got)
	}
	if got := rec.lastForm.Get("code_verifier"); got != "the-verifier" {
		t.Errorf("code_verifier = %q, want the-verifier", got)
	}
	if rec.lastForm.Has("client_secret") {
		t.Error("token request included client_secret — must never be sent by a public client")
	}
}

// TestClient_Refresh_NeverSendsClientSecret proves the request omits
// client_secret entirely rather than sending it empty, per poe-apis.md
// §3.3's documented server-side rejection of client_secret="".
func TestClient_Refresh_NeverSendsClientSecret(t *testing.T) {
	rec := &tokenEndpointRecorder{body: `{"access_token":"at2","refresh_token":"rt2","expires_in":3600}`}
	srv := httptest.NewServer(rec.handler())
	defer srv.Close()

	c := NewClient(nil, WithTokenURL(srv.URL))
	tok, err := c.Refresh(context.Background(), "old-refresh-token")
	if err != nil {
		t.Fatalf("Refresh: %v", err)
	}
	if tok.AccessToken != "at2" {
		t.Errorf("AccessToken = %q, want at2", tok.AccessToken)
	}

	if got := rec.lastForm.Get("grant_type"); got != "refresh_token" {
		t.Errorf("grant_type = %q, want refresh_token", got)
	}
	if got := rec.lastForm.Get("refresh_token"); got != "old-refresh-token" {
		t.Errorf("refresh_token = %q, want old-refresh-token", got)
	}
	if rec.lastForm.Has("client_secret") {
		t.Error("refresh request included client_secret — must be omitted entirely, not sent empty")
	}
}

func TestClient_PostToken_NonOKStatusIsError(t *testing.T) {
	rec := &tokenEndpointRecorder{statusCode: http.StatusBadRequest, body: `{"error":"invalid_grant"}`}
	srv := httptest.NewServer(rec.handler())
	defer srv.Close()

	c := NewClient(nil, WithTokenURL(srv.URL))
	_, err := c.ExchangeCode(context.Background(), "http://127.0.0.1:9999/cb", "bad-code", "verifier")
	if err == nil {
		t.Fatal("ExchangeCode with a 400 response: want error, got nil")
	}
	if !strings.Contains(err.Error(), "400") {
		t.Errorf("error = %v, want it to mention the 400 status", err)
	}
}

func TestClient_PostToken_MissingAccessTokenIsError(t *testing.T) {
	rec := &tokenEndpointRecorder{body: `{"token_type":"Bearer"}`}
	srv := httptest.NewServer(rec.handler())
	defer srv.Close()

	c := NewClient(nil, WithTokenURL(srv.URL))
	_, err := c.Refresh(context.Background(), "rt")
	if err == nil {
		t.Fatal("Refresh with a response missing access_token: want error, got nil")
	}
}

// TestClient_FetchProfile_SendsBearerAndParsesResponse proves FetchProfile
// authenticates with the given access token and decodes locale/twitch, and
// that the caller gets back the response headers regardless (needed for
// rate-limit tracking — see internal/reqqueue).
func TestClient_FetchProfile_SendsBearerAndParsesResponse(t *testing.T) {
	var gotAuth string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotAuth = req.Header.Get("Authorization")
		w.Header().Set("X-Rate-Limit-Policy", "profile-policy")
		w.Write([]byte(`{
			"uuid": "uuid-1",
			"name": "SomeAccount",
			"locale": "en_US",
			"twitch": {"name": "someaccount_tv"}
		}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithProfileURL(srv.URL))
	profile, headers, err := c.FetchProfile(context.Background(), "the-access-token")
	if err != nil {
		t.Fatalf("FetchProfile: %v", err)
	}

	if gotAuth != "Bearer the-access-token" {
		t.Errorf("Authorization header = %q, want %q", gotAuth, "Bearer the-access-token")
	}
	if profile.UUID != "uuid-1" || profile.Name != "SomeAccount" || profile.Locale != "en_US" {
		t.Errorf("profile = %+v, missing expected fields", profile)
	}
	if profile.Twitch == nil || profile.Twitch.Name != "someaccount_tv" {
		t.Errorf("profile.Twitch = %+v, want {someaccount_tv}", profile.Twitch)
	}
	if headers.Get("X-Rate-Limit-Policy") != "profile-policy" {
		t.Errorf("response headers not returned to caller (got %q)", headers.Get("X-Rate-Limit-Policy"))
	}
}

// TestClient_FetchProfile_NoTwitchLink_NilTwitch proves an account with no
// Twitch link decodes to a nil Twitch rather than a zero-value struct, so
// callers can tell "not linked" apart from "linked with an empty name".
func TestClient_FetchProfile_NoTwitchLink_NilTwitch(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"uuid": "uuid-1", "name": "SomeAccount", "locale": "en_US"}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithProfileURL(srv.URL))
	profile, _, err := c.FetchProfile(context.Background(), "token")
	if err != nil {
		t.Fatalf("FetchProfile: %v", err)
	}
	if profile.Twitch != nil {
		t.Errorf("Twitch = %+v, want nil for an unlinked account", profile.Twitch)
	}
}

// TestClient_FetchProfile_NonOKStatusIsError proves a non-200 response
// (e.g. an expired access token) is reported as an error rather than
// silently returning a zero-value Profile.
func TestClient_FetchProfile_NonOKStatusIsError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		w.Write([]byte(`{"error":"invalid_token"}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithProfileURL(srv.URL))
	_, _, err := c.FetchProfile(context.Background(), "expired-token")
	if err == nil {
		t.Fatal("FetchProfile with a 401 response: want error, got nil")
	}
	if !strings.Contains(err.Error(), "401") {
		t.Errorf("error = %v, want it to mention the 401 status", err)
	}
}
