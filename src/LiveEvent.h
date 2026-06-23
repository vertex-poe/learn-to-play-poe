#pragma once

#include <QMetaType>
#include <QString>
#include <QVariantMap>

// LiveEvent carries parsed game log events through the pub/sub bus during live mode.
//
// Data map keys by event type:
//   area_entered:         area_name, area_code, area_level, area_type (empty if unknown)
//   level_up:             character, char_class, level
//   character_death:      character
//   afk_on:               (none)
//   afk_off:              duration_secs
//   whisper:              direction (from/to), player, message
//   chat:                 channel (#/$/%/&), guild_tag (optional), player, message
//   achievement:          name
//   hideout_discovered:   name
//   pvp_queue:            match_name, other_players
//   pvp_queue_cancelled:  (none)
//   passive_allocated:    skill_id, skill_name, is_mastery
//   passive_unallocated:  skill_id, skill_name, is_mastery
//   quest_event:          event_type (sub-type string)
//   general_event:        event_type (sub-type string)
//   session_start:        (none)
struct LiveEvent {
    QString     type;
    QString     timestamp; // "2026-06-03 14:23:45" from the log
    QVariantMap data;
};
Q_DECLARE_METATYPE(LiveEvent)

namespace LiveEventType {
    inline constexpr const char* AreaEntered        = "area_entered";
    inline constexpr const char* LevelUp            = "level_up";
    inline constexpr const char* CharacterDeath     = "character_death";
    inline constexpr const char* AfkOn              = "afk_on";
    inline constexpr const char* AfkOff             = "afk_off";
    inline constexpr const char* Whisper            = "whisper";
    inline constexpr const char* Chat               = "chat";
    inline constexpr const char* Achievement        = "achievement";
    inline constexpr const char* HideoutDiscovered  = "hideout_discovered";
    inline constexpr const char* PvpQueue           = "pvp_queue";
    inline constexpr const char* PvpQueueCancelled  = "pvp_queue_cancelled";
    inline constexpr const char* PassiveAllocated   = "passive_allocated";
    inline constexpr const char* PassiveUnallocated = "passive_unallocated";
    inline constexpr const char* QuestEvent         = "quest_event";
    inline constexpr const char* GeneralEvent       = "general_event";
    inline constexpr const char* SessionStart       = "session_start";
}
