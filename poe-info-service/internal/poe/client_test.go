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

// TestClient_UserAgent_DefaultIsCompliantWithoutWithVersion proves a Client
// built with no WithVersion option still sends a PoE-API-compliant
// User-Agent (see UserAgentApp's doc comment: PoE's Cloudflare edge 403s any
// request without one, so this can't be allowed to silently fall back to
// Go's default "Go-http-client/1.1").
func TestClient_UserAgent_DefaultIsCompliantWithoutWithVersion(t *testing.T) {
	var gotUA string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotUA = req.Header.Get("User-Agent")
		w.Write([]byte(`[]`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	if _, _, err := c.FetchLeagues(context.Background(), LeaguesParams{}); err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}
	wantPrefix := "OAuth " + UserAgentApp + "/"
	if !strings.HasPrefix(gotUA, wantPrefix) || !strings.Contains(gotUA, UserAgentContact) {
		t.Errorf("User-Agent = %q, want prefix %q and contact %q", gotUA, wantPrefix, UserAgentContact)
	}
}

// TestClient_UserAgent_WithVersion_UsesGivenVersion proves WithVersion's
// value ends up in the header, per the exact format GGG's developer docs
// require (see docs/architecture.md's "User-Agent requirement" section):
// "OAuth {clientId}/{version} (contact: {contact})".
func TestClient_UserAgent_WithVersion_UsesGivenVersion(t *testing.T) {
	var gotUA string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotUA = req.Header.Get("User-Agent")
		w.Write([]byte(`[]`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL), WithVersion("1.2.3"))
	if _, _, err := c.FetchLeagues(context.Background(), LeaguesParams{}); err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}
	want := "OAuth " + UserAgentApp + "/1.2.3 (contact: " + UserAgentContact + ")"
	if gotUA != want {
		t.Errorf("User-Agent = %q, want %q", gotUA, want)
	}
}

// TestClient_UserAgent_SentOnTokenAndProfileAndLeagueRequests proves every
// outbound request type carries the header, not just FetchLeagues — a
// regression guard for the bug that motivated this (PoE's Cloudflare edge
// silently 403ing every OAuth API call, including the token endpoint and
// Bearer-authenticated ones, until this was added everywhere).
func TestClient_UserAgent_SentOnTokenAndProfileAndLeagueRequests(t *testing.T) {
	var gotTokenUA, gotProfileUA, gotLeagueUA, gotAccountLeaguesUA string
	tokenSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotTokenUA = req.Header.Get("User-Agent")
		w.Write([]byte(`{"access_token":"tok","token_type":"Bearer","expires_in":3600,"scope":"","username":"u","sub":"s"}`))
	}))
	defer tokenSrv.Close()
	profileSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotProfileUA = req.Header.Get("User-Agent")
		w.Write([]byte(`{"uuid":"u","name":"n"}`))
	}))
	defer profileSrv.Close()
	leagueSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotLeagueUA = req.Header.Get("User-Agent")
		w.Write([]byte(`{"league":{"id":"Standard","realm":"pc"}}`))
	}))
	defer leagueSrv.Close()
	accountLeaguesSrv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotAccountLeaguesUA = req.Header.Get("User-Agent")
		w.Write([]byte(`{"leagues":[]}`))
	}))
	defer accountLeaguesSrv.Close()

	c := NewClient(nil, WithTokenURL(tokenSrv.URL), WithProfileURL(profileSrv.URL), WithLeagueURL(leagueSrv.URL), WithAccountLeaguesURL(accountLeaguesSrv.URL))
	if _, err := c.ExchangeCode(context.Background(), "http://127.0.0.1/cb", "code", "verifier"); err != nil {
		t.Fatalf("ExchangeCode: %v", err)
	}
	if _, _, err := c.FetchProfile(context.Background(), "access-token"); err != nil {
		t.Fatalf("FetchProfile: %v", err)
	}
	if _, _, err := c.FetchLeague(context.Background(), "access-token", "Standard", ""); err != nil {
		t.Fatalf("FetchLeague: %v", err)
	}
	if _, _, err := c.FetchAccountLeagues(context.Background(), "access-token", ""); err != nil {
		t.Fatalf("FetchAccountLeagues: %v", err)
	}

	wantPrefix := "OAuth " + UserAgentApp + "/"
	for name, got := range map[string]string{"token": gotTokenUA, "profile": gotProfileUA, "league": gotLeagueUA, "accountLeagues": gotAccountLeaguesUA} {
		if !strings.HasPrefix(got, wantPrefix) {
			t.Errorf("%s request User-Agent = %q, want prefix %q", name, got, wantPrefix)
		}
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
		w.Write([]byte(`[]`))
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

// TestClient_FetchLeagues_ResponseIsBareArray_NotWrappedObject is a
// dedicated regression test for the actual bug that broke poe.leagues.list
// end to end: the live /leagues endpoint's response body is a bare JSON
// array (`[{...}, {...}]`), not `{"leagues": [...]}` — confirmed directly
// against api.pathofexile.com on 2026-07-14. FetchLeagues used to decode
// into a `{"leagues": [...]}`-shaped wrapper struct, which silently failed
// with "cannot unmarshal array into Go value of type poe.leaguesResponse"
// against every real response, even though every test's httptest fake used
// the same (wrong) wrapped shape and so never caught it.
func TestClient_FetchLeagues_ResponseIsBareArray_NotWrappedObject(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.Write([]byte(`[{"id":"Standard","realm":"pc"},{"id":"Hardcore","realm":"pc"}]`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithLeaguesURL(srv.URL))
	leagues, _, err := c.FetchLeagues(context.Background(), LeaguesParams{})
	if err != nil {
		t.Fatalf("FetchLeagues: %v", err)
	}
	if len(leagues) != 2 || leagues[0].ID != "Standard" || leagues[1].ID != "Hardcore" {
		t.Errorf("leagues = %+v, want [Standard Hardcore] decoded from the bare array", leagues)
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
		w.Write([]byte(`[]`))
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
		w.Write([]byte(`[
			{"id":"SSF Ancestors","realm":"pc","url":"https://example.com","startAt":"2024-01-01T00:00:00Z","endAt":null,"description":"desc","rules":[{"id":"Hardcore"},{"id":"NoParties"}],"event":false,"delveEvent":false},
			{"id":"Standard","realm":"pc","event":false,"delveEvent":false}
		]`))
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

// TestClient_FetchAccountLeagues_SendsBearerAndParsesWrappedResponse proves
// FetchAccountLeagues authenticates with the given access token — unlike
// FetchLeagues, GET /account/leagues requires Bearer auth — and decodes the
// {"leagues": [...]} wrapper, unlike FetchLeagues's bare array.
func TestClient_FetchAccountLeagues_SendsBearerAndParsesWrappedResponse(t *testing.T) {
	var gotAuth string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotAuth = req.Header.Get("Authorization")
		w.Write([]byte(`{"leagues":[{"id":"Standard","realm":"pc"},{"id":"My Private League","realm":"pc"}]}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithAccountLeaguesURL(srv.URL))
	leagues, _, err := c.FetchAccountLeagues(context.Background(), "the-access-token", "")
	if err != nil {
		t.Fatalf("FetchAccountLeagues: %v", err)
	}
	if gotAuth != "Bearer the-access-token" {
		t.Errorf("Authorization header = %q, want %q", gotAuth, "Bearer the-access-token")
	}
	if len(leagues) != 2 || leagues[0].ID != "Standard" || leagues[1].ID != "My Private League" {
		t.Errorf("leagues = %+v, want [Standard, My Private League] decoded from the wrapped response", leagues)
	}
}

// TestClient_FetchAccountLeagues_PCOrEmptyRealm_NoPathSegment proves both ""
// and "pc" omit the realm path segment entirely — the endpoint's own
// documented realm segment is xbox/sony only, with PC assumed when omitted.
func TestClient_FetchAccountLeagues_PCOrEmptyRealm_NoPathSegment(t *testing.T) {
	for _, realm := range []string{"", "pc"} {
		var gotPath string
		srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
			gotPath = req.URL.Path
			w.Write([]byte(`{"leagues":[]}`))
		}))

		c := NewClient(nil, WithAccountLeaguesURL(srv.URL))
		if _, _, err := c.FetchAccountLeagues(context.Background(), "token", realm); err != nil {
			t.Fatalf("FetchAccountLeagues(realm=%q): %v", realm, err)
		}
		if gotPath != "" && gotPath != "/" {
			t.Errorf("realm=%q: path = %q, want no realm segment appended", realm, gotPath)
		}
		srv.Close()
	}
}

// TestClient_FetchAccountLeagues_NonPCRealm_AppendsPathSegment proves a
// non-PC realm (xbox/sony) is appended as a path segment.
func TestClient_FetchAccountLeagues_NonPCRealm_AppendsPathSegment(t *testing.T) {
	var gotPath string
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		gotPath = req.URL.Path
		w.Write([]byte(`{"leagues":[]}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithAccountLeaguesURL(srv.URL+"/account/leagues"))
	if _, _, err := c.FetchAccountLeagues(context.Background(), "token", "xbox"); err != nil {
		t.Fatalf("FetchAccountLeagues: %v", err)
	}
	if gotPath != "/account/leagues/xbox" {
		t.Errorf("path = %q, want /account/leagues/xbox", gotPath)
	}
}

// TestClient_FetchAccountLeagues_NonOKStatusIsError proves a non-200
// response is reported as an error rather than silently returning an empty
// list.
func TestClient_FetchAccountLeagues_NonOKStatusIsError(t *testing.T) {
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, req *http.Request) {
		w.WriteHeader(http.StatusUnauthorized)
		w.Write([]byte(`{"error":"invalid_token"}`))
	}))
	defer srv.Close()

	c := NewClient(nil, WithAccountLeaguesURL(srv.URL))
	_, _, err := c.FetchAccountLeagues(context.Background(), "expired-token", "")
	if err == nil {
		t.Fatal("FetchAccountLeagues with a 401 response: want error, got nil")
	}
	if !strings.Contains(err.Error(), "401") {
		t.Errorf("error = %v, want it to mention the 401 status", err)
	}
}
