package ingest

import (
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/parser"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

// TestFullSessionFixture_IngestsWithoutError runs the same fixture
// internal/parser uses through the full tailer-free pipeline (parser.New +
// Writer.HandleEvent, mirroring the loop in poe-info-service's `log ingest`
// CLI verb — see runLogIngest in cli.go) against a real schema'd sqlite db.
// It exists because neither layer had a test that ingested a realistic
// Client.txt end to end: parser tests never wrote to a database, and writer
// tests never parsed a raw line.
func TestFullSessionFixture_IngestsWithoutError(t *testing.T) {
	db := newTestDB(t)
	w := newTestWriter(t, db)
	p := parser.New()

	for _, line := range testfixtures.SampleSessionLines() {
		for _, evt := range p.ParseLine(line) {
			if err := w.HandleEvent(evt); err != nil {
				t.Fatalf("HandleEvent(%s) for line %q: %v", evt.Type, line, err)
			}
		}
	}

	// Sanity-check that every event type actually reached the database, not
	// just that HandleEvent returned nil (a silently-ignored event type in
	// the switch in writer.go would still "succeed" with err == nil).
	checks := []struct {
		desc  string
		query string
	}{
		{"session", `SELECT COUNT(*) FROM sessions`},
		{"area move", `SELECT COUNT(*) FROM area_moves`},
		{"character level event", `SELECT COUNT(*) FROM character_level_events`},
		{"character death", `SELECT COUNT(*) FROM character_deaths`},
		{"session_afk row", `SELECT COUNT(*) FROM session_afk`},
		{"session alt-tab", `SELECT COUNT(*) FROM session_alt_tabs WHERE in_at IS NOT NULL`},
		{"whisper", `SELECT COUNT(*) FROM whispers`},
		{"chat", `SELECT COUNT(*) FROM chats`},
		{"achievement event", `SELECT COUNT(*) FROM achievement_events`},
		{"hideout discovery", `SELECT COUNT(*) FROM hideout_discovered_events`},
		{"pvp queue event (cancelled)", `SELECT COUNT(*) FROM pvp_queue_events WHERE cancelled_at IS NOT NULL`},
		{"passive skill allocation", `SELECT COUNT(*) FROM passive_skill_allocations`},
		{"quest event", `SELECT COUNT(*) FROM quest_events`},
		{"general event", `SELECT COUNT(*) FROM general_events`},
		{"ruleset failed event", `SELECT COUNT(*) FROM zone_ruleset_failed_events`},
		{"played event", `SELECT COUNT(*) FROM character_played_events`},
		{"guild account", `SELECT COUNT(*) FROM accounts WHERE guild_name = 'Unicorns'`},
		{"guild member", `SELECT COUNT(*) FROM guild_members`},
		{"chat channel join", `SELECT COUNT(*) FROM chat_channel_joins`},
		{"passive point snapshot", `SELECT COUNT(*) FROM passive_point_snapshots`},
		{"passive snapshot quest", `SELECT COUNT(*) FROM passive_snapshot_quests`},
		{"client screen event (login+char select)", `SELECT COUNT(*) FROM client_screen_events`},
	}
	for _, c := range checks {
		if got := scanInt(t, db, c.query); got < 1 {
			t.Errorf("%s: expected at least 1 row after ingesting the fixture, got %d (query: %s)", c.desc, got, c.query)
		}
	}

	if got := scanInt(t, db, `SELECT COUNT(*) FROM passive_skill_allocations`); got != 3 {
		t.Errorf("passive_skill_allocations count = %d, want 3 (alloc + unalloc + mastery alloc)", got)
	}
	if got := scanInt(t, db, `SELECT COUNT(*) FROM client_screen_events`); got != 2 {
		t.Errorf("client_screen_events count = %d, want 2 (login_screen + char_select)", got)
	}
}
