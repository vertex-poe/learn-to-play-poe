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

// TestClient_FetchLeagues_NoAuthHeaderSent proves /leagues is called without
// any Authorization header — unlike every other OAuth data endpoint, this
// one is public (poe-apis.md's documented rule 9).
func TestClient_FetchLeagues_NoAuthHeaderSent(t *testing.T) {
	var gotAuth string
	var sawAuthHeader bool
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotAuth = req.Header.Get("Authorization")
		_, sawAuthHeader = req.Header["Authorization"]
		w.Write([]byte(`{"leagues":[]}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	if _, _, err := c.FetchLeagues(context.Background(), LeaguesParams{}); err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}
	if sawAuthHeader {
		t.Errorf("Authorization header = %q, want none sent at all", gotAuth)
	}
}

// TestClient_FetchLeagues_ZeroParams_SendsNoQueryString proves a zero
// LeaguesParams omits every query parameter rather than sending them empty,
// letting the endpoint's own documented defaults (realm=pc, type=main,
// limit=50) apply server-side.
func TestClient_FetchLeagues_ZeroParams_SendsNoQueryString(t *testing.T) {
	var gotQuery string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotQuery = req.URL.RawQuery
		w.Write([]byte(`{"leagues":[]}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	if _, _, err := c.FetchLeagues(context.Background(), LeaguesParams{}); err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}
	if gotQuery != "" {
		t.Errorf("query string = %q, want empty for a zero LeaguesParams", gotQuery)
	}
}

// TestClient_FetchLeagues_SendsGivenParamsAndParsesResponse proves every
// LeaguesParams field is sent when set, and the response's league array
// (including nested rules) decodes correctly.
func TestClient_FetchLeagues_SendsGivenParamsAndParsesResponse(t *testing.T) {
	var gotQuery url.Values
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotQuery = req.URL.Query()
		w.Write([]byte(`{"leagues":[
			{"id":"SSF Ancestors","realm":"pc","url":"https://example.com","startAt":"2024-01-01T00:00:00Z","endAt":null,"description":"desc","rules":[{"id":"Hardcore"},{"id":"NoParties"}],"event":false,"delveEvent":false},
			{"id":"Standard","realm":"pc","event":false,"delveEvent":false}
		]}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	leagues, _, err := c.FetchLeagues(context.Background(), LeaguesParams{Realm: "xbox", Type: "season", Season: "Ancestors", Limit: 10, Offset: 5})
	if err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}

	if got := gotQuery.Get("realm"); got != "xbox" {
		t.Errorf("realm = %q, want xbox", got)
	}
	if got := gotQuery.Get("type"); got != "season" {
		t.Errorf("type = %q, want season", got)
	}
	if got := gotQuery.Get("season"); got != "Ancestors" {
		t.Errorf("season = %q, want Ancestors", got)
	}
	if got := gotQuery.Get("limit"); got != "10" {
		t.Errorf("limit = %q, want 10", got)
	}
	if got := gotQuery.Get("offset"); got != "5" {
		t.Errorf("offset = %q, want 5", got)
	}

	if len(leagues) != 2 {
		t.Fatalf("got %d leagues, want 2", len(leagues))
	}
	first := leagues[0]
	if first.ID != "SSF Ancestors" || first.Realm != "pc" || first.Description != "desc" {
		t.Errorf("first league = %+v, missing expected fields", first)
	}
	if len(first.Rules) != 2 || first.Rules[0].ID != "Hardcore" || first.Rules[1].ID != "NoParties" {
		t.Errorf("first league rules = %+v, want [Hardcore NoParties]", first.Rules)
	}
	if leagues[1].ID != "Standard" {
		t.Errorf("second league ID = %q, want Standard", leagues[1].ID)
	}
}

// TestClient_FetchLeagues_NonOKStatusIsError proves a non-200 response is
// reported as an error rather than silently returning an empty list.
func TestClient_FetchLeagues_NonOKStatusIsError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.WriteHeader(http.StatusServiceUnavailable)
		w.Write([]byte(`{"error":"unavailable"}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	_, _, err := c.FetchLeagues(context.Background(), LeaguesParams{})
	if err == nil {
		t.Fatal("FetchLeagues with a 503 response: want error, got nil")
	}
	if !strings.Contains(err.Error(), "503") {
		t.Errorf("error = %v, want it to mention the 503 status", err)
	}
}

// TestClient_FetchLeague_SendsBearerAndPathAndRealm proves FetchLeague
// authenticates with the given access token (unlike FetchLeagues), puts the
// league name in the path (not a query param), and only sends realm when
// non-empty.
func TestClient_FetchLeague_SendsBearerAndPathAndRealm(t *testing.T) {
	var gotAuth, gotPath, gotQuery string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotAuth = req.Header.Get("Authorization")
		gotPath = req.URL.Path
		gotQuery = req.URL.RawQuery
		w.Write([]byte(`{"league":{"id":"SSF Ancestors","realm":"pc"}}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeagueURL(srv.URL+"/league"))
	league, _, err := c.FetchLeague(context.Background(), "the-access-token", "SSF Ancestors", "xbox")
	if err != nil {
		t.Fatalf("FetchLeague: %v", err)
	}

	if gotAuth != "Bearer the-access-token" {
		t.Errorf("Authorization header = %q, want %q", gotAuth, "Bearer the-access-token")
	}
	if gotPath != "/league/SSF Ancestors" {
		t.Errorf("path = %q, want /league/SSF Ancestors", gotPath)
	}
	if gotQuery != "realm=xbox" {
		t.Errorf("query = %q, want realm=xbox", gotQuery)
	}
	if league == nil || league.ID != "SSF Ancestors" {
		t.Errorf("league = %+v, want SSF Ancestors", league)
	}
}

// TestClient_FetchLeague_EmptyRealm_NoQueryString proves an empty realm
// omits the query string entirely rather than sending realm= empty.
func TestClient_FetchLeague_EmptyRealm_NoQueryString(t *testing.T) {
	var gotQuery string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotQuery = req.URL.RawQuery
		w.Write([]byte(`{"league":{"id":"Standard","realm":"pc"}}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeagueURL(srv.URL+"/league"))
	if _, _, err := c.FetchLeague(context.Background(), "token", "Standard", ""); err != nil {
		t.Fatalf("FetchLeague: %v", err)
	}
	if gotQuery != "" {
		t.Errorf("query = %q, want empty with no realm given", gotQuery)
	}
}

// TestClient_FetchLeague_NullLeague_ReturnsNilNotError proves a `{"league":
// null}` response (the endpoint's documented "not found" shape) decodes to
// a nil League with no error, distinct from an actual failure.
func TestClient_FetchLeague_NullLeague_ReturnsNilNotError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`{"league":null}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeagueURL(srv.URL+"/league"))
	league, _, err := c.FetchLeague(context.Background(), "token", "Nonexistent League", "")
	if err != nil {
		t.Fatalf("FetchLeague: %v", err)
	}
	if league != nil {
		t.Errorf("league = %+v, want nil for a null response", league)
	}
}

// TestClient_FetchLeague_NonOKStatusIsError proves a non-200 response is
// reported as an error rather than silently returning a nil League.
func TestClient_FetchLeague_NonOKStatusIsError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		w.Write([]byte(`{"error":"invalid_token"}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeagueURL(srv.URL+"/league"))
	_, _, err := c.FetchLeague(context.Background(), "expired-token", "Standard", "")
	if err == nil {
		t.Fatal("FetchLeague with a 401 response: want error, got nil")
	}
	if !strings.Contains(err.Error(), "401") {
		t.Errorf("error = %v, want it to mention the 401 status", err)
	}
}
