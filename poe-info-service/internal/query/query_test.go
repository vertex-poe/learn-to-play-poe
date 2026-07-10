package query

import (
	"database/sql"
	"strings"
	"testing"
)

// Minimal schema covering the tables used by the query functions under test.
const testSchema = `
CREATE TABLE installs (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    path TEXT NOT NULL UNIQUE
);
CREATE TABLE accounts (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL
);
CREATE TABLE classes (
    id   INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL
);
CREATE TABLE characters (
    id       INTEGER PRIMARY KEY AUTOINCREMENT,
    class_id INTEGER REFERENCES classes(id),
    name     TEXT NOT NULL
);
CREATE TABLE sessions (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    install_id  INTEGER NOT NULL REFERENCES installs(id),
    account_id  INTEGER REFERENCES accounts(id),
    char_id     INTEGER REFERENCES characters(id),
    started_at  TEXT NOT NULL,
    ended_at    TEXT,
    total_secs  INTEGER,
    active_secs INTEGER,
    afk_secs    INTEGER
);
CREATE TABLE chats (
    id             INTEGER PRIMARY KEY AUTOINCREMENT,
    public_char_id INTEGER NOT NULL DEFAULT 0,
    guild_id       INTEGER,
    channel        TEXT NOT NULL,
    message        TEXT NOT NULL,
    occurred_at    TEXT NOT NULL
);
CREATE TABLE whispers (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    guild_id    INTEGER,
    direction   TEXT NOT NULL,
    player_name TEXT NOT NULL,
    message     TEXT NOT NULL,
    occurred_at TEXT NOT NULL
);
CREATE TABLE areas (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    code         TEXT NOT NULL UNIQUE,
    display_name TEXT,
    type         TEXT,
    subtype      TEXT,
    level        INTEGER
);
CREATE TABLE area_time_spans (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id    INTEGER NOT NULL REFERENCES sessions(id),
    area_id       INTEGER REFERENCES areas(id),
    entered_at    TEXT NOT NULL,
    exited_at     TEXT,
    duration_secs INTEGER,
    afk_secs      INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE session_afk (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    session_id INTEGER NOT NULL REFERENCES sessions(id),
    span_id    INTEGER REFERENCES area_time_spans(id),
    kind       TEXT NOT NULL DEFAULT 'afk',
    afk_on_at  TEXT NOT NULL,
    afk_off_at TEXT
);
`

func openTestDB(t *testing.T) *DB {
	t.Helper()
	// Each test gets its own named in-memory DB so there is no cross-test contamination.
	name := strings.ReplaceAll(t.Name(), "/", "_")
	raw, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	raw.SetMaxOpenConns(1)
	if _, err := raw.Exec(testSchema); err != nil {
		raw.Close()
		t.Fatalf("apply schema: %v", err)
	}
	t.Cleanup(func() { raw.Close() })
	return &DB{db: raw}
}

func mustExec(t *testing.T, db *DB, q string, args ...any) {
	t.Helper()
	if _, err := db.db.Exec(q, args...); err != nil {
		t.Fatalf("exec %q: %v", q, err)
	}
}

func insertInstall(t *testing.T, db *DB, path string) int64 {
	t.Helper()
	res, err := db.db.Exec("INSERT INTO installs(path) VALUES(?)", path)
	if err != nil {
		t.Fatalf("insertInstall: %v", err)
	}
	id, _ := res.LastInsertId()
	return id
}

func insertSession(t *testing.T, db *DB, installID int64, startedAt, endedAt string, totalSecs, activeSecs int) {
	t.Helper()
	if endedAt == "" {
		mustExec(t, db, "INSERT INTO sessions(install_id,started_at) VALUES(?,?)", installID, startedAt)
	} else if totalSecs < 0 {
		mustExec(t, db, "INSERT INTO sessions(install_id,started_at,ended_at) VALUES(?,?,?)", installID, startedAt, endedAt)
	} else {
		mustExec(t, db, "INSERT INTO sessions(install_id,started_at,ended_at,total_secs,active_secs) VALUES(?,?,?,?,?)",
			installID, startedAt, endedAt, totalSecs, activeSecs)
	}
}

func insertArea(t *testing.T, db *DB, code, areaType string) int64 {
	t.Helper()
	res, err := db.db.Exec("INSERT INTO areas(code,display_name,type) VALUES(?,?,?)", code, code, areaType)
	if err != nil {
		t.Fatalf("insertArea: %v", err)
	}
	id, _ := res.LastInsertId()
	return id
}

func insertSpan(t *testing.T, db *DB, sessionID, areaID int64, enteredAt, exitedAt string, durationSecs int) int64 {
	t.Helper()
	var res sql.Result
	var err error
	if exitedAt == "" {
		res, err = db.db.Exec("INSERT INTO area_time_spans(session_id,area_id,entered_at) VALUES(?,?,?)",
			sessionID, areaID, enteredAt)
	} else {
		res, err = db.db.Exec("INSERT INTO area_time_spans(session_id,area_id,entered_at,exited_at,duration_secs) VALUES(?,?,?,?,?)",
			sessionID, areaID, enteredAt, exitedAt, durationSecs)
	}
	if err != nil {
		t.Fatalf("insertSpan: %v", err)
	}
	id, _ := res.LastInsertId()
	return id
}

func insertAfk(t *testing.T, db *DB, sessionID, spanID int64, onAt, offAt string) {
	t.Helper()
	insertAway(t, db, sessionID, spanID, "afk", onAt, offAt)
}

func insertAway(t *testing.T, db *DB, sessionID, spanID int64, kind, onAt, offAt string) {
	t.Helper()
	if offAt == "" {
		mustExec(t, db, "INSERT INTO session_afk(session_id,span_id,kind,afk_on_at) VALUES(?,?,?,?)", sessionID, spanID, kind, onAt)
	} else {
		mustExec(t, db, "INSERT INTO session_afk(session_id,span_id,kind,afk_on_at,afk_off_at) VALUES(?,?,?,?,?)",
			sessionID, spanID, kind, onAt, offAt)
	}
}

// ── FetchZoneTransitions ─────────────────────────────────────────────────────

func TestFetchZoneTransitions_afkFields(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "", -1, -1)
	sid := int64(1)
	areaID := insertArea(t, db, "1_1_town", "Town")

	// Closed span with two closed AFK intervals — afk_secs should be their sum.
	closedSpanID := insertSpan(t, db, sid, areaID, "2024-01-15 10:00:00", "2024-01-15 10:30:00", 1800)
	insertAfk(t, db, sid, closedSpanID, "2024-01-15 10:01:00", "2024-01-15 10:03:00") // 120s
	insertAfk(t, db, sid, closedSpanID, "2024-01-15 10:10:00", "2024-01-15 10:10:30") // 30s

	// Still-open span with a still-open AFK — afk_open_since should surface it,
	// and afk_secs must exclude the open interval's time.
	openSpanID := insertSpan(t, db, sid, areaID, "2024-01-15 10:30:00", "", 0)
	insertAfk(t, db, sid, openSpanID, "2024-01-15 10:35:00", "")

	zones, err := db.FetchZoneTransitions(sid, 0, 0)
	if err != nil {
		t.Fatalf("FetchZoneTransitions: %v", err)
	}
	if len(zones) != 2 {
		t.Fatalf("expected 2 zones, got %d", len(zones))
	}

	// Results are DESC by entered_at, so index 0 is the open span.
	openZone, closedZone := zones[0], zones[1]

	if closedZone.AfkSecs != 150 {
		t.Errorf("closed span AfkSecs = %d, want 150", closedZone.AfkSecs)
	}
	if closedZone.AfkOpenSince != "" {
		t.Errorf("closed span AfkOpenSince = %q, want empty", closedZone.AfkOpenSince)
	}

	if openZone.AfkSecs != 0 {
		t.Errorf("open span AfkSecs = %d, want 0 (excludes the still-open interval)", openZone.AfkSecs)
	}
	if openZone.AfkOpenSince != "2024-01-15 10:35:00" {
		t.Errorf("open span AfkOpenSince = %q, want 2024-01-15 10:35:00", openZone.AfkOpenSince)
	}
}

// TestFetchZoneTransitions_mergesAltTabWithAfk covers that AfkSecs/
// AfkOpenSince fold alt-tab intervals in alongside real AFK ones into one
// number — the game treats alt-tabbing out the same as an AFK timeout for
// activity purposes, so the query never filters by session_afk.kind.
func TestFetchZoneTransitions_mergesAltTabWithAfk(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "", -1, -1)
	sid := int64(1)
	areaID := insertArea(t, db, "1_1_town", "Town")

	closedSpanID := insertSpan(t, db, sid, areaID, "2024-01-15 10:00:00", "2024-01-15 10:30:00", 1800)
	insertAway(t, db, sid, closedSpanID, "afk", "2024-01-15 10:01:00", "2024-01-15 10:03:00")     // 120s
	insertAway(t, db, sid, closedSpanID, "alt_tab", "2024-01-15 10:10:00", "2024-01-15 10:10:30") // 30s

	openSpanID := insertSpan(t, db, sid, areaID, "2024-01-15 10:30:00", "", 0)
	insertAway(t, db, sid, openSpanID, "alt_tab", "2024-01-15 10:35:00", "")

	zones, err := db.FetchZoneTransitions(sid, 0, 0)
	if err != nil {
		t.Fatalf("FetchZoneTransitions: %v", err)
	}
	if len(zones) != 2 {
		t.Fatalf("expected 2 zones, got %d", len(zones))
	}

	openZone, closedZone := zones[0], zones[1]

	if closedZone.AfkSecs != 150 {
		t.Errorf("closed span AfkSecs = %d, want 150 (120 afk + 30 alt_tab, merged)", closedZone.AfkSecs)
	}
	if openZone.AfkOpenSince != "2024-01-15 10:35:00" {
		t.Errorf("open span AfkOpenSince = %q, want 2024-01-15 10:35:00 (an open alt_tab interval)", openZone.AfkOpenSince)
	}
}

// ── FetchSessions ─────────────────────────────────────────────────────────────

func TestFetchSessions_empty(t *testing.T) {
	db := openTestDB(t)
	sessions, err := db.FetchSessions(0, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 0 {
		t.Fatalf("expected 0 sessions, got %d", len(sessions))
	}
}

func TestFetchSessions_singleOpen(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "", -1, -1)

	sessions, err := db.FetchSessions(0, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 1 {
		t.Fatalf("expected 1 session, got %d", len(sessions))
	}
	s := sessions[0]
	if s.StartedAt != "2024-01-15 10:00:00" {
		t.Errorf("StartedAt: got %q, want %q", s.StartedAt, "2024-01-15 10:00:00")
	}
	if s.EndedAt != "" {
		t.Errorf("EndedAt: got %q, want empty", s.EndedAt)
	}
	if s.TotalSecs != -1 {
		t.Errorf("TotalSecs: got %d, want -1", s.TotalSecs)
	}
	if s.ActiveSecs != -1 {
		t.Errorf("ActiveSecs: got %d, want -1", s.ActiveSecs)
	}
	if s.InstallPath != "/game/Client.txt" {
		t.Errorf("InstallPath: got %q, want %q", s.InstallPath, "/game/Client.txt")
	}
}

func TestFetchSessions_afkSecs(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	mustExec(t, db, "INSERT INTO sessions(install_id,started_at,ended_at,total_secs,active_secs,afk_secs) VALUES(?,?,?,?,?,?)",
		iid, "2024-01-15 10:00:00", "2024-01-15 12:00:00", 7200, 6500, 700)

	sessions, err := db.FetchSessions(0, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 1 {
		t.Fatalf("expected 1 session, got %d", len(sessions))
	}
	if got := sessions[0].AfkSecs; got != 700 {
		t.Errorf("AfkSecs: got %d, want 700", got)
	}
}

func TestFetchSessions_singleClosed(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "2024-01-15 12:00:00", 7200, 6500)

	sessions, err := db.FetchSessions(0, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 1 {
		t.Fatalf("expected 1 session, got %d", len(sessions))
	}
	s := sessions[0]
	if s.EndedAt != "2024-01-15 12:00:00" {
		t.Errorf("EndedAt: got %q, want %q", s.EndedAt, "2024-01-15 12:00:00")
	}
	if s.TotalSecs != 7200 {
		t.Errorf("TotalSecs: got %d, want 7200", s.TotalSecs)
	}
	if s.ActiveSecs != 6500 {
		t.Errorf("ActiveSecs: got %d, want 6500", s.ActiveSecs)
	}
}

func TestFetchSessions_chronologicalOrder(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "2024-01-15 11:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-15 14:00:00", "2024-01-15 16:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-16 09:00:00", "", -1, -1)

	sessions, err := db.FetchSessions(0, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 3 {
		t.Fatalf("expected 3 sessions, got %d", len(sessions))
	}
	if sessions[0].StartedAt >= sessions[1].StartedAt || sessions[1].StartedAt >= sessions[2].StartedAt {
		t.Errorf("sessions not in chronological order: %v", []string{
			sessions[0].StartedAt, sessions[1].StartedAt, sessions[2].StartedAt,
		})
	}
	if sessions[0].StartedAt != "2024-01-15 10:00:00" {
		t.Errorf("sessions[0].StartedAt = %q, want 2024-01-15 10:00:00", sessions[0].StartedAt)
	}
	if sessions[2].StartedAt != "2024-01-16 09:00:00" {
		t.Errorf("sessions[2].StartedAt = %q, want 2024-01-16 09:00:00", sessions[2].StartedAt)
	}
}

func TestFetchSessions_limitCaps(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-01 10:00:00", "2024-01-01 11:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-02 10:00:00", "2024-01-02 11:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-03 10:00:00", "2024-01-03 11:00:00", -1, -1)

	// limit=2: fetches 2 newest DESC → [Jan-03, Jan-02], reversed → [Jan-02, Jan-03]
	sessions, err := db.FetchSessions(2, 0)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 2 {
		t.Fatalf("expected 2 sessions, got %d", len(sessions))
	}
	if sessions[0].StartedAt != "2024-01-02 10:00:00" {
		t.Errorf("sessions[0].StartedAt = %q, want 2024-01-02", sessions[0].StartedAt)
	}
	if sessions[1].StartedAt != "2024-01-03 10:00:00" {
		t.Errorf("sessions[1].StartedAt = %q, want 2024-01-03", sessions[1].StartedAt)
	}
}

func TestFetchSessions_offsetSkipsNewest(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-01 10:00:00", "2024-01-01 11:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-02 10:00:00", "2024-01-02 11:00:00", -1, -1)
	insertSession(t, db, iid, "2024-01-03 10:00:00", "2024-01-03 11:00:00", -1, -1)

	// offset=1: skips Jan-03 (newest) → returns [Jan-02, Jan-01] DESC, reversed → [Jan-01, Jan-02]
	sessions, err := db.FetchSessions(0, 1)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(sessions) != 2 {
		t.Fatalf("expected 2 sessions, got %d", len(sessions))
	}
	if sessions[0].StartedAt != "2024-01-01 10:00:00" {
		t.Errorf("sessions[0].StartedAt = %q, want 2024-01-01", sessions[0].StartedAt)
	}
	if sessions[1].StartedAt != "2024-01-02 10:00:00" {
		t.Errorf("sessions[1].StartedAt = %q, want 2024-01-02", sessions[1].StartedAt)
	}
}

// ── FetchSessionEvents ────────────────────────────────────────────────────────

// TestFetchSessionEvents_openSession is a regression test for a real bug:
// the inner UNION ALL subquery selected COALESCE(s.active_secs,-1) and
// COALESCE(s.total_secs,-1) without aliasing them, but the outer SELECT
// referenced them by name (active_secs, total_secs) — SQLite has no column
// by that name on an unaliased expression, so every call errored with "no
// such column: active_secs". This is always hit for the live session (see
// SessionQueryLimits.h: session_event_limit is never 0 there), so the
// current-session detail view errored on every load.
func TestFetchSessionEvents_openSession(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "", -1, -1)

	events, err := db.FetchSessionEvents(10)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(events) != 1 {
		t.Fatalf("expected 1 event, got %d", len(events))
	}
	if events[0].EventType != "start" {
		t.Errorf("EventType = %q, want \"start\"", events[0].EventType)
	}
	if events[0].ActiveSecs != -1 {
		t.Errorf("ActiveSecs = %d, want -1", events[0].ActiveSecs)
	}
	if events[0].TotalSecs != -1 {
		t.Errorf("TotalSecs = %d, want -1", events[0].TotalSecs)
	}
}

func TestFetchSessionEvents_closedSession_includesActiveAndTotalSecs(t *testing.T) {
	db := openTestDB(t)
	iid := insertInstall(t, db, "/game/Client.txt")
	insertSession(t, db, iid, "2024-01-15 10:00:00", "2024-01-15 12:00:00", 7200, 6500)

	events, err := db.FetchSessionEvents(10)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(events) != 2 {
		t.Fatalf("expected 2 events (start+stop), got %d", len(events))
	}
	if events[0].EventType != "start" || events[1].EventType != "stop" {
		t.Fatalf("expected [start, stop] in chronological order, got [%s, %s]",
			events[0].EventType, events[1].EventType)
	}
	for _, ev := range events {
		if ev.TotalSecs != 7200 {
			t.Errorf("%s event TotalSecs = %d, want 7200", ev.EventType, ev.TotalSecs)
		}
		if ev.ActiveSecs != 6500 {
			t.Errorf("%s event ActiveSecs = %d, want 6500", ev.EventType, ev.ActiveSecs)
		}
	}
}

// ── FetchChatDates ────────────────────────────────────────────────────────────

func insertChat(t *testing.T, db *DB, channel, occurredAt string) {
	t.Helper()
	mustExec(t, db, "INSERT INTO chats(channel,message,occurred_at) VALUES(?,?,?)",
		channel, "msg", occurredAt)
}

func insertWhisper(t *testing.T, db *DB, playerName, occurredAt string) {
	t.Helper()
	mustExec(t, db, "INSERT INTO whispers(direction,player_name,message,occurred_at) VALUES(?,?,?,?)",
		"from", playerName, "msg", occurredAt)
}

func TestFetchChatDates_noFilter(t *testing.T) {
	db := openTestDB(t)
	insertChat(t, db, "#", "2024-03-10 12:00:00")
	dates, err := db.FetchChatDates(nil, false)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if dates != nil {
		t.Errorf("expected nil for empty filter, got %v", dates)
	}
}

func TestFetchChatDates_chatOnly(t *testing.T) {
	db := openTestDB(t)
	insertChat(t, db, "#", "2024-03-10 12:00:00")
	insertChat(t, db, "#", "2024-03-10 14:00:00") // same date, should deduplicate
	insertChat(t, db, "#", "2024-03-11 09:00:00")
	insertChat(t, db, "$", "2024-03-12 08:00:00") // different channel, excluded

	dates, err := db.FetchChatDates([]string{"#"}, false)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(dates) != 2 {
		t.Fatalf("expected 2 dates, got %d: %v", len(dates), dates)
	}
	// most-recent first
	if dates[0] != "2024-03-11" {
		t.Errorf("dates[0] = %q, want 2024-03-11", dates[0])
	}
	if dates[1] != "2024-03-10" {
		t.Errorf("dates[1] = %q, want 2024-03-10", dates[1])
	}
}

func TestFetchChatDates_dmsOnly(t *testing.T) {
	db := openTestDB(t)
	insertWhisper(t, db, "Alice", "2024-03-05 10:00:00")
	insertWhisper(t, db, "Bob", "2024-03-07 11:00:00")

	dates, err := db.FetchChatDates(nil, true)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(dates) != 2 {
		t.Fatalf("expected 2 dates, got %d: %v", len(dates), dates)
	}
	if dates[0] != "2024-03-07" {
		t.Errorf("dates[0] = %q, want 2024-03-07", dates[0])
	}
	if dates[1] != "2024-03-05" {
		t.Errorf("dates[1] = %q, want 2024-03-05", dates[1])
	}
}

func TestFetchChatDates_combined(t *testing.T) {
	db := openTestDB(t)
	insertChat(t, db, "#", "2024-03-10 12:00:00")
	insertWhisper(t, db, "Alice", "2024-03-11 10:00:00")
	insertWhisper(t, db, "Bob", "2024-03-10 09:00:00") // same date as chat, should deduplicate

	dates, err := db.FetchChatDates([]string{"#"}, true)
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(dates) != 2 {
		t.Fatalf("expected 2 dates, got %d: %v", len(dates), dates)
	}
	if dates[0] != "2024-03-11" {
		t.Errorf("dates[0] = %q, want 2024-03-11", dates[0])
	}
	if dates[1] != "2024-03-10" {
		t.Errorf("dates[1] = %q, want 2024-03-10", dates[1])
	}
}

// ── FetchWhisperPartnersWithDates ─────────────────────────────────────────────

func TestFetchWhisperPartnersWithDates_empty(t *testing.T) {
	db := openTestDB(t)
	partners, err := db.FetchWhisperPartnersWithDates()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if partners != nil {
		t.Errorf("expected nil for empty table, got %v", partners)
	}
}

func TestFetchWhisperPartnersWithDates_single(t *testing.T) {
	db := openTestDB(t)
	insertWhisper(t, db, "Alice", "2024-03-10 12:00:00")
	insertWhisper(t, db, "Alice", "2024-03-11 09:00:00")
	insertWhisper(t, db, "Alice", "2024-03-10 15:00:00") // same date as first, deduplicates

	partners, err := db.FetchWhisperPartnersWithDates()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(partners) != 1 {
		t.Fatalf("expected 1 partner, got %d", len(partners))
	}
	p := partners[0]
	if p.Name != "Alice" {
		t.Errorf("Name = %q, want Alice", p.Name)
	}
	if len(p.Dates) != 2 {
		t.Fatalf("expected 2 dates, got %d: %v", len(p.Dates), p.Dates)
	}
	// most-recent date first
	if p.Dates[0] != "2024-03-11" {
		t.Errorf("Dates[0] = %q, want 2024-03-11", p.Dates[0])
	}
	if p.Dates[1] != "2024-03-10" {
		t.Errorf("Dates[1] = %q, want 2024-03-10", p.Dates[1])
	}
}

func TestFetchWhisperPartnersWithDates_orderedByRecency(t *testing.T) {
	db := openTestDB(t)
	// Alice messaged first, Bob most recently — Bob should come first.
	insertWhisper(t, db, "Alice", "2024-03-01 10:00:00")
	insertWhisper(t, db, "Bob", "2024-03-15 10:00:00")
	insertWhisper(t, db, "Alice", "2024-03-05 10:00:00")

	partners, err := db.FetchWhisperPartnersWithDates()
	if err != nil {
		t.Fatalf("unexpected error: %v", err)
	}
	if len(partners) != 2 {
		t.Fatalf("expected 2 partners, got %d", len(partners))
	}
	if partners[0].Name != "Bob" {
		t.Errorf("partners[0].Name = %q, want Bob (most recent)", partners[0].Name)
	}
	if partners[1].Name != "Alice" {
		t.Errorf("partners[1].Name = %q, want Alice", partners[1].Name)
	}
	if len(partners[1].Dates) != 2 {
		t.Errorf("Alice should have 2 distinct dates, got %d", len(partners[1].Dates))
	}
}
