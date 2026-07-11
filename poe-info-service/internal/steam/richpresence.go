package steam

import (
	"context"
	"fmt"
	"net/http"
	"strconv"
	"strings"

	"github.com/PuerkitoBio/goquery"
)

// steamID3Base is the fixed offset between a SteamID64 and the 32-bit
// "SteamID3" the miniprofile endpoint is keyed by: id3 = id64 - base. See
// https://steamcommunity.com/discussions/forum/1/5940851794736009972/ (the
// post credited by the reference implementation for this discovery) — Valve
// documents neither the endpoint nor this relationship.
const steamID3Base = 76561197960265728

// ValidateSteamID64 reports whether steamID64 is a well-formed SteamID64:
// an unsigned 64-bit integer no smaller than steamID3Base (every real
// individual account's id is well above this, per Valve's SteamID
// numbering — see steamID3's doc comment). Exported so config.set's
// steam_ids validation rejects a bad id up front, at the point a client
// configures it, rather than only failing later inside a poll cycle.
func ValidateSteamID64(steamID64 string) (uint64, error) {
	id, err := strconv.ParseUint(steamID64, 10, 64)
	if err != nil {
		return 0, fmt.Errorf("steamid64 %q is not a valid unsigned integer: %w", steamID64, err)
	}
	if id < steamID3Base {
		return 0, fmt.Errorf("steamid64 %q is below the SteamID3 base offset (%d)", steamID64, steamID3Base)
	}
	return id, nil
}

// steamID3 converts a SteamID64 string to the decimal SteamID3 string the
// miniprofile endpoint expects.
func steamID3(steamID64 string) (string, error) {
	id, err := ValidateSteamID64(steamID64)
	if err != nil {
		return "", err
	}
	return strconv.FormatUint(id-steamID3Base, 10), nil
}

// FetchRichPresence scrapes https://steamcommunity.com/miniprofile/<id3> for
// steamID64 and returns the rich_presence text, or "" if the page has none.
//
// knownGameName, when non-empty, is cross-checked against the miniprofile's
// own game-name span before the rich presence text is trusted — if they
// disagree, the page may be stale or the user may have switched games
// between the official-API call and this scrape, so rich presence is
// suppressed for this call rather than returned mismatched. Pass "" to skip
// the check (e.g. when no official-API result is available to check
// against).
func (c *Client) FetchRichPresence(ctx context.Context, steamID64 string, knownGameName string) (string, error) {
	id3, err := steamID3(steamID64)
	if err != nil {
		return "", err
	}

	reqURL := c.miniprofileBaseURL + "/miniprofile/" + id3

	resp, err := c.doWithRetry(ctx, func(ctx context.Context) (*http.Request, error) {
		return http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	})
	if err != nil {
		return "", fmt.Errorf("fetch miniprofile: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("fetch miniprofile: unexpected status %d", resp.StatusCode)
	}

	doc, err := goquery.NewDocumentFromReader(resp.Body)
	if err != nil {
		return "", fmt.Errorf("parse miniprofile: %w", err)
	}

	if knownGameName != "" {
		miniGameName := strings.TrimSpace(doc.Find("span.miniprofile_game_name").First().Text())
		if miniGameName != "" && miniGameName != knownGameName {
			return "", nil
		}
	}

	richPresence := doc.Find("span.rich_presence").First()
	if richPresence.Length() == 0 {
		return "", nil
	}
	return strings.TrimSpace(richPresence.Text()), nil
}
