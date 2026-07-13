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

// RichPresencePayload is both the "steam.presence" request's response shape
// and the payload published to TopicSteamPresence: the single tracked
// steam_id's most recently fetched Steam rich-presence text, verbatim (no
// parsing — see CharacterLevelPayload/CharacterClassPayload/LeaguePayload
// for the parsed parts). See poe-info-service's CONTRIBUTING.md "Steam
// presence" section for the full contract.
type RichPresencePayload struct {
	RichPresence string `json:"richPresence,omitempty"` // "" if never fetched, or Steam reported no rich-presence text
	FetchedAt    int64  `json:"fetchedAt"`               // unix seconds of the last fetch attempt; 0 if never fetched
	Status       string `json:"status"`                  // RichPresenceStatus*; an open string so new values are addable later per ADR-003
	Error        string `json:"error,omitempty"`         // human-readable detail, populated only when status=="error"
}

const (
	// RichPresenceStatusPending marks a steam_id that's configured but has
	// not been fetched yet — e.g. the server just started, or no client has
	// requested/subscribed yet.
	RichPresenceStatusPending = "pending"
	RichPresenceStatusOK      = "ok"
	// RichPresenceStatusError marks a fetch attempt that failed (network
	// error, non-200, etc.) — Error carries the detail. The previous
	// RichPresence/league/level/class values are left in place rather than
	// cleared, so a transient failure doesn't blank out otherwise-still-valid
	// data.
	RichPresenceStatusError = "error"
)

// RichPresenceSourceSteam identifies Steam's rich-presence scrape as the
// origin of a CharacterLevelPayload/CharacterClassPayload/LeaguePayload
// value. Deliberately an open string, not baked into the topic/method name
// (see TopicCharacterLevel etc.) — per ADR-003, once these concept-named
// topics ship their shape is permanent, so a future second source (e.g.
// Client.txt-derived level/class, or a Steam Deck acting as the primary
// source when Client.txt isn't locally available — see ROADMAP) can be
// added as a new Source value without any rename.
const RichPresenceSourceSteam = "steamRichPresence"

// TopicSteamPresence carries RichPresencePayload, published whenever the raw
// rich-presence text changes. A client must subscribe to this topic (or one
// of TopicCharacterLevel/TopicCharacterClass/TopicLeague) to activate
// background polling at all — the poller only contacts Steam while at least
// one subscriber exists across any of those topics (Hub.HasSubscribers),
// since it's a rate-limited external resource. Requesting "steam.presence"
// (or character.level/character.class/poe.league) always fetches fresh data
// first if the cached copy is more than 25s old, regardless of subscription
// state — see CONTRIBUTING.md.
const TopicSteamPresence = "steamPresence"

// CharacterLevelPayload is both the "character.level" request's response
// shape and the payload published to TopicCharacterLevel, parsed from the
// rich-presence text (e.g. "SSF Ancestors: 92 Warden - The Sarn Encampment"
// → Level: 92). Level is 0 and Source is "" if the current rich-presence
// text doesn't match the expected shape (not in a PoE session, or nothing
// fetched yet).
type CharacterLevelPayload struct {
	Level     int    `json:"level"`
	Source    string `json:"source,omitempty"`
	FetchedAt int64  `json:"fetchedAt"`
}

const TopicCharacterLevel = "character.level"

// CharacterClassPayload is both the "character.class" request's response
// shape and the payload published to TopicCharacterClass, parsed from the
// rich-presence text (e.g. "...92 Warden..." → Class: "Warden").
type CharacterClassPayload struct {
	Class     string `json:"class,omitempty"`
	Source    string `json:"source,omitempty"`
	FetchedAt int64  `json:"fetchedAt"`
}

const TopicCharacterClass = "character.class"

// LeaguePayload is both the "poe.league" request's response shape and the
// payload published to TopicLeague, parsed from the rich-presence text
// (e.g. "SSF Ancestors: ..." → League: "SSF Ancestors").
type LeaguePayload struct {
	League    string `json:"league,omitempty"`
	Source    string `json:"source,omitempty"`
	FetchedAt int64  `json:"fetchedAt"`
}

const TopicLeague = "poe.league"

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
