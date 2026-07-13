package poe

import (
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strings"
	"time"
)

const requestTimeout = 15 * time.Second

// Client talks to PoE's OAuth authorization server. It is a public client:
// no client_secret is ever configured or sent, on the authorization-code
// exchange or the refresh — per poe-apis.md §3.3, an empty client_secret is
// actively rejected by the token server, so the parameter is simply never
// included rather than sent empty.
type Client struct {
	http       *http.Client
	authURL    string
	tokenURL   string
	profileURL string
}

// Option configures a Client. WithAuthURL/WithTokenURL/WithProfileURL exist
// so tests can point a Client at an httptest.Server instead of the real PoE
// hosts.
type Option func(*Client)

func WithAuthURL(u string) Option    { return func(c *Client) { c.authURL = u } }
func WithTokenURL(u string) Option   { return func(c *Client) { c.tokenURL = u } }
func WithProfileURL(u string) Option { return func(c *Client) { c.profileURL = u } }

// NewClient returns a Client ready to authenticate. httpClient may be nil,
// in which case a Client with requestTimeout is used.
func NewClient(httpClient *http.Client, opts ...Option) *Client {
	if httpClient == nil {
		httpClient = &http.Client{Timeout: requestTimeout}
	}
	c := &Client{
		http:       httpClient,
		authURL:    AuthURL,
		tokenURL:   TokenURL,
		profileURL: ProfileURL,
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
