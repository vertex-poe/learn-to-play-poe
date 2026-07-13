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
	FetchedAt    int64  `json:"fetchedAt"`              // unix seconds of the last fetch attempt; 0 if never fetched
	Status       string `json:"status"`                 // RichPresenceStatus*; an open string so new values are addable later per ADR-003
	Error        string `json:"error,omitempty"`        // human-readable detail, populated only when status=="error"
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
	// Detail is a zero-cost enrichment: whatever the leagues table already
	// has cached for League (from some earlier poe.leagues.list/.detail
	// fetch), joined in for free — this never itself triggers a PoE OAuth
	// API call. Nil if League is empty, or nothing is cached for it yet
	// (see internal/server/poe_leagues.go's queryLeagueByName).
	Detail *LeagueSummary `json:"detail,omitempty"`
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

// PoeAccountSummary is one row of the "poe.accounts.list" response — every
// account this service knows of, whether learned from Client.txt guild
// events, a PoE OAuth login, or both merged onto the same row by name. Never
// includes the credential itself (see PoeOAuthStatusPayload's doc comment).
type PoeAccountSummary struct {
	Name string `json:"name"`
	// PoeUUID is the OAuth `sub` claim — empty if this account has never
	// been OAuth-authenticated locally (e.g. a friend's account only ever
	// seen in guild chat).
	PoeUUID string `json:"poeUuid,omitempty"`
	// Active is true for the account currently signed in via PoE OAuth on
	// this service — at most one row has Active true at any time (ADR-005).
	Active bool `json:"active"`
}

// RateLimitRule mirrors internal/reqqueue.Rule over the wire — one
// rate-limit rule's last-known state. Remaining/ResetsAt are both
// best-effort: reqqueue tracks state purely from the most recent response
// that happened to report it, and (per its documented simplifications)
// estimates a saturated rule's reset time as its full Period rather than
// computing the exact remaining time in its rolling window.
type RateLimitRule struct {
	Name string `json:"name"`
	// Limit/Remaining are this rule's hit budget and how much of it looks
	// unused as of the last-known state — Remaining is floored at 0.
	Limit         int `json:"limit"`
	Remaining     int `json:"remaining"`
	PeriodSeconds int `json:"periodSeconds"`
	// ResetsAt is unix seconds this rule is expected to clear again, only
	// set (non-zero) while the rule looks saturated (Remaining == 0) — a
	// rule with headroom has no known reset time at all, see above.
	ResetsAt int64 `json:"resetsAt,omitempty"`
}

// FetchCost reports what one request actually cost against an external
// rate-limited API. Present only on a response that caused a real fetch —
// a cache hit (or a fetch:"never" peek) costs nothing and never carries a
// Cost, regardless of whether a caller asked for cost reporting (see
// poe.profile.*/poe.leagues.list/poe.leagues.detail's includeCost param).
type FetchCost struct {
	API string `json:"api"` // "poe-oauth" today — a label, not a URL
	// Policy is the rate-limit policy this call was billed against, ""
	// if not yet learned (e.g. this endpoint's very first-ever call, before
	// any response has revealed its real policy name — see
	// internal/reqqueue.Task.PolicyHint's doc comment).
	Policy string `json:"policy,omitempty"`
	// Queries is how many external HTTP calls this request caused —
	// always 1 when Cost is present at all (every fetch here is a single
	// HTTP round-trip); the field exists so a future batched/paginated
	// fetch can report more than one without a shape change.
	Queries int             `json:"queries"`
	Rules   []RateLimitRule `json:"rules,omitempty"`
}

// PoeProfileFieldPayload is the "poe.profile.locale"/"poe.profile.twitch"
// response shape, and what's published to TopicPoeProfile. Status is
// "fresh" (served straight from a cache entry still within the requested
// max-age), "stale" (a cache entry exists but is outside max-age, and no
// fetch was made — fetch:"never", or fetch:"ifStale" with no way to fetch),
// "miss" (no cache at all, no fetch made), "pending" (a fetch was enqueued
// but the caller didn't ask to wait — the real value arrives later via
// TopicPoeProfile), or "ok" (a wait:true request's fetch completed in
// time). Freshness/Fetching are the same information as Status, split into
// two orthogonal fields for a caller that wants to branch on them directly
// rather than parsing Status's combined vocabulary — Freshness is
// "fresh"/"stale"/"miss" and never changes once set on a given response;
// Fetching is true only for "pending"/"ok" (a fetch happened or is
// happening). Value/FetchedAt are populated whenever Freshness isn't
// "miss" — the caller asked for what's cached, and it existed, even if
// stale. Cost is set only when this specific call actually performed a
// fetch and the caller requested it (see poeProfileFieldRequest.IncludeCost).
type PoeProfileFieldPayload struct {
	Status    string     `json:"status"`
	Freshness string     `json:"freshness"`
	Fetching  bool       `json:"fetching"`
	Value     string     `json:"value,omitempty"`
	FetchedAt int64      `json:"fetchedAt,omitempty"`
	Error     string     `json:"error,omitempty"`
	Cost      *FetchCost `json:"cost,omitempty"`
}

// TopicPoeProfile carries the full profile (poeUuid/name/locale/twitch/
// fetchedAt — see server.poeProfileCacheEntry) whenever a /profile fetch
// completes, letting a poe.profile.locale/.twitch caller that didn't ask to
// block (wait:false) learn the result asynchronously instead of polling.
const TopicPoeProfile = "poeProfile"

// PoeProfilePayload is what's published to TopicPoeProfile. Cost is always
// populated when present (a topic push only ever happens after a real
// fetch attempt) — unlike the request/response path, there's no per-caller
// includeCost opt-in for a broadcast topic.
type PoeProfilePayload struct {
	PoeUUID   string     `json:"poeUuid"`
	Name      string     `json:"name"`
	Locale    string     `json:"locale,omitempty"`
	Twitch    string     `json:"twitch,omitempty"`
	FetchedAt int64      `json:"fetchedAt"`
	Error     string     `json:"error,omitempty"`
	Cost      *FetchCost `json:"cost,omitempty"`
}

// LeagueSummary is one element of the "poe.leagues.list" response's leagues
// array, and one row of the leagues table (see
// internal/server/poe_leagues.go). Unlike LeaguePayload (the player's
// *current* league, parsed from Steam rich-presence text), this is the full
// catalogue of active leagues fetched from the PoE OAuth API's GET /leagues.
// Rules is the flattened list of each poe.LeagueRule's ID (e.g. "Hardcore",
// "NoParties") — no other per-rule metadata exists today.
type LeagueSummary struct {
	Name        string   `json:"name"`
	Realm       string   `json:"realm"`
	URL         string   `json:"url,omitempty"`
	StartAt     string   `json:"startAt,omitempty"`
	EndAt       string   `json:"endAt,omitempty"` // "" = permanent league
	Description string   `json:"description,omitempty"`
	Rules       []string `json:"rules,omitempty"`
	Event       bool     `json:"event"`
	DelveEvent  bool     `json:"delveEvent"`
}

// PoeLeaguesPayload is the "poe.leagues.list" response shape, and what's
// published to TopicPoeLeagues. Status mirrors PoeProfileFieldPayload's
// convention (see its doc comment for the full "fresh"/"stale"/"miss"/
// "pending"/"ok"/"error" vocabulary and what Freshness/Fetching split out of
// it). Leagues/FetchedAt are populated whenever Freshness isn't "miss" —
// including on a "pending" response, where they carry whatever was cached
// before this fetch (possibly stale, possibly empty) so a caller has
// something to show immediately rather than nothing until the fetch
// completes. Cost is set only when this specific call actually performed a
// fetch and the caller requested it (see poeLeaguesRequest.IncludeCost).
type PoeLeaguesPayload struct {
	Status    string          `json:"status"`
	Freshness string          `json:"freshness"`
	Fetching  bool            `json:"fetching"`
	Leagues   []LeagueSummary `json:"leagues,omitempty"`
	FetchedAt int64           `json:"fetchedAt,omitempty"`
	Error     string          `json:"error,omitempty"`
	Cost      *FetchCost      `json:"cost,omitempty"`
}

// TopicPoeLeagues carries PoeLeaguesPayload whenever a GET /leagues fetch
// completes (successfully or not), letting a poe.leagues.list *or*
// poe.leagues.detail caller that didn't ask to block (wait:false) learn the
// result asynchronously instead of polling — poe.leagues.detail has no
// topic of its own since, today, a "detail" fetch is the same underlying
// bulk /leagues call poe.leagues.list makes (see
// internal/server/poe_leagues.go); a detail caller filters this topic's
// Leagues array for the one name it asked about. Cost is always populated
// when present, the same as TopicPoeProfile.
const TopicPoeLeagues = "poeLeagues"

// PoeLeagueDetailPayload is the "poe.leagues.detail" response shape — one
// specific league's cached row, by name, rather than poe.leagues.list's
// whole catalogue. See its handler's doc comment
// (internal/server/poe_leagues.go) for why this has no dedicated fetch
// path of its own today. Status/Freshness/Fetching/Cost follow the same
// vocabulary as PoeLeaguesPayload; League/FetchedAt are populated whenever
// Freshness isn't "miss".
type PoeLeagueDetailPayload struct {
	Status    string         `json:"status"`
	Freshness string         `json:"freshness"`
	Fetching  bool           `json:"fetching"`
	League    *LeagueSummary `json:"league,omitempty"`
	FetchedAt int64          `json:"fetchedAt,omitempty"`
	Error     string         `json:"error,omitempty"`
	Cost      *FetchCost     `json:"cost,omitempty"`
}

// PoeRateLimitPolicyPayload is one policy's entry in
// PoeRateLimitStatusPayload — mirrors internal/reqqueue.PolicyReport over
// the wire.
type PoeRateLimitPolicyPayload struct {
	Policy string          `json:"policy"`
	Rules  []RateLimitRule `json:"rules"`
	// NextAllowedAt is unix seconds this policy will next be dispatchable,
	// omitted if it's clear to dispatch right now.
	NextAllowedAt int64 `json:"nextAllowedAt,omitempty"`
}

// PoeRateLimitStatusPayload is the "poe.ratelimit.status" response shape,
// and what's published to TopicPoeRateLimit. Policies covers every
// rate-limit policy this service's PoE OAuth request queue has learned
// about so far — a policy never dispatched under yet (including one this
// service has simply never called) is absent, not zero-valued.
type PoeRateLimitStatusPayload struct {
	Policies []PoeRateLimitPolicyPayload `json:"policies"`
}

// TopicPoeRateLimit carries PoeRateLimitStatusPayload, published whenever a
// PoE OAuth fetch (poe.profile.*, poe.leagues.*) updates a policy's known
// rate-limit state — lets a client watch live budget without polling
// poe.ratelimit.status.
const TopicPoeRateLimit = "poeRateLimit"
