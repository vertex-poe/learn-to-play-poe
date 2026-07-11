package testfixtures

// Sample JSON responses shaped like ISteamUser/GetPlayerSummaries/v2,
// covering the cases internal/steam's official.go parses for.

const SteamPlayerSummariesInGame = `{
	"response": {
		"players": [
			{
				"steamid": "76561197960287930",
				"personaname": "Xylia",
				"gameid": "238960",
				"gameextrainfo": "Path of Exile"
			}
		]
	}
}`

const SteamPlayerSummariesNotInGame = `{
	"response": {
		"players": [
			{
				"steamid": "76561197960287930",
				"personaname": "Xylia"
			}
		]
	}
}`

// SteamPlayerSummariesMultipleUnordered returns two players in the reverse
// of typical request order, exercising FetchOfficial's keyed-by-steamid
// (not response-order-dependent) result map.
const SteamPlayerSummariesMultipleUnordered = `{
	"response": {
		"players": [
			{
				"steamid": "76561197960287931",
				"personaname": "Bob",
				"gameid": "570",
				"gameextrainfo": "Dota 2"
			},
			{
				"steamid": "76561197960287930",
				"personaname": "Xylia",
				"gameid": "238960",
				"gameextrainfo": "Path of Exile"
			}
		]
	}
}`

const SteamPlayerSummariesEmpty = `{"response": {"players": []}}`
