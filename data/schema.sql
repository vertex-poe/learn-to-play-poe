CREATE TABLE IF NOT EXISTS installs (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    path             TEXT    NOT NULL UNIQUE,
    file_created_at  INTEGER NOT NULL DEFAULT 0,
    file_modified_at INTEGER NOT NULL DEFAULT 0,
    file_size        INTEGER NOT NULL DEFAULT 0,
    last_byte_offset INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS areas (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    code         TEXT    NOT NULL UNIQUE,
    type         TEXT    CHECK(type IN (
                     'Campaign',
                     'Campaign — Act 1',  'Campaign — Act 2',  'Campaign — Act 3',
                     'Campaign — Act 4',  'Campaign — Act 5',  'Campaign — Act 6',
                     'Campaign — Act 7',  'Campaign — Act 8',  'Campaign — Act 9',
                     'Campaign — Act 10',
                     'Campaign — Act 1 — Vaal side area',   'Campaign — Act 2 — Vaal side area',
                     'Campaign — Act 3 — Vaal side area',   'Campaign — Act 4 — Vaal side area',
                     'Campaign — Act 5 — Vaal side area',   'Campaign — Act 6 — Vaal side area',
                     'Campaign — Act 7 — Vaal side area',   'Campaign — Act 8 — Vaal side area',
                     'Campaign — Act 9 — Vaal side area',   'Campaign — Act 10 — Vaal side area',
                     'Map', 'Map — Vaal side area',
                     'Hideout', 'Mechanic', 'Heist', 'Lab', 'Boss Arena', 'PvP'
                 )),
    level        INTEGER,
    display_name TEXT
);

CREATE TABLE IF NOT EXISTS area_moves (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    install_id INTEGER NOT NULL REFERENCES installs(id),
    area_id    INTEGER NOT NULL REFERENCES areas(id),
    entered_at TEXT    NOT NULL,
    UNIQUE (install_id, entered_at)
);

CREATE INDEX IF NOT EXISTS idx_area_moves_install_time
ON area_moves(install_id, entered_at);

CREATE TABLE IF NOT EXISTS accounts (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    name       TEXT    NOT NULL UNIQUE,
    guild_name TEXT
);

CREATE TABLE IF NOT EXISTS chat_channels (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    number INTEGER NOT NULL UNIQUE,
    lang   TEXT,
    name   TEXT
);

CREATE TABLE IF NOT EXISTS classes (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS characters (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT    NOT NULL UNIQUE,
    class_id INTEGER NOT NULL REFERENCES classes(id),
    level    INTEGER
);

CREATE TABLE IF NOT EXISTS character_level_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    install_id  INTEGER NOT NULL REFERENCES installs(id),
    char_id     INTEGER NOT NULL REFERENCES characters(id),
    level       INTEGER NOT NULL,
    occurred_at TEXT    NOT NULL,
    UNIQUE (install_id, char_id, level)
);

CREATE TABLE IF NOT EXISTS chat_channel_joins (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    install_id INTEGER NOT NULL REFERENCES installs(id),
    channel_id INTEGER NOT NULL REFERENCES chat_channels(id),
    joined_at  TEXT    NOT NULL,
    UNIQUE (install_id, joined_at)
);

CREATE TABLE IF NOT EXISTS sessions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    install_id  INTEGER NOT NULL REFERENCES installs(id),
    started_at  TEXT    NOT NULL,
    ended_at    TEXT,
    total_secs  INTEGER,
    active_secs INTEGER,
    afk_secs    INTEGER,
    account_id  INTEGER REFERENCES accounts(id),
    char_id     INTEGER REFERENCES characters(id),
    area_id     INTEGER REFERENCES areas(id),
    UNIQUE(install_id, started_at)
);

CREATE TABLE IF NOT EXISTS session_afk (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES sessions(id),
    afk_on_at  TEXT    NOT NULL,
    afk_off_at TEXT,
    UNIQUE(session_id, afk_on_at)
);

-- One row per contiguous period spent in a single area (area_id NULL = character select).
-- char_id is the most recently seen character at the time the span opened, updated on
-- level-up while the span is open. Duration is computed in SQL on close so we never
-- have to carry entered_at in memory.
CREATE TABLE IF NOT EXISTS area_time_spans (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id    INTEGER NOT NULL REFERENCES sessions(id),
    area_id       INTEGER REFERENCES areas(id),
    char_id       INTEGER REFERENCES characters(id),
    entered_at    TEXT    NOT NULL,
    exited_at     TEXT,
    duration_secs INTEGER,
    afk_secs      INTEGER NOT NULL DEFAULT 0,
    UNIQUE(session_id, entered_at)
);

CREATE TABLE IF NOT EXISTS passive_skills (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    code       TEXT    NOT NULL UNIQUE,
    name       TEXT    NOT NULL,
    is_mastery INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS passive_skill_allocations (
    id               INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id       INTEGER NOT NULL REFERENCES sessions(id),
    char_id          INTEGER REFERENCES characters(id),
    passive_skill_id INTEGER NOT NULL REFERENCES passive_skills(id),
    action           TEXT    NOT NULL DEFAULT 'allocated',
    allocated_at     TEXT    NOT NULL,
    UNIQUE(session_id, passive_skill_id, action, allocated_at)
);

CREATE TABLE IF NOT EXISTS guilds (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    tag  TEXT    NOT NULL UNIQUE,
    name TEXT    NOT NULL DEFAULT ''
);

CREATE TABLE IF NOT EXISTS whispers (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    direction   TEXT    NOT NULL CHECK(direction IN ('from', 'to')),
    player_name TEXT    NOT NULL,
    guild_id    INTEGER REFERENCES guilds(id),
    message     TEXT    NOT NULL,
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, occurred_at, direction, player_name)
);

CREATE TABLE IF NOT EXISTS quest_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    area_id     INTEGER REFERENCES areas(id),
    event_type  TEXT    NOT NULL,
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, occurred_at, event_type)
);

CREATE TABLE IF NOT EXISTS character_deaths (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    char_id     INTEGER NOT NULL REFERENCES characters(id),
    area_id     INTEGER REFERENCES areas(id),
    level       INTEGER,
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, char_id, occurred_at)
);

-- Players seen only in public chat or whispers — name only, no class/level info required.
CREATE TABLE IF NOT EXISTS public_chars (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    name     TEXT    NOT NULL UNIQUE,
    guild_id INTEGER REFERENCES guilds(id)
);

CREATE TABLE IF NOT EXISTS character_played_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    span_id     INTEGER REFERENCES area_time_spans(id),
    played_secs INTEGER NOT NULL,
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, occurred_at)
);

CREATE TABLE IF NOT EXISTS achievements (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    code TEXT    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS achievement_events (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id     INTEGER NOT NULL REFERENCES sessions(id),
    achievement_id INTEGER NOT NULL REFERENCES achievements(id),
    occurred_at    TEXT    NOT NULL,
    UNIQUE(session_id, achievement_id, occurred_at)
);

-- Snapshot of /passives output: totals + per-quest breakdown at a point in time.
CREATE TABLE IF NOT EXISTS passive_point_snapshots (
    id                   INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id           INTEGER NOT NULL REFERENCES sessions(id),
    char_id              INTEGER REFERENCES characters(id),
    occurred_at          TEXT    NOT NULL,
    total_points         INTEGER NOT NULL DEFAULT 0,
    allocated_points     INTEGER NOT NULL DEFAULT 0,
    ascendancy_total     INTEGER NOT NULL DEFAULT 0,
    ascendancy_allocated INTEGER NOT NULL DEFAULT 0,
    level_points         INTEGER NOT NULL DEFAULT 0,
    quest_points         INTEGER NOT NULL DEFAULT 0,
    UNIQUE(session_id, occurred_at)
);

CREATE TABLE IF NOT EXISTS passive_quest_sources (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS passive_snapshot_quests (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    snapshot_id INTEGER NOT NULL REFERENCES passive_point_snapshots(id),
    quest_id    INTEGER NOT NULL REFERENCES passive_quest_sources(id),
    points      INTEGER NOT NULL DEFAULT 1,
    UNIQUE(snapshot_id, quest_id)
);

CREATE TABLE IF NOT EXISTS hideouts (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS hideout_discovered_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    hideout_id  INTEGER NOT NULL REFERENCES hideouts(id),
    area_id     INTEGER REFERENCES areas(id),
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, hideout_id, occurred_at)
);

CREATE TABLE IF NOT EXISTS pvp_matches (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT    NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS pvp_queue_events (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id   INTEGER NOT NULL REFERENCES sessions(id),
    match_id     INTEGER NOT NULL REFERENCES pvp_matches(id),
    player_count INTEGER NOT NULL DEFAULT 0,
    occurred_at  TEXT    NOT NULL,
    cancelled_at TEXT,
    UNIQUE(session_id, occurred_at)
);

CREATE TABLE IF NOT EXISTS guild_members (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    guild_name TEXT    NOT NULL,
    account_id INTEGER NOT NULL REFERENCES accounts(id),
    UNIQUE(guild_name, account_id)
);

CREATE TABLE IF NOT EXISTS zone_ruleset_failed_events (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id   INTEGER NOT NULL REFERENCES sessions(id),
    area_id      INTEGER REFERENCES areas(id),
    ruleset_name TEXT    NOT NULL,
    occurred_at  TEXT    NOT NULL,
    UNIQUE(session_id, ruleset_name, occurred_at)
);

-- channel stores the raw prefix character: '#' global, '$' trade, '%' party, '&' guild.
CREATE TABLE IF NOT EXISTS chats (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id     INTEGER NOT NULL REFERENCES sessions(id),
    public_char_id INTEGER NOT NULL REFERENCES public_chars(id),
    channel        TEXT    NOT NULL,
    guild_id       INTEGER REFERENCES guilds(id),
    message        TEXT    NOT NULL,
    occurred_at    TEXT    NOT NULL,
    UNIQUE(session_id, occurred_at, public_char_id, channel)
);

CREATE INDEX IF NOT EXISTS idx_chats_by_time
ON chats(occurred_at DESC, channel);

CREATE INDEX IF NOT EXISTS idx_whispers_by_time
ON whispers(occurred_at DESC);

CREATE TABLE IF NOT EXISTS general_events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id  INTEGER NOT NULL REFERENCES sessions(id),
    area_id     INTEGER REFERENCES areas(id),
    event_type  TEXT    NOT NULL,
    occurred_at TEXT    NOT NULL,
    UNIQUE(session_id, occurred_at, event_type)
);

-- Operational metadata: arbitrary key→value pairs for the app's own bookkeeping
-- (maintenance timestamps, etc.). Not user configuration; not game data.
CREATE TABLE IF NOT EXISTS npc_dialog_entries (
    message_hash  TEXT NOT NULL PRIMARY KEY,
    npc_name      TEXT NOT NULL,
    npc_name_hash TEXT NOT NULL,
    label         TEXT
);

CREATE TABLE IF NOT EXISTS app_state (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);

-- Chronological spine for the historical events panel. One row per event regardless
-- of type; source_id is the rowid in the type-specific table. Indexed on occurred_at
-- so paginated queries never scan the full union of event tables.
-- UNIQUE(event_type, source_id) makes INSERT OR IGNORE safe on re-ingest.
CREATE TABLE IF NOT EXISTS events (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    occurred_at TEXT    NOT NULL,
    event_type  TEXT    NOT NULL,
    source_id   INTEGER NOT NULL,
    UNIQUE(event_type, source_id)
);

CREATE INDEX IF NOT EXISTS idx_events_by_time ON events (occurred_at DESC);

CREATE INDEX IF NOT EXISTS idx_sessions_started_at ON sessions (started_at DESC);

CREATE INDEX IF NOT EXISTS idx_sessions_ended_at
ON sessions (ended_at DESC) WHERE ended_at IS NOT NULL;
