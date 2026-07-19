# Database Schema

`poe-info-service` owns and exclusively accesses a single SQLite database, `poe-info-service.db` (see [ADR-006](decisions/006-user-config-storage.md) for why user-facing config lives elsewhere, in the TOML file, never here). It holds everything parsed from `Client.txt` — Path of Exile's log file — plus reference data derived from that parsing, plus data fetched from external APIs (PoE OAuth, Steam). The goal is to build a persistent record of play history that survives across app restarts and can power session stats, event history, and alerts.

All timestamps are stored as ISO 8601 text strings (`TEXT NOT NULL`). The database runs in WAL mode with `synchronous=NORMAL` for write throughput during ingestion (see `sqliteDSN` in `internal/server/server.go`). Nearly every event table has a `UNIQUE` constraint on its meaningful columns so that re-ingesting the same log file is always safe and idempotent.

Schema and migrations are owned by [`internal/schema`](../internal/schema) (`schema.sql` plus `schema.go`'s `migrate()`) — see [`CONTRIBUTING.md`](../CONTRIBUTING.md)'s "Adding a schema migration" section for the process, and [ADR-003](decisions/003-client-api-versioning.md) for why the schema itself (unlike the WebSocket API) is *not* required to stay backward-compatible.

---

## Source of data

Everything in this database originates from one of three places:

- **`Client.txt`** — the game's own log file, parsed line by line by the log ingestion pipeline. This is the primary source and covers the vast majority of tables.
- **In-game `/commands`** — a small number of tables are populated from the output of commands like `/passives` and `/played`, which the game echoes into `Client.txt`.
- **External APIs** — the `leagues` table caches results of the PoE OAuth API's `GET /leagues`; `accounts.poe_uuid`/`oauth_credential_key`/`oauth_authenticated_at` are populated by a PoE OAuth login. See [`api.md`](api.md) and [`architecture.md`](architecture.md) for the fetch/caching rules around these.

---

## Design patterns

**Install-scoped data.** The `installs` table is the root anchor for almost everything. Each row represents a distinct PoE installation path. All session and event tables carry `install_id` (via `session_id`) so the app can handle multiple installs without data collisions.

**Normalized reference tables.** Repeating strings (area codes, character names, account names, skill codes, etc.) are deduplicated into small lookup tables and referenced by integer FK. This keeps event tables lean and makes renaming or enriching a concept (e.g., adding a display name to an area) a single-row update.

**Additive-only migrations.** Per [ADR-003](decisions/003-client-api-versioning.md), the schema is free to change in backward-incompatible ways between versions — unlike the WebSocket API's response shapes — but every actual migration in `internal/schema/schema.go` has so far been additive (new tables/columns), never a destructive rename/repurpose, since there's no compatibility reason to do otherwise yet.

---

## Install tracking

### `installs`

Tracks each PoE installation directory the app has seen. This is the root FK for sessions and (transitively) for all event tables. The ingestion cursor columns let the parser resume from where it left off without re-reading the whole file.

| Column | Notes |
|---|---|
| `path` | Absolute path to the PoE installation directory. Unique. |
| `file_created_at` | Filesystem creation time of `Client.txt` at last ingest (Unix ms). |
| `file_modified_at` | Filesystem modification time at last ingest. Used to detect a replaced log. |
| `file_size` | File size at last ingest. |
| `last_byte_offset` | Byte position to resume reading from. Zero means never ingested. |

---

## Reference / lookup tables

These tables normalize repeating string values into integer IDs. They rarely change after first insert and are referenced by FK from event tables.

### `areas`

Game zones — maps, towns, the character select screen (NULL area_id elsewhere). Identified by an internal `code` string from the log, enriched with a human-readable `display_name` and monster `level` where known.

### `accounts`

Player account names, keyed by name. Most rows come from `Client.txt` guild events and carry only `guild_name`. The local player's own account additionally gains `poe_uuid` (the PoE OAuth token's `sub` claim), `oauth_credential_key`, and `oauth_authenticated_at` once they sign in via PoE OAuth (see `poe-info-service/internal/server/poe_oauth.go`). `oauth_authenticated_at` is non-NULL exactly while that account is the one currently signed in — it and `oauth_credential_key` are cleared on logout, leaving `name`/`poe_uuid` behind as a historical record. Only one account can be OAuth-authenticated at a time today (ADR-005); see `ROADMAP_DETAILS.md`'s "Multi-account PoE OAuth support" entry for why `oauth_credential_key` exists as its own column already.

### `leagues`

Cached results shared by three PoE OAuth API methods (see
`poe-info-service/internal/server/poe_leagues.go`): `poe.leagues.list`
(Bearer-authenticated `GET /account/leagues`, private leagues included),
`poe.leagues.public` (the public, no-Bearer-required `GET /leagues`), and
`poe.leagues.detail` (`GET /league/{name}`) — an update from any of them
upserts the same row shape. Unique on `(name, realm)` since a league name
(e.g. "Standard") repeats identically across realms. `rules_json` is a JSON
array of rule id strings (e.g. `["Hardcore"]`); `fetched_at` drives each
method's own cache-freshness check and is refreshed on every successful
fetch that returns a given row.

### `classes`

Character class names (Marauder, Witch, etc.). Populated on first sight of a character level-up line.

### `characters`

Player characters by name, linked to their `class_id` and last-seen `level`. Covers the local player's own characters; other players' characters appear in `public_chars` if seen only in chat.

### `public_chars`

Characters seen exclusively in public chat channels — name only, no class or level data available. Kept separate from `characters` to avoid polluting the local player's character list with strangers.

### `chat_channels`

Numbered global chat channels the player has joined, with optional `lang` and `name` metadata from the join log line.

### `achievements`

Achievement codes seen unlocked. The code is an internal string from the log; display names are not stored (not available in `Client.txt`).

### `passive_skills`

Passive skill tree nodes — internal `code`, human-readable `name`, and a flag for whether the node is a mastery node. Populated from `/passives` output.

### `passive_quest_sources`

Quest names that grant bonus passive skill points (e.g., "Enemy at the Gate"). Used as the quest dimension in passive point snapshots.

### `hideouts`

Hideout names discovered by the player. Populated on first sight.

### `pvp_matches`

PvP match types seen in queue events (e.g., "Descent: Champions"). Normalized for the same reason as everything else.

### `guild_members`

Links between a `guild_name` (text) and an `account_id`. Populated from guild chat prefixes. Lets you see which accounts belong to a guild over time.

---

## Sessions

A session is a contiguous block of play — from when the game connects to when it disconnects. Sessions are the primary organizational unit: almost every event table references a `session_id` rather than an `install_id` directly.

### `sessions`

One row per play session. Captures the full time range and, at close time, the active account, active character, and last known area. Time columns (`total_secs`, `active_secs`, `afk_secs`) are computed and written when the session ends. `afk_secs` includes both real AFK timeouts and time spent alt-tabbed out — the game treats both as inactivity — and comes from summing `session_afk`.

### `session_afk`

Individual "away" intervals within a session — either a real AFK timeout or the player alt-tabbing out, distinguished by `kind` (`'afk'` or `'alt_tab'`) but otherwise treated identically (see `sessions.afk_secs`). Each row is one contiguous block (`afk_on_at` → `afk_off_at`); the open-ended row (NULL `afk_off_at`) is the current away period if the player hasn't returned yet. `span_id` binds the interval to the `area_time_spans` row it occurred in, so a zone's cumulative away time can be recomputed at any time straight from these child rows; an interval that straddles a zone transition is split into two rows (one per span) at the boundary.

---

## Movement and time-in-area

### `area_moves`

A lightweight append-only record of every area transition: which install, which area, when. One row per `entered_at` timestamp. Used for timeline reconstruction and area-visit counts.

### `area_time_spans`

Contiguous blocks of time spent in one area within one session. Unlike `area_moves` (which is a raw sequence of transitions), this table is about *duration*: `entered_at` + `exited_at` + computed `duration_secs`. Also tracks `afk_secs` (AFK + alt-tab time, see `session_afk`) within the span so time-in-area stats can be broken into active vs. idle. `area_id` is NULL during the character select screen. `char_id` reflects the most recently seen character when the span opened, updated on level-up.

---

## Character progression

### `character_level_events`

One row per level-up per character per install. The `UNIQUE(install_id, char_id, level)` constraint means re-ingesting the same log never double-counts a level.

### `character_deaths`

Deaths in HC or SC, with the area and level at time of death. Linked to both session and character so you can see a character's full death history across sessions.

### `character_played_events`

Time-played snapshots from the in-game `/played` command, which outputs cumulative seconds. Stored per session and optionally per `area_time_span` so drift can be measured. Not a direct parse of gameplay — it's the game's own counter echoed to the log.

### `passive_skill_allocations`

Each time the player allocates or deallocates a passive node within a session. `action` is `'allocated'` or `'deallocated'`. Useful for tracing build progression over a league.

### `passive_point_snapshots`

A snapshot of `/passives` output: total available points, how many are allocated, and the same breakdown for ascendancy points. Split between `level_points` (from leveling) and `quest_points` (from quests). One snapshot per `/passives` invocation.

### `passive_snapshot_quests`

The per-quest breakdown within a snapshot — which quests have granted passive points and how many. Child table of `passive_point_snapshots`.

---

## Social and chat

### `whispers`

Direct messages in both directions (`direction` = `'from'` or `'to'`). Linked to the session they occurred in. The player's own account/character is implicit from the session; `player_name` is always the other party.

### `chats`

Public chat messages. `channel` stores the raw prefix character: `#` global, `$` trade, `%` party, `&` guild. `guild_tag` is the optional `<TAG>` prefix some messages carry. Senders are stored as `public_char_id` (→ `public_chars`), not `characters`, because chat senders are strangers whose class/level we don't know.

### `chat_channel_joins`

Each time the player joins a numbered global chat channel. Used to know which channel the player was on at any given time.

---

## Game events

These tables capture specific recognizable moments from the log. Each one is scoped to a session and has a timestamp.

### `quest_events`

Quest milestones: completing a quest, entering a story area for the first time, etc. `event_type` is a string code identifying the specific milestone. `area_id` is where it happened when determinable.

### `achievement_events`

Achievements unlocked during a session, referenced by `achievement_id` → `achievements`.

### `hideout_discovered_events`

First time a hideout is discovered. `area_id` is the area the player was in when the discovery line appeared (usually the hideout's own area).

### `pvp_queue_events`

PvP queue entries: when the player entered the queue, how many players were in it at that moment, and when the queue was cancelled (if it was). `match_id` → `pvp_matches`.

### `zone_ruleset_failed_events`

Logged when the game reports that a zone's ruleset check failed — appears in racing or HC-validation contexts. `ruleset_name` is the raw string from the log.

### `general_events`

Catch-all for recognized log lines that don't fit a dedicated table. `event_type` is a string code. Adding a new specific table for a given `event_type` is the natural migration path when a category grows large enough to warrant its own columns.

---

## App state

### `app_state`

A simple key→value store for operational metadata the app needs to persist but that isn't user configuration and isn't game data. Currently used to track when each database maintenance tier last ran (`last_routine_check`, `last_full_check`, `last_repair`) so the scheduler can decide whether a full or repair run is due. If this table is unreadable the app treats it as if no checks have ever run, which is safe — it just means maintenance runs sooner rather than later.

---

## Event history spine

### `events`

A lightweight chronological index across all event types, used by the historical events panel. One row is written here (in the same transaction) every time a row is inserted into any of the specific event tables above. `event_type` is a short string identifying the source table (e.g. `'whisper'`, `'death'`, `'achievement'`); `source_id` is the rowid in that table. An index on `occurred_at DESC` makes paginated queries (`LIMIT`/`OFFSET`) fast without scanning every event table via a `UNION ALL`.

| Column | Notes |
|---|---|
| `occurred_at` | Timestamp copied from the source event row. |
| `event_type` | String key identifying the source table. |
| `source_id` | Rowid of the corresponding row in the source table. |
