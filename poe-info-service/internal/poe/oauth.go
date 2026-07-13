// Package poe implements the OAuth 2.0 Authorization Code + PKCE flow for
// Path of Exile's official Developer API (api.pathofexile.com), per
// poe-info-service/docs/decisions/004-credential-custody.md: this service
// initiates the flow itself, via the system's default browser plus a local
// loopback redirect listener — no WebView-capable client is required, unlike
// POESESSID (see internal/creds and _reference/poe-apis/poe-apis.md §3.3 for
// the full protocol reference this package implements).
//
// This package only performs the authorization dance and models the
// resulting token's lifecycle; it never persists anything itself — the
// caller (internal/server) owns storing the result via internal/creds and
// scheduling refreshes.
package poe

import "time"

const (
	// AuthURL and TokenURL are PoE's OAuth 2.0 authorization server
	// endpoints — fixed, not configurable.
	AuthURL  = "https://www.pathofexile.com/oauth/authorize"
	TokenURL = "https://www.pathofexile.com/oauth/token"

	// ProfileURL is the OAuth data API's account-profile endpoint (requires
	// the account:profile scope) — see _reference/poe-apis/poe-apis.md's
	// Account Profile section.
	ProfileURL = "https://api.pathofexile.com/profile"

	// ClientID is a public, hardcoded identifier for a public (secret-less)
	// OAuth client — not a secret, never paired with a client_secret.
	ClientID = "REPLACE_WITH_REGISTERED_CLIENT_ID"

	// CallbackPath is the fixed path segment of the loopback redirect URI;
	// only the host port varies per login attempt.
	CallbackPath = "/auth/path-of-exile"

	// loginTimeout bounds how long a single login attempt waits for the
	// user to complete the browser flow before giving up and tearing down
	// the loopback listener.
	loginTimeout = 5 * time.Minute
)

// Scopes are the OAuth scopes requested — least privilege needed to read
// leagues, stashes, and characters.
var Scopes = []string{"account:leagues", "account:stashes", "account:characters"}
