// Package testfixtures holds sample Client.txt content shared by the parser
// and ingest test suites, so both layers exercise the exact same lines
// instead of drifting apart over time.
package testfixtures

import "strings"

// SampleSession is a synthetic but realistic Client.txt excerpt covering one
// line for every event type parser.Parser currently recognises (see
// proto.Event* in internal/proto/proto.go), in an order that satisfies the
// parser's location state machine (login screen -> char select -> in zone).
// Update this whenever a new event type is added, so the completeness checks
// in internal/parser and internal/ingest keep covering every type.
const SampleSession = `2024/01/15 10:00:00 ***** LOG FILE OPENING *****
2024/01/15 10:00:01 100 a [INFO] Client 1 : Set Source [(unknown)]
2024/01/15 10:00:02 101 a [INFO] Client 1 : Async connecting to 192.168.1.1:6112
2024/01/15 10:00:03 102 a [INFO] Client 1 : Connected to 192.168.1.1:6112
2024/01/15 10:00:04 103 a [DEBUG] Client 1 : Generating level 1 area "1_1_town" with seed 123
2024/01/15 10:00:05 104 a [INFO] Client 1 : You have entered Lioneye's Watch.
2024/01/15 10:00:06 105 a [INFO] Client 1 : Xylia (Witch) is now level 2
2024/01/15 10:00:07 106 a [INFO] Client 1 : AFK mode is now ON.
2024/01/15 10:00:37 107 a [INFO] Client 1 : AFK mode is now OFF.
2024/01/15 10:00:38 108 a [INFO] [WINDOW] Lost focus
2024/01/15 10:00:40 109 a [INFO] [WINDOW] Gained focus
2024/01/15 10:00:41 110 a [INFO] Client 1 : @From Alice: hey there
2024/01/15 10:00:42 111 a [INFO] Client 1 : #Bob: hi all
2024/01/15 10:00:43 112 a [INFO] Client 1 : Achivement stored: AllOptionalDialogue
2024/01/15 10:00:44 113 a [INFO] Client 1 : Spawning discoverable Hideout Tidal Island Hideout
2024/01/15 10:00:45 114 a [INFO] Client 1 : Queueing for PVP match "CTF Open" with 3 other players
2024/01/15 10:00:46 115 a [INFO] Client 1 : Cancelled PVP queue
2024/01/15 10:00:47 116 a [INFO] Client 1 : Successfully allocated passive skill id: accuracy581, name: Projectile Damage
2024/01/15 10:00:48 117 a [INFO] Client 1 : Successfully unallocated passive skill id: accuracy581, name: Projectile Damage
2024/01/15 10:00:49 118 a [INFO] Client 1 : Successfully allocated mastery effect id: mastery123, mastery: Culling, name: Culling Strike Mastery
2024/01/15 10:00:50 119 a [INFO] Client 1 : Xylia has been slain.
2024/01/15 10:00:51 120 a [INFO] Client 1 : Joined guild named Unicorns with 5 members
2024/01/15 10:00:52 121 a [INFO] Client 1 : Guild member updated KayKay83
2024/01/15 10:00:53 122 a [INFO] Client 1 : You have joined global chat channel 1,137 English.
2024/01/15 10:00:54 123 a [INFO] Client 1 : 95 total Passive Skill Points (91 allocated)
2024/01/15 10:00:54 124 a [INFO] Client 1 : 6 total Ascendancy Skill Points (6 allocated)
2024/01/15 10:00:54 125 a [INFO] Client 1 : 71 Passive Skill Points from character level
2024/01/15 10:00:54 126 a [INFO] Client 1 : 24 Passive Skill Points from quests:
2024/01/15 10:00:54 127 a [INFO] Client 1 : (1 from The Dweller of the Deep)
2024/01/15 10:00:54 128 a [INFO] Client 1 : (2 from Sever the Right Hand)
2024/01/15 10:00:55 129 a [INFO] Client 1 : You have played for 15 hours, 41 minutes, and 32 seconds.
2024/01/15 10:00:56 130 a [INFO] Client 1 : 0 monsters remain.
2024/01/15 10:00:57 131 a [INFO] Client 1 : You have received a Passive Skill Point.
2024/01/15 10:00:58 132 a [INFO] Client 1 : You have received 2 Passive Skill Points.
2024/01/15 10:00:59 133 a [INFO] Client 1 : You have received 1 Passive Respec Points.
2024/01/15 10:01:00 134 a [INFO] Client 1 : Kitava's merciless affliction reduces your resistances.
2024/01/15 10:01:01 135 a [INFO] Client 1 : InstanceClientLabyrinthCraftResultOptionsList recieved
2024/01/15 10:01:02 136 a [INFO] Client 1 : TalkingPetAudioEvent 'Squawk'
2024/01/15 10:01:03 137 a [INFO] Client 1 : There has been a patch that you need to update to.
2024/01/15 10:01:04 138 a [INFO] Client 1 : Failed to create ruleset 42 (HardcoreRuleset)
`

// SampleSessionLines splits SampleSession into individual log lines, in
// order, with no blank trailing entry.
func SampleSessionLines() []string {
	lines := strings.Split(strings.TrimRight(SampleSession, "\n"), "\n")
	return lines
}
