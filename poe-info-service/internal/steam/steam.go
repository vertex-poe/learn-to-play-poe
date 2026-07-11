// Package steam fetches a Steam user's "playing now" status from two
// sources and combines them into one snapshot per steamid64:
//
//   - The official Steam Web API (ISteamUser/GetPlayerSummaries), which
//     needs a Web API key and reports only which game a user is in
//     (personaname/gameid/gameextrainfo).
//   - An unofficial scrape of https://steamcommunity.com/miniprofile/<id3>,
//     which needs no credential and exposes the richer "rich presence" text
//     the Steam client itself shows (e.g. a game's league/character/level),
//     by parsing the page's `span.rich_presence` element.
//
// Behavior contract: a missing/invalid Web API key is not an error. The
// official fields (PersonaName, GameName, GameAppID, InGame) simply stay
// empty/false, while the rich-presence scrape still runs and populates
// RichPresence independently — the two sources degrade independently, never
// together. See poe-info-service's CONTRIBUTING.md "Steam presence" section
// for how a client supplies the key (via credentials.store).
//
// This package only fetches and parses one snapshot; it has no knowledge of
// caching, pub/sub, or polling cadence — that orchestration lives in
// internal/server (see server.watchSteamPresence), keeping this package a
// small, httptest-friendly unit.
package steam

import (
	"context"
	"net/http"
	"time"
)

const (
	defaultOfficialBaseURL    = "https://api.steampowered.com"
	defaultMiniprofileBaseURL = "https://steamcommunity.com"

	// throttleInterval is the minimum spacing enforced between any two
	// outbound requests to Steam, shared across both the official API and
	// the miniprofile scrape — slightly more conservative than the 0.2s the
	// reference implementation (github.com/JustTemmie/steam-presence) uses,
	// since Steam has no documented rate limit and is known to silently
	// block clients that hit it too aggressively.
	throttleInterval = 250 * time.Millisecond

	requestTimeout = 10 * time.Second
)

// Presence is one steamid64's fetched snapshot. GameName/GameAppID/InGame
// come from the official API and are zero-valued whenever no API key was
// supplied; RichPresence comes from the miniprofile scrape and is zero-
// valued whenever the page has no rich_presence span (not playing, no rich
// presence set for the current game, or the mismatch guard suppressed it).
type Presence struct {
	SteamID64    string
	PersonaName  string
	GameName     string
	GameAppID    string
	InGame       bool
	RichPresence string
}

// Client fetches Steam data over HTTP, throttling every outbound request
// (official + scrape alike) through one shared limiter.
type Client struct {
	http               *http.Client
	throttle           *minIntervalLimiter
	officialBaseURL    string
	miniprofileBaseURL string
}

// Option configures a Client. WithOfficialBaseURL and WithMiniprofileBaseURL
// exist so tests can point a Client at an httptest.Server instead of the
// real Steam hosts.
type Option func(*Client)

func WithOfficialBaseURL(url string) Option {
	return func(c *Client) { c.officialBaseURL = url }
}

func WithMiniprofileBaseURL(url string) Option {
	return func(c *Client) { c.miniprofileBaseURL = url }
}

// NewClient returns a Client ready to fetch. httpClient may be nil, in which
// case a Client with requestTimeout is used.
func NewClient(httpClient *http.Client, opts ...Option) *Client {
	if httpClient == nil {
		httpClient = &http.Client{Timeout: requestTimeout}
	}
	c := &Client{
		http:               httpClient,
		throttle:           newMinIntervalLimiter(throttleInterval),
		officialBaseURL:    defaultOfficialBaseURL,
		miniprofileBaseURL: defaultMiniprofileBaseURL,
	}
	for _, opt := range opts {
		opt(c)
	}
	return c
}

// FetchPresence combines one steamid64's already-fetched official-API result
// (see FetchOfficial, called once per poll cycle for the whole tracked
// list) with a fresh rich-presence scrape into a single Presence. The two
// sources degrade independently: if official is nil or has no entry for
// steamID64 (no API key configured, or Steam didn't return this id), the
// official fields stay zero-valued and richErr, if any, is still surfaced
// via the returned error — a scrape failure never suppresses whatever
// official data is available, and vice versa.
func (c *Client) FetchPresence(ctx context.Context, steamID64 string, official map[string]OfficialResult) (Presence, error) {
	p := Presence{SteamID64: steamID64}

	var knownGameName string
	if off, ok := official[steamID64]; ok {
		p.PersonaName = off.PersonaName
		p.GameName = off.GameName
		p.GameAppID = off.GameAppID
		p.InGame = off.InGame
		knownGameName = off.GameName
	}

	rich, err := c.FetchRichPresence(ctx, steamID64, knownGameName)
	if err != nil {
		return p, err
	}
	p.RichPresence = rich
	return p, nil
}
