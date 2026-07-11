package steam

import (
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/url"
	"strings"
)

// OfficialResult is one steamid64's ISteamUser/GetPlayerSummaries result.
// Exported so a caller (internal/server's poller) can fetch it once per
// batch and hold it between the FetchOfficial and FetchPresence calls.
type OfficialResult struct {
	PersonaName string
	GameName    string // gameextrainfo; "" if not currently in a game
	GameAppID   string // gameid; "" alongside GameName
	InGame      bool
}

type playerSummariesResponse struct {
	Response struct {
		Players []struct {
			SteamID       string `json:"steamid"`
			PersonaName   string `json:"personaname"`
			GameID        string `json:"gameid"`
			GameExtraInfo string `json:"gameextrainfo"`
		} `json:"players"`
	} `json:"response"`
}

// FetchOfficial calls ISteamUser/GetPlayerSummaries once for the whole
// steamIDs list (Steam accepts a comma-joined batch, same as the reference
// implementation's userID string), and returns a result per id that was
// present in the response, keyed by steamid64. Steam does not guarantee the
// response is in request order, and silently omits ids it doesn't recognize
// — callers should treat a missing map entry the same as "not in a game."
func (c *Client) FetchOfficial(ctx context.Context, apiKey string, steamIDs []string) (map[string]OfficialResult, error) {
	if apiKey == "" || len(steamIDs) == 0 {
		return map[string]OfficialResult{}, nil
	}

	reqURL := c.officialBaseURL + "/ISteamUser/GetPlayerSummaries/v2/?" + url.Values{
		"key":      {apiKey},
		"format":   {"json"},
		"steamids": {strings.Join(steamIDs, ",")},
	}.Encode()

	resp, err := c.doWithRetry(ctx, func(ctx context.Context) (*http.Request, error) {
		return http.NewRequestWithContext(ctx, http.MethodGet, reqURL, nil)
	})
	if err != nil {
		return nil, fmt.Errorf("fetch player summaries: %w", err)
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("fetch player summaries: unexpected status %d", resp.StatusCode)
	}

	var parsed playerSummariesResponse
	if err := json.NewDecoder(resp.Body).Decode(&parsed); err != nil {
		return nil, fmt.Errorf("decode player summaries: %w", err)
	}

	out := make(map[string]OfficialResult, len(parsed.Response.Players))
	for _, p := range parsed.Response.Players {
		out[p.SteamID] = OfficialResult{
			PersonaName: p.PersonaName,
			GameName:    p.GameExtraInfo,
			GameAppID:   p.GameID,
			InGame:      p.GameExtraInfo != "",
		}
	}
	return out, nil
}
