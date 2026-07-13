package proto

import "encoding/json"

const Version = "0.1.0"

type MessageType string

const (
	TypeHello       MessageType = "hello"
	TypeStepDown    MessageType = "step-down"
	TypeSubscribe   MessageType = "subscribe"
	TypeUnsubscribe MessageType = "unsubscribe"
	TypeEvent       MessageType = "event"
	TypeRequest     MessageType = "request"
	TypeResponse    MessageType = "response"
	TypePing        MessageType = "ping"
	TypePong        MessageType = "pong"
	TypeKeepalive   MessageType = "keepalive"
)

type Message struct {
	Type    MessageType     `json:"type"`
	ID      string          `json:"id,omitempty"`
	Topic   string          `json:"topic,omitempty"`
	Method  string          `json:"method,omitempty"`
	Error   string          `json:"error,omitempty"`
	Payload json.RawMessage `json:"payload,omitempty"`
}

type HelloPayload struct {
	Version   string `json:"version"`
	StartTime int64  `json:"startTime"` // Unix seconds
}

type ParsedEvent struct {
	Type      string         `json:"type"`
	Timestamp string         `json:"timestamp"`
	Data      map[string]any `json:"data,omitempty"`
}

const (
	EventAreaEntered        = "area_entered"
	EventLevelUp            = "level_up"
	EventCharacterDeath     = "character_death"
	EventAfkOn              = "afk_on"
	EventAfkOff             = "afk_off"
	EventWhisper            = "whisper"
	EventChat               = "chat"
	EventAchievement        = "achievement"
	EventHideoutDiscovered  = "hideout_discovered"
	EventPvpQueue           = "pvp_queue"
	EventPvpQueueCancelled  = "pvp_queue_cancelled"
	EventPassiveAllocated   = "passive_allocated"
	EventPassiveUnallocated = "passive_unallocated"
	EventQuestEvent         = "quest_event"
	EventGeneralEvent       = "general_event"
	EventSessionStart       = "session_start"
	EventLoginScreen        = "login_screen"
	EventCharSelect         = "char_select"
	EventAltTabOut          = "alt_tab_out"
	EventAltTabBack         = "alt_tab_back"

	// DB-only event types: written to the l2p database by the ingest writer
	// but never broadcast to the "clientlog" pub/sub topic (no overlay use).
	EventPlayed             = "played"
	EventPassivesSnapshot   = "passives_snapshot"
	EventGuildJoined        = "guild_joined"
	EventGuildMemberUpdated = "guild_member_updated"
	EventChatChannelJoin    = "chat_channel_join"
)

type StatusPayload struct {
	Version   string   `json:"version"`
	StartTime int64    `json:"startTime"`
	LogPath   string   `json:"logPath"`
	LogOffset int64    `json:"logOffset"`
	Uptime    string   `json:"uptime"`
	Phase     string   `json:"phase"`             // "waiting" | "ingesting" | "tailing"
	Message   string   `json:"message"`           // human-readable: "waiting" | "processing game logs" | "waiting for game events"
	Percent   *float64 `json:"percent,omitempty"` // 0-100 backlog-replay progress; present only while phase=="ingesting"
}

// TopicStatus carries the same shape as the "status" request's response
// (StatusPayload above), published whenever phase changes or percent crosses
// into a new whole percent — see watchIngestStatus in server.go. Clients
// request "status" once on connect for an initial snapshot, then subscribe
// to this topic instead of re-polling "status" for the (potentially long)
// duration of a Client.txt backlog replay.
const TopicStatus = "status"

// TopicConfig carries the same shape as the "config.list" request's
// response (a map[string]configEntry under a "settings" key, see
// server.configSnapshot), published whenever a mutable setting changes —
// whether from a client's own config.set call, another connected client's,
// or the auto-detect loop finding a new install dir on its own. Lets a
// client (e.g. l2p-poe's Settings > Game page, and its startup
// no-install-dirs notice) stay in sync without re-polling config.list.
const TopicConfig = "config"

// SteamPresenceEntry is one tracked steamid64's most recently fetched Steam
// presence — official-API fields (personaName/gameName/gameAppId/inGame;
// populated only once a client has stored a "steamApiKey" credential via
// credentials.store) combined with the unofficial rich-presence scrape
// (richPresence; works with no credential) into a single entry, per the
// project's "one combined method" decision. See poe-info-service's
// CONTRIBUTING.md "Steam presence" section for the full contract.
type SteamPresenceEntry struct {
	SteamID64    string `json:"steamId64"`
	PersonaName  string `json:"personaName,omitempty"`  // "" without a stored steamApiKey, or before first successful official fetch
	GameName     string `json:"gameName,omitempty"`     // gameextrainfo; "" if not in a game, or no steamApiKey
	GameAppID    string `json:"gameAppId,omitempty"`    // gameid; "" alongside GameName
	InGame       bool   `json:"inGame"`                 // always false without a stored steamApiKey
	RichPresence string `json:"richPresence,omitempty"` // parsed rich_presence text; "" if absent, or the mismatch guard suppressed it
	FetchedAt    int64  `json:"fetchedAt"`              // unix seconds of the last successful fetch; 0 if never fetched
	Status       string `json:"status"`                 // SteamPresenceStatus*; an open string so new values are addable later per ADR-003
	Error        string `json:"error,omitempty"`        // human-readable detail, populated only when status=="error"
}

// SteamPresencePayload is both the "steam.presence" request's response shape
// and the payload published to TopicSteamPresence — one entry per configured
// steam_ids entry, in configured order.
type SteamPresencePayload struct {
	Entries []SteamPresenceEntry `json:"entries"`
}

const (
	// SteamPresenceStatusPending marks an id that's configured but has not
	// been fetched yet — e.g. the server just started, or no client has
	// subscribed to TopicSteamPresence yet (fetching is subscriber-gated).
	SteamPresenceStatusPending = "pending"
	SteamPresenceStatusOK      = "ok"
	// SteamPresenceStatusError marks an id whose most recent fetch attempt
	// failed (network error, non-200, etc.) — Error carries the detail.
	SteamPresenceStatusError = "error"
)

// TopicSteamPresence carries SteamPresencePayload, published after each
// completed Steam poll cycle. A client must subscribe to this topic to
// activate polling at all: the background poller only contacts Steam while
// at least one subscriber exists (Hub.HasSubscribers), since it's a
// rate-limited external resource. Requesting "steam.presence" without
// subscribing returns whatever is cached (possibly every entry still
// "pending") and never triggers a fetch itself.
const TopicSteamPresence = "steamPresence"

// PoeOAuthStatusPayload is both the "poe.oauth.status" request's response
// shape and the payload published to TopicPoeOAuthStatus. Per ADR-004, this
// service never returns the underlying access/refresh token to a client —
// only whether one is held and, if so, non-secret metadata about it.
type PoeOAuthStatusPayload struct {
	Authorized bool `json:"authorized"` // a token is stored and hasn't outlived its assumed refresh-token lifetime
	InProgress bool `json:"inProgress"` // a poe.oauth.login flow is currently waiting on the browser
	// Username/Scope/AccessExpiration are populated only when Authorized —
	// display-only metadata, never the token itself.
	Username         string `json:"username,omitempty"`
	Scope            string `json:"scope,omitempty"`
	AccessExpiration int64  `json:"accessExpiration,omitempty"` // unix seconds
	// Error carries the most recent login/refresh failure's message, if any
	// — cleared on the next successful login.
	Error string `json:"error,omitempty"`
}

// TopicPoeOAuthStatus carries PoeOAuthStatusPayload, published whenever the
// PoE OAuth state changes: a login attempt starts, succeeds, or fails; a
// background refresh succeeds or fails; or the client logs out
// (poe.oauth.logout). Lets a client reflect connection status live instead
// of polling poe.oauth.status.
const TopicPoeOAuthStatus = "poeOAuthStatus"
