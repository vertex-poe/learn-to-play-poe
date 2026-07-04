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

// TopicIngest carries a single "caught_up" event once backlog replay of
// Client.txt finishes and the tailer begins tailing live, so clients know
// it's now safe to query Client.txt-derived history without racing replay.
const TopicIngest = "ingest"

type IngestEventPayload struct {
	Type string `json:"type"` // "caught_up"
}

const IngestEventCaughtUp = "caught_up"
