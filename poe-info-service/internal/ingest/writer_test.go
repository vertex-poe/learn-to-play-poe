package ingest

import (
	"database/sql"
	"strings"
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/schema"

	_ "modernc.org/sqlite"
)

func newTestDB(t *testing.T) *sql.DB {
	t.Helper()
	name := strings.ReplaceAll(t.Name(), "/", "_")
	db, err := sql.Open("sqlite", "file:"+name+"?mode=memory&cache=shared")
	if err != nil {
		t.Fatalf("open in-memory db: %v", err)
	}
	db.SetMaxOpenConns(1)
	t.Cleanup(func() { db.Close() })
	if err := schema.EnsureSchema(db); err != nil {
		t.Fatalf("EnsureSchema: %v", err)
	}
	return db
}

func newTestWriter(t *testing.T, db *sql.DB) *Writer {
	t.Helper()
	installID, err := EnsureInstall(db, "/game/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	w, err := NewWriter(db, installID)
	if err != nil {
		t.Fatalf("NewWriter: %v", err)
	}
	return w
}

func handle(t *testing.T, w *Writer, evt proto.ParsedEvent) {
	t.Helper()
	if err := w.HandleEvent(evt); err != nil {
		t.Fatalf("HandleEvent(%s): %v", evt.Type, err)
	}
}

func scanString(t *testing.T, db *sql.DB, q string, args ...any) string {
	t.Helper()
	var v string
	if err := db.QueryRow(q, args...).Scan(&v); err != nil {
		t.Fatalf("query %q: %v", q, err)
	}
	return v
}

func scanInt(t *testing.T, db *sql.DB, q string, args ...any) int {
	t.Helper()
	var v int
	if err := db.QueryRow(q, args...).Scan(&v); err != nil {
		t.Fatalf("query %q: %v", q, err)
	}
	return v
}

func TestSessionAndAreaLifecycle(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)

	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})
	if w.sessionID < 0 {
		t.Fatalf("expected session to be created")
	}
	// Char-select span (area_id NULL) opened at session start.
	if got := scanInt(t, db, `SELECT COUNT(*) FROM area_time_spans WHERE session_id=? AND area_id IS NULL`, w.sessionID); got != 1 {
		t.Errorf("expected 1 char-select span, got %d", got)
	}

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventAreaEntered, Timestamp: "2024-01-15 10:05:00",
		Data: map[string]any{"area_name": "Lioneye's Watch", "area_code": "1_1_town", "area_level": 1},
	})
	if w.sessionAreaID < 0 {
		t.Fatalf("expected sessionAreaID to be set")
	}
	// Previous (char-select) span should now be closed.
	if got := scanInt(t, db, `SELECT COUNT(*) FROM area_time_spans WHERE session_id=? AND area_id IS NULL AND exited_at IS NOT NULL`, w.sessionID); got != 1 {
		t.Errorf("expected char-select span to be closed, got %d closed rows", got)
	}
	if got := scanInt(t, db, `SELECT COUNT(*) FROM area_moves WHERE install_id=?`, w.installID); got != 1 {
		t.Errorf("expected 1 area_moves row, got %d", got)
	}

	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 12:00:00"})
	firstSessionID := w.sessionID - 1 // the session just closed
	endedAt := scanString(t, db, `SELECT ended_at FROM sessions WHERE id=?`, firstSessionID)
	if endedAt != "2024-01-15 10:05:00" {
		t.Errorf("closeSession endTs = %q, want the last processed event's timestamp", endedAt)
	}
}

func TestLevelUpAndPlayedBackfill(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	// /played arrives before the character is known (char select span).
	handle(t, w, proto.ParsedEvent{Type: proto.EventPlayed, Timestamp: "2024-01-15 10:01:00", Data: map[string]any{"played_secs": int64(3661)}})

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventLevelUp, Timestamp: "2024-01-15 10:02:00",
		Data: map[string]any{"character": "Xylia", "char_class": "Witch", "level": 2},
	})
	if w.sessionCharID < 0 {
		t.Fatalf("expected sessionCharID to be set after level up")
	}
	playedSecs := scanInt(t, db, `SELECT played_secs FROM characters WHERE id=?`, w.sessionCharID)
	if playedSecs != 3661 {
		t.Errorf("played_secs = %d, want 3661 (backfilled from span)", playedSecs)
	}

	level := scanInt(t, db, `SELECT level FROM character_level_events WHERE char_id=?`, w.sessionCharID)
	if level != 2 {
		t.Errorf("character_level_events.level = %d, want 2", level)
	}
	eventType := scanString(t, db, `SELECT event_type FROM events WHERE event_type='level_up'`)
	if eventType != "level_up" {
		t.Errorf("expected audit events row for level_up")
	}

	// A second /played now updates played_secs directly (char already known).
	handle(t, w, proto.ParsedEvent{Type: proto.EventPlayed, Timestamp: "2024-01-15 10:10:00", Data: map[string]any{"played_secs": int64(7200)}})
	playedSecs = scanInt(t, db, `SELECT played_secs FROM characters WHERE id=?`, w.sessionCharID)
	if playedSecs != 7200 {
		t.Errorf("played_secs = %d, want 7200 after second /played", playedSecs)
	}
}

func TestAfkAndAltTab(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	handle(t, w, proto.ParsedEvent{Type: proto.EventAfkOn, Timestamp: "2024-01-15 10:01:00"})
	handle(t, w, proto.ParsedEvent{Type: proto.EventAfkOff, Timestamp: "2024-01-15 10:03:00"})
	dur := scanInt(t, db, `SELECT CAST((julianday(afk_off_at)-julianday(afk_on_at))*86400 AS INTEGER) FROM session_afk WHERE session_id=?`, w.sessionID)
	if dur != 120 {
		t.Errorf("afk duration = %d, want 120", dur)
	}
	if w.sessionAfkSecs != 120 {
		t.Errorf("sessionAfkSecs = %d, want 120", w.sessionAfkSecs)
	}

	handle(t, w, proto.ParsedEvent{Type: proto.EventAltTabOut, Timestamp: "2024-01-15 10:05:00"})
	handle(t, w, proto.ParsedEvent{Type: proto.EventAltTabBack, Timestamp: "2024-01-15 10:06:00"})
	inAt := scanString(t, db, `SELECT in_at FROM session_alt_tabs WHERE session_id=?`, w.sessionID)
	if inAt != "2024-01-15 10:06:00" {
		t.Errorf("alt-tab in_at = %q, want 2024-01-15 10:06:00", inAt)
	}
}

func TestChatWhisperAndDeath(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventChat, Timestamp: "2024-01-15 10:01:00",
		Data: map[string]any{"channel": "#", "player": "Bob", "message": "hi", "guild_tag": "ABC"},
	})
	channel := scanString(t, db, `SELECT channel FROM chats WHERE message='hi'`)
	if channel != "#" {
		t.Errorf("chat channel = %q, want #", channel)
	}
	guildID := scanInt(t, db, `SELECT guild_id FROM public_chars WHERE name='Bob'`)
	if guildID <= 0 {
		t.Errorf("expected Bob's guild_id to be set from guild_tag")
	}

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventWhisper, Timestamp: "2024-01-15 10:02:00",
		Data: map[string]any{"direction": "from", "player": "Alice", "message": "hey"},
	})
	if got := scanInt(t, db, `SELECT COUNT(*) FROM whispers WHERE player_name='Alice'`); got != 1 {
		t.Errorf("expected 1 whisper from Alice, got %d", got)
	}

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventLevelUp, Timestamp: "2024-01-15 10:03:00",
		Data: map[string]any{"character": "Xylia", "char_class": "Witch", "level": 5},
	})
	handle(t, w, proto.ParsedEvent{Type: proto.EventCharacterDeath, Timestamp: "2024-01-15 10:04:00", Data: map[string]any{"character": "Xylia"}})
	deathLevel := scanInt(t, db, `SELECT level FROM character_deaths WHERE char_id=?`, w.sessionCharID)
	if deathLevel != 5 {
		t.Errorf("character_deaths.level = %d, want 5 (current char's level)", deathLevel)
	}
}

func TestAchievementHideoutAndPvp(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	handle(t, w, proto.ParsedEvent{Type: proto.EventAchievement, Timestamp: "2024-01-15 10:01:00", Data: map[string]any{"name": "AllOptionalDialogue"}})
	if got := scanInt(t, db, `SELECT COUNT(*) FROM achievement_events`); got != 1 {
		t.Errorf("expected 1 achievement event, got %d", got)
	}

	handle(t, w, proto.ParsedEvent{Type: proto.EventHideoutDiscovered, Timestamp: "2024-01-15 10:02:00", Data: map[string]any{"name": "Tidal Island Hideout"}})
	if got := scanInt(t, db, `SELECT COUNT(*) FROM hideout_discovered_events`); got != 1 {
		t.Errorf("expected 1 hideout discovery, got %d", got)
	}

	handle(t, w, proto.ParsedEvent{Type: proto.EventPvpQueue, Timestamp: "2024-01-15 10:03:00", Data: map[string]any{"match_name": "CTF Open", "other_players": 3}})
	if w.lastPvpQueueEventID < 0 {
		t.Fatalf("expected lastPvpQueueEventID to be set")
	}
	handle(t, w, proto.ParsedEvent{Type: proto.EventPvpQueueCancelled, Timestamp: "2024-01-15 10:04:00"})
	cancelledAt := scanString(t, db, `SELECT cancelled_at FROM pvp_queue_events LIMIT 1`)
	if cancelledAt != "2024-01-15 10:04:00" {
		t.Errorf("cancelled_at = %q, want 2024-01-15 10:04:00", cancelledAt)
	}
	if w.lastPvpQueueEventID != -1 {
		t.Errorf("expected lastPvpQueueEventID reset after cancel")
	}
}

func TestPassiveAllocation(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventPassiveAllocated, Timestamp: "2024-01-15 10:01:00",
		Data: map[string]any{"skill_id": "accuracy581", "skill_name": "Projectile Damage", "is_mastery": false},
	})
	action := scanString(t, db, `SELECT action FROM passive_skill_allocations`)
	if action != "allocated" {
		t.Errorf("action = %q, want allocated", action)
	}

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventPassiveUnallocated, Timestamp: "2024-01-15 10:02:00",
		Data: map[string]any{"skill_id": "accuracy581", "skill_name": "Projectile Damage", "is_mastery": false},
	})
	if got := scanInt(t, db, `SELECT COUNT(*) FROM passive_skill_allocations`); got != 2 {
		t.Errorf("expected 2 allocation rows (alloc + unalloc), got %d", got)
	}
}

func TestGuildJoinAndMemberUpdate(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)

	handle(t, w, proto.ParsedEvent{Type: proto.EventGuildJoined, Timestamp: "2024-01-15 10:00:00", Data: map[string]any{"guild_name": "Unicorns"}})
	guildName := scanString(t, db, `SELECT guild_name FROM accounts WHERE name='unknown'`)
	if guildName != "Unicorns" {
		t.Errorf("accounts.guild_name = %q, want Unicorns", guildName)
	}

	handle(t, w, proto.ParsedEvent{Type: proto.EventGuildMemberUpdated, Timestamp: "2024-01-15 10:01:00", Data: map[string]any{"account_name": "KayKay83"}})
	if got := scanInt(t, db, `SELECT COUNT(*) FROM guild_members gm JOIN accounts a ON a.id=gm.account_id WHERE gm.guild_name='Unicorns' AND a.name='KayKay83'`); got != 1 {
		t.Errorf("expected guild_members row linking Unicorns/KayKay83, got %d", got)
	}
}

func TestChatChannelJoinTracksNumberAndLang(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)

	// Labels are registered independently via channels.register/.rename/.delete
	// (see internal/channels) — a chat_channel_join only records the channel
	// number and language.
	handle(t, w, proto.ParsedEvent{Type: proto.EventChatChannelJoin, Timestamp: "2024-01-15 10:00:00", Data: map[string]any{"number": 7, "lang": "English"}})
	lang := scanString(t, db, `SELECT lang FROM chat_channels WHERE number=7`)
	if lang != "English" {
		t.Errorf("chat_channels.lang = %q, want English", lang)
	}
	if got := scanInt(t, db, `SELECT COUNT(*) FROM chat_channel_joins`); got != 1 {
		t.Errorf("expected 1 chat_channel_joins row, got %d", got)
	}
}

func TestPassivesSnapshotWithQuests(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	handle(t, w, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})

	handle(t, w, proto.ParsedEvent{
		Type: proto.EventPassivesSnapshot, Timestamp: "2024-01-15 10:01:00",
		Data: map[string]any{
			"total_points": 95, "allocated_points": 91,
			"asc_total": 6, "asc_allocated": 6,
			"level_points": 71, "quest_points": 24,
			"quests": []map[string]any{
				{"name": "The Dweller of the Deep", "points": 1},
				{"name": "Sever the Right Hand", "points": 2},
			},
		},
	})

	total := scanInt(t, db, `SELECT total_points FROM passive_point_snapshots WHERE session_id=?`, w.sessionID)
	if total != 95 {
		t.Errorf("total_points = %d, want 95", total)
	}
	if got := scanInt(t, db, `SELECT COUNT(*) FROM passive_snapshot_quests`); got != 2 {
		t.Errorf("expected 2 passive_snapshot_quests rows, got %d", got)
	}
	points := scanInt(t, db, `SELECT psq.points FROM passive_snapshot_quests psq JOIN passive_quest_sources pqs ON pqs.id=psq.quest_id WHERE pqs.name='Sever the Right Hand'`)
	if points != 2 {
		t.Errorf("points for 'Sever the Right Hand' = %d, want 2", points)
	}
}

func TestClientScreenEvents(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)

	handle(t, w, proto.ParsedEvent{Type: proto.EventLoginScreen, Timestamp: "2024-01-15 10:00:00"})
	handle(t, w, proto.ParsedEvent{Type: proto.EventCharSelect, Timestamp: "2024-01-15 10:00:05"})

	loginCount := scanInt(t, db, `SELECT COUNT(*) FROM client_screen_events WHERE event_type='login_screen'`)
	charSelectCount := scanInt(t, db, `SELECT COUNT(*) FROM client_screen_events WHERE event_type='char_select'`)
	if loginCount != 1 || charSelectCount != 1 {
		t.Errorf("expected 1 login_screen + 1 char_select row, got %d/%d", loginCount, charSelectCount)
	}
}

func TestRestartRecoversOpenSessionAndSpan(t *testing.T) {
	db := newTestDB(t)
	w1 := newTestWriter(t, db)
	handle(t, w1, proto.ParsedEvent{Type: proto.EventSessionStart, Timestamp: "2024-01-15 10:00:00"})
	handle(t, w1, proto.ParsedEvent{
		Type: proto.EventAreaEntered, Timestamp: "2024-01-15 10:05:00",
		Data: map[string]any{"area_name": "Lioneye's Watch", "area_code": "1_1_town", "area_level": 1},
	})

	// Simulate a service restart: a brand new Writer for the same install
	// must recover the still-open session and span rather than losing them.
	installID, err := EnsureInstall(db, "/game/Client.txt")
	if err != nil {
		t.Fatalf("EnsureInstall: %v", err)
	}
	w2, err := NewWriter(db, installID)
	if err != nil {
		t.Fatalf("NewWriter: %v", err)
	}
	if w2.sessionID != w1.sessionID {
		t.Errorf("recovered sessionID = %d, want %d", w2.sessionID, w1.sessionID)
	}
	if w2.currentSpanID != w1.currentSpanID {
		t.Errorf("recovered currentSpanID = %d, want %d", w2.currentSpanID, w1.currentSpanID)
	}
}
