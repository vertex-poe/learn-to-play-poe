package parser

import (
	"regexp"
	"strconv"
	"strings"
	"time"

	"github.com/MovingCairn/poe-info-service/internal/proto"
)

var (
	lineRE           = regexp.MustCompile(`^(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}) \d+ [0-9a-f]+ \[(\w+)[^\]]*\](?: \[(\w+)\])? ?(.*)`)
	logOpenRE        = regexp.MustCompile(`^(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2})`)
	generatingRE     = regexp.MustCompile(`Generating level (\d+) area "([^"]+)"`)
	enteredRE        = regexp.MustCompile(`You have entered (.+?)\.`)
	sceneSourceRE    = regexp.MustCompile(`Set Source \[([^\]]+)\]`)
	guildRE          = regexp.MustCompile(`Joined guild named (.+?) with \d+ members`)
	guildDetailsRE   = regexp.MustCompile(`Guild details changed (.+)`)
	guildMemberRE    = regexp.MustCompile(`Guild member updated (\S+)`)
	chatChannelRE    = regexp.MustCompile(`You have joined global chat channel ([\d,]+) (\w+)`)
	levelUpRE        = regexp.MustCompile(`(\S+) \((\w+)\) is now level (\d+)`)
	afkRE            = regexp.MustCompile(`AFK mode is now (ON|OFF)`)
	whisperRE        = regexp.MustCompile(`@(From|To) (?:<([^>]*)> )?(\S+): (.*)`)
	passiveAllocRE   = regexp.MustCompile(`Successfully (allocated|unallocated) passive skill id: ([^,]+), name: (.+)`)
	masteryAllocRE   = regexp.MustCompile(`Successfully (allocated|unallocated) mastery effect id: ([^,]+), mastery: [^,]+, name: (.+)`)
	deathRE          = regexp.MustCompile(`(\S+) has been slain\.`)
	chatRE           = regexp.MustCompile(`([#$%&])(?:<([^>]*)> )?(\S+): (.*)`)
	playedRE         = regexp.MustCompile(`You have played for .+?\.`)
	// "Achivement" is an intentional typo matching the actual log output
	achievementRE    = regexp.MustCompile(`Achivement stored: (\S+)`)
	hideoutRE        = regexp.MustCompile(`Spawning discoverable Hideout (.+)`)
	pvpQueueRE       = regexp.MustCompile(`Queueing for PVP match "([^"]+)" with (\d+) other players`)
	passivesTotalRE  = regexp.MustCompile(`(\d+) total Passive Skill Points \((\d+) allocated\)`)
	passivesAscRE    = regexp.MustCompile(`(\d+) total Ascendancy Skill Points \((\d+) allocated\)`)
	passivesLevelRE  = regexp.MustCompile(`(\d+) Passive Skill Points from character level`)
	passivesQuestTotalRE = regexp.MustCompile(`(\d+) Passive Skill Points from quests:`)
	passivesQuestEntryRE = regexp.MustCompile(`\((\d+) from (.+)\)`)
	triggerFollowupRE    = regexp.MustCompile(`[\w ]+[=:] ?\d+`)
	rulesetFailedRE      = regexp.MustCompile(`Failed to create ruleset \d+ \(([^)]+)\)`)
	talkingPetRE         = regexp.MustCompile(`TalkingPetAudioEvent '([^']+)'`)
)

type locState int

const (
	locUnknown               locState = iota
	locLoginScreen
	locConnectingFromLogin
	locConnectingFromZone
	locAwaitingScene
	locCharSelect
	locInZone
)

type passivesBlock struct {
	ts             string
	totalPoints    int
	allocatedPoints int
	ascTotal       int
	ascAllocated   int
	levelPoints    int
	questPoints    int
}

type Parser struct {
	loc                  locState
	pendingCode          string
	pendingLevel         int
	skipTriggerFollowup  bool
	pendingPassives      *passivesBlock
	altTabOutTs          string
	afkOnTs              string
}

func New() *Parser {
	return &Parser{}
}

func normalizeTs(ts string) string {
	b := []byte(ts)
	if len(b) >= 10 {
		b[4] = '-'
		b[7] = '-'
	}
	return string(b)
}

func tsToSecs(ts string) int64 {
	t, err := time.Parse("2006-01-02 15:04:05", ts)
	if err != nil {
		return 0
	}
	return t.Unix()
}

func (p *Parser) clearAltTab(ts string) *proto.ParsedEvent {
	if p.altTabOutTs == "" {
		return nil
	}
	outSecs := tsToSecs(p.altTabOutTs)
	nowSecs := tsToSecs(ts)
	dur := nowSecs - outSecs
	if dur < 0 {
		dur = 0
	}
	p.altTabOutTs = ""
	return &proto.ParsedEvent{
		Type:      proto.EventAltTabBack,
		Timestamp: ts,
		Data:      map[string]any{"duration_secs": dur},
	}
}

func (p *Parser) ParseLine(line string) []proto.ParsedEvent {
	// LOG FILE OPENING — checked before the header regex
	if strings.Contains(line, "LOG FILE OPENING") {
		p.loc = locUnknown
		p.pendingCode = ""
		p.pendingLevel = 0
		p.skipTriggerFollowup = false
		p.pendingPassives = nil
		p.altTabOutTs = ""
		p.afkOnTs = ""
		return []proto.ParsedEvent{{Type: proto.EventSessionStart, Timestamp: normalizeTs(logOpenRE.FindString(line))}}
	}

	m := lineRE.FindStringSubmatch(line)
	if m == nil {
		return nil
	}

	ts := normalizeTs(m[1])
	level := m[2]
	tag := m[3]
	msg := m[4]

	// Noise filter
	if p.skipTriggerFollowup {
		if triggerFollowupRE.MatchString(msg) {
			return nil
		}
		p.skipTriggerFollowup = false
	}
	if strings.HasPrefix(msg, "Client couldn't execute a triggered action") ||
		strings.HasPrefix(msg, "Instant/Triggered action") {
		p.skipTriggerFollowup = true
		return nil
	}

	var events []proto.ParsedEvent

	// DEBUG level
	if level == "DEBUG" {
		if gm := generatingRE.FindStringSubmatch(msg); gm != nil {
			p.pendingLevel, _ = strconv.Atoi(gm[1])
			p.pendingCode = gm[2]
		}
		return nil
	}

	if level != "INFO" {
		return nil
	}

	// INFO level processing

	// a. Passives multi-line block
	if p.pendingPassives != nil {
		if passivesAscRE.MatchString(msg) || passivesLevelRE.MatchString(msg) ||
			passivesQuestTotalRE.MatchString(msg) || passivesQuestEntryRE.MatchString(msg) {
			return nil
		}
		p.pendingPassives = nil
		// fall through
	}
	if pm := passivesTotalRE.FindStringSubmatch(msg); pm != nil {
		total, _ := strconv.Atoi(pm[1])
		allocated, _ := strconv.Atoi(pm[2])
		p.pendingPassives = &passivesBlock{
			ts:              ts,
			totalPoints:     total,
			allocatedPoints: allocated,
		}
		return nil
	}

	// b. [WINDOW] bracket
	if tag == "WINDOW" {
		if strings.Contains(msg, "Lost focus") {
			if p.altTabOutTs == "" {
				p.altTabOutTs = ts
				events = append(events, proto.ParsedEvent{Type: proto.EventAltTabOut, Timestamp: ts})
			}
		} else if strings.Contains(msg, "Gained focus") {
			if ev := p.clearAltTab(ts); ev != nil {
				events = append(events, *ev)
			}
		}
		return events
	}

	// c. Guild (no event, consume)
	if guildRE.MatchString(msg) || guildDetailsRE.MatchString(msg) || guildMemberRE.MatchString(msg) {
		return nil
	}

	// d. Chat channel join (no event, consume)
	if chatChannelRE.MatchString(msg) {
		return nil
	}

	// e. Level up
	if lm := levelUpRE.FindStringSubmatch(msg); lm != nil {
		lvl, _ := strconv.Atoi(lm[3])
		events = append(events, proto.ParsedEvent{
			Type:      proto.EventLevelUp,
			Timestamp: ts,
			Data:      map[string]any{"character": lm[1], "char_class": lm[2], "level": lvl},
		})
	}

	// f. AFK
	if am := afkRE.FindStringSubmatch(msg); am != nil {
		if am[1] == "ON" {
			p.afkOnTs = ts
			events = append(events, proto.ParsedEvent{Type: proto.EventAfkOn, Timestamp: ts})
		} else {
			var dur int64
			if p.afkOnTs != "" {
				dur = tsToSecs(ts) - tsToSecs(p.afkOnTs)
				if dur < 0 {
					dur = 0
				}
				p.afkOnTs = ""
			}
			events = append(events, proto.ParsedEvent{
				Type:      proto.EventAfkOff,
				Timestamp: ts,
				Data:      map[string]any{"duration_secs": dur},
			})
		}
	}

	// g. Quest events
	if strings.Contains(msg, "0 monsters remain.") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "monsters_cleared"}})
	}
	if strings.Contains(msg, "You have received a Passive Skill Point.") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "passive_skill_point_received"}})
	}
	if strings.Contains(msg, "Passive Skill Points.") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "passive_skill_points_received"}})
	}
	if strings.Contains(msg, "Passive Respec Points") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "passive_respec_received"}})
	}
	if strings.Contains(msg, "Kitava's merciless affliction") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "kitava_resistance_penalty"}})
	}
	if strings.Contains(msg, "InstanceClientLabyrinthCraftResultOptionsList recieved") {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": "labyrinth_craft_options_received"}})
	}

	// h. Patch required
	if strings.Contains(msg, "There has been a patch that you need to update to.") {
		events = append(events, proto.ParsedEvent{Type: proto.EventGeneralEvent, Timestamp: ts, Data: map[string]any{"event_type": "patch_required"}})
	}

	// i. Ruleset failed
	if rm := rulesetFailedRE.FindStringSubmatch(msg); rm != nil {
		events = append(events, proto.ParsedEvent{Type: proto.EventGeneralEvent, Timestamp: ts, Data: map[string]any{"event_type": "ruleset_failed", "ruleset": rm[1]}})
	}

	// j. Talking pet
	if tm := talkingPetRE.FindStringSubmatch(msg); tm != nil {
		events = append(events, proto.ParsedEvent{Type: proto.EventQuestEvent, Timestamp: ts, Data: map[string]any{"event_type": tm[1]}})
	}

	// k. /played (consume, no event)
	if playedRE.MatchString(msg) {
		return events
	}

	// l. Achievement
	if acm := achievementRE.FindStringSubmatch(msg); acm != nil {
		events = append(events, proto.ParsedEvent{Type: proto.EventAchievement, Timestamp: ts, Data: map[string]any{"name": acm[1]}})
	}

	// m. Hideout
	if hm := hideoutRE.FindStringSubmatch(msg); hm != nil {
		events = append(events, proto.ParsedEvent{Type: proto.EventHideoutDiscovered, Timestamp: ts, Data: map[string]any{"name": strings.TrimSpace(hm[1])}})
	}

	// n. PVP queue
	if pqm := pvpQueueRE.FindStringSubmatch(msg); pqm != nil {
		others, _ := strconv.Atoi(pqm[2])
		events = append(events, proto.ParsedEvent{Type: proto.EventPvpQueue, Timestamp: ts, Data: map[string]any{"match_name": pqm[1], "other_players": others}})
	}
	if strings.Contains(msg, "Cancelled PVP queue") {
		events = append(events, proto.ParsedEvent{Type: proto.EventPvpQueueCancelled, Timestamp: ts})
	}

	// o. Passive alloc
	if pam := passiveAllocRE.FindStringSubmatch(msg); pam != nil {
		evType := proto.EventPassiveAllocated
		if pam[1] == "unallocated" {
			evType = proto.EventPassiveUnallocated
		}
		events = append(events, proto.ParsedEvent{
			Type:      evType,
			Timestamp: ts,
			Data:      map[string]any{"skill_id": pam[2], "skill_name": pam[3], "is_mastery": false},
		})
	}

	// p. Mastery alloc
	if mam := masteryAllocRE.FindStringSubmatch(msg); mam != nil {
		evType := proto.EventPassiveAllocated
		if mam[1] == "unallocated" {
			evType = proto.EventPassiveUnallocated
		}
		events = append(events, proto.ParsedEvent{
			Type:      evType,
			Timestamp: ts,
			Data:      map[string]any{"skill_id": mam[2], "skill_name": mam[3], "is_mastery": true},
		})
	}

	// q. Whisper
	if wm := whisperRE.FindStringSubmatch(msg); wm != nil {
		dir := strings.ToLower(wm[1])
		data := map[string]any{
			"direction": dir,
			"player":    wm[3],
			"message":   wm[4],
		}
		if wm[2] != "" {
			data["guild_tag"] = wm[2]
		}
		events = append(events, proto.ParsedEvent{Type: proto.EventWhisper, Timestamp: ts, Data: data})
		if dir == "to" {
			if ev := p.clearAltTab(ts); ev != nil {
				events = append(events, *ev)
			}
		}
	}

	// r. Death
	if dm := deathRE.FindStringSubmatch(msg); dm != nil {
		events = append(events, proto.ParsedEvent{Type: proto.EventCharacterDeath, Timestamp: ts, Data: map[string]any{"character": dm[1]}})
	}

	// s. Chat
	if cm := chatRE.FindStringSubmatch(msg); cm != nil {
		data := map[string]any{
			"channel": cm[1],
			"player":  cm[3],
			"message": cm[4],
		}
		if cm[2] != "" {
			data["guild_tag"] = cm[2]
		}
		events = append(events, proto.ParsedEvent{Type: proto.EventChat, Timestamp: ts, Data: data})
	}

	// t. Area entered
	if p.pendingCode != "" &&
		p.loc != locLoginScreen &&
		p.loc != locConnectingFromLogin &&
		p.loc != locConnectingFromZone &&
		p.loc != locAwaitingScene {
		if em := enteredRE.FindStringSubmatch(msg); em != nil {
			events = append(events, proto.ParsedEvent{
				Type:      proto.EventAreaEntered,
				Timestamp: ts,
				Data:      map[string]any{"area_name": em[1], "area_code": p.pendingCode, "area_level": p.pendingLevel},
			})
			p.loc = locInZone
			p.pendingCode = ""
		}
	}

	// u. [SCENE] Set Source
	if sm := sceneSourceRE.FindStringSubmatch(msg); sm != nil {
		sceneName := sm[1]
		if sceneName == "(unknown)" {
			if p.loc == locAwaitingScene {
				p.loc = locCharSelect
				events = append(events, proto.ParsedEvent{Type: proto.EventCharSelect, Timestamp: ts})
			} else {
				p.loc = locLoginScreen
				events = append(events, proto.ParsedEvent{Type: proto.EventLoginScreen, Timestamp: ts})
			}
			p.pendingCode = ""
		} else if sceneName != "(null)" && p.pendingCode == "" &&
			p.loc != locLoginScreen &&
			p.loc != locConnectingFromLogin &&
			p.loc != locConnectingFromZone &&
			p.loc != locAwaitingScene {
			events = append(events, proto.ParsedEvent{
				Type:      proto.EventAreaEntered,
				Timestamp: ts,
				Data:      map[string]any{"area_name": sceneName, "area_code": sceneName, "area_level": 0},
			})
			p.loc = locInZone
		}
	}

	// v. Connecting state machine
	if strings.Contains(msg, "Async connecting to ") {
		if p.loc == locLoginScreen {
			p.loc = locConnectingFromLogin
		} else if p.loc == locInZone {
			p.loc = locConnectingFromZone
		}
	}
	if strings.Contains(msg, "Connected to ") {
		if p.loc == locConnectingFromLogin {
			p.loc = locCharSelect
			events = append(events, proto.ParsedEvent{Type: proto.EventCharSelect, Timestamp: ts})
		} else if p.loc == locConnectingFromZone {
			p.loc = locAwaitingScene
		}
	}

	return events
}
