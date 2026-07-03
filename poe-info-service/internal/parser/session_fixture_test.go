package parser

import (
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/proto"
	"github.com/MovingCairn/poe-info-service/internal/testfixtures"
)

func findAllEvents(events []proto.ParsedEvent, evType string) []proto.ParsedEvent {
	var out []proto.ParsedEvent
	for _, e := range events {
		if e.Type == evType {
			out = append(out, e)
		}
	}
	return out
}

// TestParseLine_FullSessionFixture feeds a realistic Client.txt excerpt
// (testfixtures.SampleSession) through a single Parser from LOG FILE OPENING
// to EOF and checks two things at once: that every event type parser.Parser
// can produce (proto.Event*) is actually reachable by parsing real log-line
// syntax — not just by hand-building a proto.ParsedEvent — and that the
// regex capture groups extract the fields callers rely on.
func TestParseLine_FullSessionFixture(t *testing.T) {
	p := New()
	var events []proto.ParsedEvent
	for _, line := range testfixtures.SampleSessionLines() {
		events = append(events, p.ParseLine(line)...)
	}

	allEventTypes := []string{
		proto.EventAreaEntered, proto.EventLevelUp, proto.EventCharacterDeath,
		proto.EventAfkOn, proto.EventAfkOff, proto.EventWhisper, proto.EventChat,
		proto.EventAchievement, proto.EventHideoutDiscovered, proto.EventPvpQueue,
		proto.EventPvpQueueCancelled, proto.EventPassiveAllocated, proto.EventPassiveUnallocated,
		proto.EventQuestEvent, proto.EventGeneralEvent, proto.EventSessionStart,
		proto.EventLoginScreen, proto.EventCharSelect, proto.EventAltTabOut, proto.EventAltTabBack,
		proto.EventPlayed, proto.EventPassivesSnapshot, proto.EventGuildJoined,
		proto.EventGuildMemberUpdated, proto.EventChatChannelJoin,
	}
	for _, evType := range allEventTypes {
		if findEvent(events, evType) == nil {
			t.Errorf("fixture never produced a %s event — either the fixture or the parser regressed", evType)
		}
	}

	// Spot-check capture groups for the types that had no prior line-level
	// regex coverage at all (previously verified only via hand-built structs
	// at the writer layer).
	if ev := findEvent(events, proto.EventLevelUp); ev != nil {
		if ev.Data["character"] != "Xylia" || ev.Data["char_class"] != "Witch" || ev.Data["level"] != 2 {
			t.Errorf("level_up data = %v, want character=Xylia char_class=Witch level=2", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventAreaEntered); ev != nil {
		if ev.Data["area_name"] != "Lioneye's Watch" || ev.Data["area_code"] != "1_1_town" || ev.Data["area_level"] != 1 {
			t.Errorf("area_entered data = %v, want area_name=Lioneye's Watch area_code=1_1_town area_level=1", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventCharacterDeath); ev != nil {
		if ev.Data["character"] != "Xylia" {
			t.Errorf("character_death data = %v, want character=Xylia", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventWhisper); ev != nil {
		if ev.Data["direction"] != "from" || ev.Data["player"] != "Alice" || ev.Data["message"] != "hey there" {
			t.Errorf("whisper data = %v, want direction=from player=Alice message='hey there'", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventChat); ev != nil {
		if ev.Data["channel"] != "#" || ev.Data["player"] != "Bob" || ev.Data["message"] != "hi all" {
			t.Errorf("chat data = %v, want channel=# player=Bob message='hi all'", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventAchievement); ev != nil {
		if ev.Data["name"] != "AllOptionalDialogue" {
			t.Errorf("achievement data = %v, want name=AllOptionalDialogue", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventHideoutDiscovered); ev != nil {
		if ev.Data["name"] != "Tidal Island Hideout" {
			t.Errorf("hideout_discovered data = %v, want name='Tidal Island Hideout'", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventPvpQueue); ev != nil {
		if ev.Data["match_name"] != "CTF Open" || ev.Data["other_players"] != 3 {
			t.Errorf("pvp_queue data = %v, want match_name='CTF Open' other_players=3", ev.Data)
		}
	}

	passiveAllocs := findAllEvents(events, proto.EventPassiveAllocated)
	if len(passiveAllocs) != 2 {
		t.Fatalf("expected 2 passive_allocated events (regular + mastery), got %d: %v", len(passiveAllocs), passiveAllocs)
	}
	if passiveAllocs[0].Data["is_mastery"] != false || passiveAllocs[0].Data["skill_id"] != "accuracy581" {
		t.Errorf("regular passive_allocated data = %v", passiveAllocs[0].Data)
	}
	if passiveAllocs[1].Data["is_mastery"] != true || passiveAllocs[1].Data["skill_name"] != "Culling Strike Mastery" {
		t.Errorf("mastery passive_allocated data = %v", passiveAllocs[1].Data)
	}
	if unallocs := findAllEvents(events, proto.EventPassiveUnallocated); len(unallocs) != 1 || unallocs[0].Data["skill_id"] != "accuracy581" {
		t.Errorf("expected 1 passive_unallocated event for accuracy581, got %v", unallocs)
	}

	wantQuestSubtypes := map[string]bool{
		"monsters_cleared": false, "passive_skill_point_received": false,
		"passive_skill_points_received": false, "passive_respec_received": false,
		"kitava_resistance_penalty": false, "labyrinth_craft_options_received": false,
		"Squawk": false,
	}
	questEvents := findAllEvents(events, proto.EventQuestEvent)
	for _, ev := range questEvents {
		if et, ok := ev.Data["event_type"].(string); ok {
			wantQuestSubtypes[et] = true
		}
	}
	for subtype, seen := range wantQuestSubtypes {
		if !seen {
			t.Errorf("expected a quest_event with event_type=%s, never saw one (got %d quest_events total)", subtype, len(questEvents))
		}
	}

	wantGeneralSubtypes := map[string]bool{"patch_required": false, "ruleset_failed": false}
	for _, ev := range findAllEvents(events, proto.EventGeneralEvent) {
		if et, ok := ev.Data["event_type"].(string); ok {
			wantGeneralSubtypes[et] = true
			if et == "ruleset_failed" && ev.Data["ruleset"] != "HardcoreRuleset" {
				t.Errorf("ruleset_failed data = %v, want ruleset=HardcoreRuleset", ev.Data)
			}
		}
	}
	for subtype, seen := range wantGeneralSubtypes {
		if !seen {
			t.Errorf("expected a general_event with event_type=%s, never saw one", subtype)
		}
	}

	if ev := findEvent(events, proto.EventGuildJoined); ev != nil && ev.Data["guild_name"] != "Unicorns" {
		t.Errorf("guild_joined data = %v, want guild_name=Unicorns", ev.Data)
	}
	if ev := findEvent(events, proto.EventGuildMemberUpdated); ev != nil && ev.Data["account_name"] != "KayKay83" {
		t.Errorf("guild_member_updated data = %v, want account_name=KayKay83", ev.Data)
	}
	if ev := findEvent(events, proto.EventChatChannelJoin); ev != nil {
		if ev.Data["number"] != 1137 || ev.Data["lang"] != "English" {
			t.Errorf("chat_channel_join data = %v, want number=1137 lang=English", ev.Data)
		}
	}
	if ev := findEvent(events, proto.EventPlayed); ev != nil {
		wantSecs := int64(15*3600 + 41*60 + 32)
		if ev.Data["played_secs"] != wantSecs {
			t.Errorf("played data = %v, want played_secs=%d", ev.Data, wantSecs)
		}
	}
}
