package poe

import "time"

const (
	// refreshTokenLifetime is a client-side assumption: the token server
	// does not return a refresh-token expiry, so this is hardcoded from
	// observed behavior. See poe-apis.md's Token Data Model section.
	refreshTokenLifetime = 7 * 24 * time.Hour

	// refreshEarlyWindow is how long before AccessExpiration a refresh is
	// scheduled, so requests never race the expiry.
	refreshEarlyWindow = 5 * time.Minute
)

// Token is one OAuth token set plus the derived timing fields needed to
// manage its lifecycle, per poe-apis.md's "Token Data Model". It is JSON
// marshaled as-is for storage under internal/creds's poeOAuthToken key —
// the on-disk/credential-store shape is just this struct.
type Token struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	TokenType    string `json:"token_type"`
	Scope        string `json:"scope"`
	Username     string `json:"username"`
	Sub          string `json:"sub"`

	// Birthday, AccessExpiration, and RefreshExpiration are computed at
	// token-receipt time (unix seconds), not returned by the server.
	Birthday          int64 `json:"birthday"`
	AccessExpiration  int64 `json:"access_expiration"`
	RefreshExpiration int64 `json:"refresh_expiration"`
}

// tokenResponse is the token endpoint's raw JSON shape.
type tokenResponse struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ExpiresIn    int64  `json:"expires_in"`
	TokenType    string `json:"token_type"`
	Scope        string `json:"scope"`
	Username     string `json:"username"`
	Sub          string `json:"sub"`
}

// newToken builds a Token from a raw server response, computing the derived
// timing fields relative to now.
func newToken(resp tokenResponse, now time.Time) Token {
	birthday := now.Unix()
	return Token{
		AccessToken:       resp.AccessToken,
		RefreshToken:      resp.RefreshToken,
		TokenType:         resp.TokenType,
		Scope:             resp.Scope,
		Username:          resp.Username,
		Sub:               resp.Sub,
		Birthday:          birthday,
		AccessExpiration:  birthday + resp.ExpiresIn,
		RefreshExpiration: birthday + int64(refreshTokenLifetime.Seconds()),
	}
}

// NeedsRefresh reports whether the access token is already within
// refreshEarlyWindow of expiring (or past it) as of now.
func (t Token) NeedsRefresh(now time.Time) bool {
	return now.Unix() >= t.AccessExpiration-int64(refreshEarlyWindow.Seconds())
}

// PastRefreshWindow reports whether the assumed refresh-token lifetime has
// elapsed — past this point a refresh attempt is expected to fail and the
// caller must fall back to full interactive re-authorization.
func (t Token) PastRefreshWindow(now time.Time) bool {
	return now.Unix() > t.RefreshExpiration
}

// RefreshAt is the wall-clock time a refresh should be scheduled for:
// refreshEarlyWindow before the access token expires.
func (t Token) RefreshAt() time.Time {
	return time.Unix(t.AccessExpiration-int64(refreshEarlyWindow.Seconds()), 0)
}
