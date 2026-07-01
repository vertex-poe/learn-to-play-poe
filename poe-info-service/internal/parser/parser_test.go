package parser

import (
	"testing"

	"github.com/MovingCairn/poe-info-service/internal/proto"
)

func findEvent(events []proto.ParsedEvent, evType string) *proto.ParsedEvent {
	for i := range events {
		if events[i].Type == evType {
			return &events[i]
		}
	}
	return nil
}

func TestParseLine_Played(t *testing.T) {
	p := New()
	events := p.ParseLine(`2024/01/15 10:00:00 123 abc [INFO] Client 456 : You have played for 15 hours, 41 minutes, and 32 seconds.`)
	ev := findEvent(events, proto.EventPlayed)
	if ev == nil {
		t.Fatalf("expected %s event, got %v", proto.EventPlayed, events)
	}
	wantSecs := int64(15*3600 + 41*60 + 32)
	if got := ev.Data["played_secs"]; got != wantSecs {
		t.Errorf("played_secs = %v, want %d", got, wantSecs)
	}
}

func TestParseLine_PassivesSnapshot(t *testing.T) {
	p := New()
	lines := []string{
		`2024/01/15 10:00:00 1 a [INFO] Client 1 : 95 total Passive Skill Points (91 allocated)`,
		`2024/01/15 10:00:00 2 a [INFO] Client 1 : 6 total Ascendancy Skill Points (6 allocated)`,
		`2024/01/15 10:00:00 3 a [INFO] Client 1 : 71 Passive Skill Points from character level`,
		`2024/01/15 10:00:00 4 a [INFO] Client 1 : 24 Passive Skill Points from quests:`,
		`2024/01/15 10:00:00 5 a [INFO] Client 1 : (1 from The Dweller of the Deep)`,
		`2024/01/15 10:00:00 6 a [INFO] Client 1 : (2 from Sever the Right Hand)`,
	}
	for _, l := range lines {
		if events := p.ParseLine(l); len(events) != 0 {
			t.Fatalf("expected no events while block is open, got %v", events)
		}
	}

	// Any non-continuation line closes the block.
	events := p.ParseLine(`2024/01/15 10:00:01 7 a [INFO] Client 1 : AFK mode is now ON.`)
	ev := findEvent(events, proto.EventPassivesSnapshot)
	if ev == nil {
		t.Fatalf("expected %s event on block close, got %v", proto.EventPassivesSnapshot, events)
	}
	if ev.Data["total_points"] != 95 || ev.Data["allocated_points"] != 91 {
		t.Errorf("total/allocated = %v/%v, want 95/91", ev.Data["total_points"], ev.Data["allocated_points"])
	}
	if ev.Data["asc_total"] != 6 || ev.Data["asc_allocated"] != 6 {
		t.Errorf("asc total/allocated = %v/%v, want 6/6", ev.Data["asc_total"], ev.Data["asc_allocated"])
	}
	if ev.Data["level_points"] != 71 {
		t.Errorf("level_points = %v, want 71", ev.Data["level_points"])
	}
	if ev.Data["quest_points"] != 24 {
		t.Errorf("quest_points = %v, want 24", ev.Data["quest_points"])
	}
	quests, ok := ev.Data["quests"].([]map[string]any)
	if !ok || len(quests) != 2 {
		t.Fatalf("quests = %v, want 2 entries", ev.Data["quests"])
	}
	if quests[0]["name"] != "The Dweller of the Deep" || quests[0]["points"] != 1 {
		t.Errorf("quests[0] = %v", quests[0])
	}

	// The AFK line itself should still be processed after the flush.
	if afkEv := findEvent(events, proto.EventAfkOn); afkEv == nil {
		t.Errorf("expected afk_on event alongside passives flush, got %v", events)
	}
}

func TestParseLine_GuildJoinedAndMemberUpdated(t *testing.T) {
	p := New()
	events := p.ParseLine(`2024/01/15 10:00:00 1 a [INFO] Client 1 : Joined guild named Unicorns with 5 members`)
	ev := findEvent(events, proto.EventGuildJoined)
	if ev == nil || ev.Data["guild_name"] != "Unicorns" {
		t.Fatalf("expected guild_joined with guild_name=Unicorns, got %v", events)
	}

	events = p.ParseLine(`2024/01/15 10:00:01 2 a [INFO] Client 1 : Guild member updated KayKay83`)
	ev = findEvent(events, proto.EventGuildMemberUpdated)
	if ev == nil || ev.Data["account_name"] != "KayKay83" {
		t.Fatalf("expected guild_member_updated with account_name=KayKay83, got %v", events)
	}
}

func TestParseLine_ChatChannelJoin(t *testing.T) {
	p := New()
	events := p.ParseLine(`2024/01/15 10:00:00 1 a [INFO] Client 1 : You have joined global chat channel 1,137 English.`)
	ev := findEvent(events, proto.EventChatChannelJoin)
	if ev == nil {
		t.Fatalf("expected chat_channel_join event, got %v", events)
	}
	if ev.Data["number"] != 1137 {
		t.Errorf("number = %v, want 1137", ev.Data["number"])
	}
	if ev.Data["lang"] != "English" {
		t.Errorf("lang = %v, want English", ev.Data["lang"])
	}
}

func TestParseLine_LogFileOpeningFlushesPendingPassives(t *testing.T) {
	p := New()
	p.ParseLine(`2024/01/15 10:00:00 1 a [INFO] Client 1 : 95 total Passive Skill Points (91 allocated)`)

	events := p.ParseLine(`2024/01/15 11:00:00 ***** LOG FILE OPENING *****`)
	if ev := findEvent(events, proto.EventPassivesSnapshot); ev == nil {
		t.Errorf("expected passives_snapshot flush on LOG FILE OPENING, got %v", events)
	}
	if ev := findEvent(events, proto.EventSessionStart); ev == nil {
		t.Errorf("expected session_start event, got %v", events)
	}
}
