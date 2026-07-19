package poe

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
	"time"
)

const requestTimeout = 15 * time.Second

const (
	// UserAgentApp/UserAgentContact identify this service to PoE's API, per
	// the format GGG's own developer docs require:
	// https://www.pathofexile.com/developer/docs#guidelines ("Developer
	// Guidelines" > "User Agent" — see docs/architecture.md's "User-Agent
	// requirement" section): "Any application that interacts with our API
	// must set an identifiable User Agent header", formatted `OAuth
	// {clientId}/{version} (contact: {contact}) ...`. This is not optional
	// politeness — PoE's edge (Cloudflare) returns a 403 for Go's default
	// User-Agent ("Go-http-client/1.1") on every endpoint, including the
	// public /leagues one, so a request without this header never succeeds
	// at all.
	UserAgentApp     = "poe-info-service"
	UserAgentContact = "MovingCairn+poe-info-service@gmail.com"
)

// Client talks to PoE's OAuth authorization server. It is a public client:
// no client_secret is ever configured or sent, on the authorization-code
// exchange or the refresh — per poe-apis.md §3.3, an empty client_secret is
// actively rejected by the token server, so the parameter is simply never
// included rather than sent empty.
type Client struct {
	http              *http.Client
	authURL           string
	tokenURL          string
	profileURL        string
	leaguesURL        string
	leagueURL         string
	accountLeaguesURL string
	userAgent         string
}

// Option configures a Client. WithAuthURL/WithTokenURL/WithProfileURL/
// WithLeaguesURL/WithLeagueURL/WithAccountLeaguesURL exist so tests can point
// a Client at an httptest.Server instead of the real PoE hosts.
type Option func(*Client)

func WithAuthURL(u string) Option           { return func(c *Client) { c.authURL = u } }
func WithTokenURL(u string) Option          { return func(c *Client) { c.tokenURL = u } }
func WithProfileURL(u string) Option        { return func(c *Client) { c.profileURL = u } }
func WithLeaguesURL(u string) Option        { return func(c *Client) { c.leaguesURL = u } }
func WithLeagueURL(u string) Option         { return func(c *Client) { c.leagueURL = u } }
func WithAccountLeaguesURL(u string) Option { return func(c *Client) { c.accountLeaguesURL = u } }

// WithVersion sets the version reported in the User-Agent header (see
// UserAgentApp's doc comment) — callers pass this service's own build
// version (proto.Version) so it doesn't need duplicating inside this
// package, which otherwise depends on nothing but the standard library.
func WithVersion(v string) Option {
	return func(c *Client) {
		c.userAgent = fmt.Sprintf("OAuth %s/%s (contact: %s)", UserAgentApp, v, UserAgentContact)
	}
}

// NewClient returns a Client ready to authenticate. httpClient may be nil,
// in which case a Client with requestTimeout is used. userAgent defaults to
// an unversioned string so every request is still PoE-API-compliant even if
// a caller forgets WithVersion — see UserAgentApp's doc comment for why that
// matters.
func NewClient(httpClient *http.Client, opts ...Option) *Client {
	if httpClient == nil {
		httpClient = &http.Client{Timeout: requestTimeout}
	}
	c := &Client{
		http:              httpClient,
		authURL:           AuthURL,
		tokenURL:          TokenURL,
		profileURL:        ProfileURL,
		leaguesURL:        LeaguesURL,
		leagueURL:         LeagueURL,
		accountLeaguesURL: AccountLeaguesURL,
		userAgent:         fmt.Sprintf("OAuth %s/dev (contact: %s)", UserAgentApp, UserAgentContact),
	}
	for _, opt := range opts {
		opt(c)
	}
	return c
}

// AuthorizeURL builds the authorization endpoint URL a user's browser must
// visit, per poe-apis.md §3.3 step 3.
func (c *Client) AuthorizeURL(redirectURI, state, codeChallenge string) string {
	v := url.Values{}
	v.Set("client_id", ClientID)
	v.Set("redirect_uri", redirectURI)
	v.Set("response_type", "code")
	v.Set("scope", strings.Join(Scopes, " "))
	v.Set("code_challenge", codeChallenge)
	v.Set("code_challenge_method", "S256")
	v.Set("state", state)
	return c.authURL + "?" + v.Encode()
}

// ExchangeCode exchanges an authorization code for a token set, per
// poe-apis.md §3.3 step 8. redirectURI must exactly match the one used in
// the authorize request.
func (c *Client) ExchangeCode(ctx context.Context, redirectURI, code, codeVerifier string) (Token, error) {
	form := url.Values{}
	form.Set("grant_type", "authorization_code")
	form.Set("client_id", ClientID)
	form.Set("code", code)
	form.Set("redirect_uri", redirectURI)
	form.Set("code_verifier", codeVerifier)

	resp, err := c.postToken(ctx, form)
	if err != nil {
		return Token{}, err
	}
	return newToken(resp, time.Now()), nil
}

// Refresh exchanges a refresh token for a new access token, per
// poe-apis.md §3.3's "Token Refresh" section. No client_secret parameter is
// ever included — see the Client doc comment.
func (c *Client) Refresh(ctx context.Context, refreshToken string) (Token, error) {
	form := url.Values{}
	form.Set("grant_type", "refresh_token")
	form.Set("client_id", ClientID)
	form.Set("refresh_token", refreshToken)

	resp, err := c.postToken(ctx, form)
	if err != nil {
		return Token{}, err
	}
	return newToken(resp, time.Now()), nil
}

func (c *Client) postToken(ctx context.Context, form url.Values) (tokenResponse, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodPost, c.tokenURL, strings.NewReader(form.Encode()))
	if err != nil {
		return tokenResponse{}, fmt.Errorf("build token request: %w", err)
	}
	req.Header.Set("Content-Type", "application/x-www-form-urlencoded")
	req.Header.Set("User-Agent", c.userAgent)

	resp, err := c.http.Do(req)
	if err != nil {
		return tokenResponse{}, fmt.Errorf("token request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return tokenResponse{}, fmt.Errorf("read token response: %w", err)
	}

	if resp.StatusCode != http.StatusOK {
		return tokenResponse{}, fmt.Errorf("token endpoint returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var tr tokenResponse
	if err := json.Unmarshal(body, &tr); err != nil {
		return tokenResponse{}, fmt.Errorf("decode token response: %w", err)
	}
	if tr.AccessToken == "" {
		return tokenResponse{}, fmt.Errorf("token response missing access_token")
	}
	return tr, nil
}

// ProfileTwitch is the "twitch" object on a Profile — present only when the
// account is Twitch-linked.
type ProfileTwitch struct {
	Name string `json:"name"`
}

// Profile is the OAuth data API's GET /profile response (requires the
// account:profile scope) — see _reference/poe-apis/poe-apis.md's Account
// Profile section.
type Profile struct {
	UUID   string         `json:"uuid"`
	Name   string         `json:"name"`
	Locale string         `json:"locale,omitempty"`
	Twitch *ProfileTwitch `json:"twitch,omitempty"`
}

// FetchProfile calls GET /profile with accessToken as a Bearer credential,
// returning the decoded profile plus the response's raw headers — a caller
// tracking this endpoint's rate-limit state (see internal/reqqueue) needs
// those regardless of whether the call itself succeeded or failed with a
// rate-limit-related status.
func (c *Client) FetchProfile(ctx context.Context, accessToken string) (Profile, http.Header, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, c.profileURL, nil)
	if err != nil {
		return Profile{}, nil, fmt.Errorf("build profile request: %w", err)
	}
	req.Header.Set("Authorization", "Bearer "+accessToken)
	req.Header.Set("User-Agent", c.userAgent)

	resp, err := c.http.Do(req)
	if err != nil {
		return Profile{}, nil, fmt.Errorf("profile request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return Profile{}, resp.Header, fmt.Errorf("read profile response: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		return Profile{}, resp.Header, fmt.Errorf("profile endpoint returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var p Profile
	if err := json.Unmarshal(body, &p); err != nil {
		return Profile{}, resp.Header, fmt.Errorf("decode profile response: %w", err)
	}
	return p, resp.Header, nil
}

// LeagueRule is one modifier rule attached to a league (e.g. "Hardcore",
// "NoParties" for SSF) — see _reference/poe-apis/poe-apis.md §6.1.1's
// "Notable fields" (the OAuth API's /leagues response shares this shape).
type LeagueRule struct {
	ID string `json:"id"`
}

// League is one element of GET /leagues's response — see
// _reference/poe-apis/poe-apis.md §6.2/§6.1.1 and
// internal/server/poe_leagues.go, which caches these in the leagues table.
type League struct {
	// ID is the league name (e.g. "SSF Ancestors") — also used as the
	// `league` parameter by every other OAuth data endpoint.
	ID          string       `json:"id"`
	Realm       string       `json:"realm"`
	URL         string       `json:"url,omitempty"`
	StartAt     string       `json:"startAt,omitempty"`
	EndAt       string       `json:"endAt,omitempty"` // omitted/"" for a permanent league
	Description string       `json:"description,omitempty"`
	Rules       []LeagueRule `json:"rules,omitempty"`
	Event       bool         `json:"event"`
	DelveEvent  bool         `json:"delveEvent"`
}

// LeaguesParams are GET /leagues's optional query parameters, per the PoE
// OAuth API's List Leagues endpoint doc. A zero LeaguesParams matches the
// endpoint's own documented defaults (realm=pc, type=main, limit=50) —
// every field here is only ever sent if non-empty/non-zero.
type LeaguesParams struct {
	Realm  string
	Type   string
	Season string // only meaningful when Type == "season" (PoE1 only)
	Limit  int
	Offset int
}

// FetchLeagues calls GET /leagues, returning the decoded league list plus
// the response's raw headers — same convention as FetchProfile (a caller
// tracking this endpoint's rate-limit state via internal/reqqueue needs
// those regardless of success/failure). Unlike every other OAuth data
// endpoint, /leagues is public and requires no Bearer token (poe-apis.md's
// documented rule 9), so no access token is accepted here.
//
// The response body is a bare JSON array of League, not `{"leagues": [...]}`
// — confirmed directly against the live endpoint (2026-07-14). GGG's current
// developer docs (https://www.pathofexile.com/developer/docs/reference
// #leagues) only describe a *different*, newer `GET /league` (singular,
// `service:leagues`-scoped) endpoint that does wrap its array under a
// "leagues" key; this service still talks to the older `/leagues` (plural)
// endpoint referenced by poe-apis.md §6.2, which is undocumented there now
// but still live, and returns the array directly.
func (c *Client) FetchLeagues(ctx context.Context, params LeaguesParams) ([]League, http.Header, error) {
	v := url.Values{}
	if params.Realm != "" {
		v.Set("realm", params.Realm)
	}
	if params.Type != "" {
		v.Set("type", params.Type)
	}
	if params.Season != "" {
		v.Set("season", params.Season)
	}
	if params.Limit > 0 {
		v.Set("limit", strconv.Itoa(params.Limit))
	}
	if params.Offset > 0 {
		v.Set("offset", strconv.Itoa(params.Offset))
	}

	reqURL := c.leaguesURL
	if enc := v.Encode(); enc != "" {
		reqURL += "?" + enc
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("build leagues request: %w", err)
	}
	req.Header.Set("User-Agent", c.userAgent)

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("leagues request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, resp.Header, fmt.Errorf("read leagues response: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, resp.Header, fmt.Errorf("leagues endpoint returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var leagues []League
	if err := json.Unmarshal(body, &leagues); err != nil {
		return nil, resp.Header, fmt.Errorf("decode leagues response: %w", err)
	}
	return leagues, resp.Header, nil
}

type leagueResponse struct {
	League *League `json:"league"`
}

// FetchLeague calls GET /league/{name} with accessToken as a Bearer
// credential, returning the decoded league (a nil pointer, not an error, if
// PoE reports no league by that name exists — "the league object requested
// or null if it cannot be found" is this endpoint's own documented
// contract) plus the response's raw headers, same convention as
// FetchProfile/FetchLeagues. Unlike FetchLeagues, this endpoint requires
// Bearer auth like every other OAuth data endpoint — /leagues (plural) is
// the one documented public exception, not this single-league lookup.
func (c *Client) FetchLeague(ctx context.Context, accessToken, name, realm string) (*League, http.Header, error) {
	reqURL := c.leagueURL + "/" + url.PathEscape(name)
	if realm != "" {
		v := url.Values{}
		v.Set("realm", realm)
		reqURL += "?" + v.Encode()
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("build league request: %w", err)
	}
	req.Header.Set("Authorization", "Bearer "+accessToken)
	req.Header.Set("User-Agent", c.userAgent)

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("league request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, resp.Header, fmt.Errorf("read league response: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, resp.Header, fmt.Errorf("league endpoint returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var lr leagueResponse
	if err := json.Unmarshal(body, &lr); err != nil {
		return nil, resp.Header, fmt.Errorf("decode league response: %w", err)
	}
	return lr.League, resp.Header, nil
}

type accountLeaguesResponse struct {
	Leagues []League `json:"leagues"`
}

// FetchAccountLeagues calls GET /account/leagues[/<realm>] with accessToken
// as a Bearer credential, returning the leagues visible to that account
// (including private ones) plus the response's raw headers, same convention
// as FetchProfile/FetchLeagues/FetchLeague. Unlike FetchLeagues, this
// endpoint requires Bearer auth, and unlike FetchLeague its response is
// wrapped as {"leagues": [...]} rather than a bare array. realm is only ever
// appended as a path segment when it's a non-PC platform (the endpoint's own
// documented realm segment is xbox/sony only — PC is assumed when the
// segment is omitted), so both "" and "pc" send no segment at all.
func (c *Client) FetchAccountLeagues(ctx context.Context, accessToken, realm string) ([]League, http.Header, error) {
	reqURL := c.accountLeaguesURL
	if realm != "" && realm != "pc" {
		reqURL += "/" + url.PathEscape(realm)
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	if err != nil {
		return nil, nil, fmt.Errorf("build account leagues request: %w", err)
	}
	req.Header.Set("Authorization", "Bearer "+accessToken)
	req.Header.Set("User-Agent", c.userAgent)

	resp, err := c.http.Do(req)
	if err != nil {
		return nil, nil, fmt.Errorf("account leagues request: %w", err)
	}
	defer resp.Body.Close()

	body, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, resp.Header, fmt.Errorf("read account leagues response: %w", err)
	}
	if resp.StatusCode != http.StatusOK {
		return nil, resp.Header, fmt.Errorf("account leagues endpoint returned %d: %s", resp.StatusCode, strings.TrimSpace(string(body)))
	}

	var ar accountLeaguesResponse
	if err := json.Unmarshal(body, &ar); err != nil {
		return nil, resp.Header, fmt.Errorf("decode account leagues response: %w", err)
	}
	return ar.Leagues, resp.Header, nil
}
