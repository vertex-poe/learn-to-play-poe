#include "LogIngestWorker.h"

#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QThread>
#include <sqlite3.h>

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

LogIngestWorker::LogIngestWorker(const QString &dbPath, qint64 installId,
                                 const QString &logPath, qint64 resumeOffset,
                                 const QHash<int, QString> &channelNames,
                                 bool liveMode,
                                 QObject *parent)
    : BackgroundWorker(parent)
    , m_dbPath(dbPath)
    , m_installId(installId)
    , m_logPath(logPath)
    , m_resumeOffset(resumeOffset)
    , m_channelNames(channelNames)
    , m_liveMode(liveMode)
{}

void LogIngestWorker::start()
{
    sqlite3 *db = nullptr;
    if (sqlite3_open(m_dbPath.toUtf8().constData(), &db) != SQLITE_OK) {
        emit failed(QStringLiteral("Cannot open database: %1")
                        .arg(QString::fromUtf8(sqlite3_errmsg(db))));
        sqlite3_close(db);
        return;
    }
    execSql(db, "PRAGMA journal_mode=WAL;");
    execSql(db, "PRAGMA synchronous=NORMAL;");
    execSql(db, "PRAGMA busy_timeout=5000;");

    // ReadOnly is the only acceptable open mode — we must never create or
    // truncate Client.txt.  Bail silently if the file has disappeared since
    // the caller checked; that is not an error worth surfacing.
    if (!QFile::exists(m_logPath)) {
        sqlite3_close(db);
        emit finished();
        return;
    }

    QFile file(m_logPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        emit failed(QStringLiteral("Cannot open log: %1").arg(file.errorString()));
        sqlite3_close(db);
        return;
    }

    qint64 totalSize = file.size();
    if (m_resumeOffset > 0 && m_resumeOffset <= totalSize)
        file.seek(m_resumeOffset);

    QElapsedTimer wallClock;
    wallClock.start();
    qDebug() << "[ingest] start path=" << m_logPath
             << "size=" << totalSize
             << "offset=" << m_resumeOffset
             << "live=" << m_liveMode.load(std::memory_order_relaxed);

    const QFileInfo fi(m_logPath);
    const qint64 fileCreatedAt  = fi.birthTime().toSecsSinceEpoch();
    const qint64 fileModifiedAt = fi.lastModified().toSecsSinceEpoch();
    const qint64 fileSize       = fi.size();

    // Groups: (1) timestamp, (2) level, (3) optional bracket tag, (4) message body
    static const QRegularExpression lineRe(
        R"(^(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}) \d+ [0-9a-f]+ \[(\w+)[^\]]*\](?: \[(\w+)\])? ?(.*))"
    );
    // [DEBUG] Generating level 13 area "1_1_town" with seed 1
    static const QRegularExpression generatingRe(
        R"re(Generating level (\d+) area "([^"]+)")re"
    );
    // [INFO] : You have entered Lioneye's Watch.
    static const QRegularExpression enteredRe(
        R"(You have entered (.+?)\.)"
    );
    // [INFO] [SCENE] Set Source [Volcano]  (message body after stripping the [SCENE] bracket)
    static const QRegularExpression sceneSourceRe(
        R"(Set Source \[([^\]]+)\])"
    );
    // [INFO] Joined guild named Unicorns with 5 members
    static const QRegularExpression guildRe(
        R"(Joined guild named (.+?) with \d+ members)"
    );
    // [INFO] Guild details changed Pomegranate
    static const QRegularExpression guildDetailsRe(
        R"(Guild details changed (.+))"
    );
    // [INFO] Guild member updated KayKay83
    static const QRegularExpression guildMemberRe(
        R"(Guild member updated (\S+))"
    );
    // [INFO] : You have joined global chat channel 1,137 English.
    static const QRegularExpression chatChannelRe(
        R"(You have joined global chat channel ([\d,]+) (\w+))"
    );
    // [INFO] : orisRangerAEFive (Ranger) is now level 3
    static const QRegularExpression levelUpRe(
        R"((\S+) \((\w+)\) is now level (\d+))"
    );
    // [INFO] : AFK mode is now ON.  /  : AFK mode is now OFF.
    static const QRegularExpression afkRe(
        R"(AFK mode is now (ON|OFF))"
    );
    // [INFO] @From YaYtOtEmZ: hey  /  @To <GUILD> YaYtOtEmZ: hello
    static const QRegularExpression whisperRe(
        R"(@(From|To) (?:<([^>]*)> )?(\S+): (.*))"
    );
    // [INFO] Successfully (allocated|unallocated) passive skill id: accuracy581, name: Projectile Damage and Attack Speed
    static const QRegularExpression passiveAllocRe(
        R"(Successfully (allocated|unallocated) passive skill id: ([^,]+), name: (.+))"
    );
    // [INFO] Successfully (allocated|unallocated) mastery effect id: 48385, mastery: mastery_elemental99, name: Elemental Mastery
    static const QRegularExpression masteryAllocRe(
        R"(Successfully (allocated|unallocated) mastery effect id: ([^,]+), mastery: [^,]+, name: (.+))"
    );
    // [INFO] : orisRangerAEFive has been slain.
    static const QRegularExpression deathRe(
        R"((\S+) has been slain\.)"
    );
    // [INFO] #DDIsBrokenAF: someone can kill my black sycle boss pls
    // [INFO] #<¾:aLkO> Stary_Dziadu_Scythe: LF UBER SHAPER CARRY
    // channel prefix: '#' global, '$' trade, '%' party, '&' guild
    // guild tag is optional, enclosed in <>, and may contain ':'
    static const QRegularExpression chatRe(
        R"(([#$%&])(?:<([^>]*)> )?(\S+): (.*))"
    );
    // [INFO] : You have played for 15 hours, 41 minutes, and 32 seconds.
    static const QRegularExpression playedRe(
        R"(You have played for .+?\.)"
    );
    static const QRegularExpression playedUnitRe(
        R"((\d+) (hours?|minutes?|seconds?))"
    );
    // [INFO] Achivement stored: AllOptionalDialogue  (note: typo in log is intentional)
    static const QRegularExpression achievementRe(
        R"(Achivement stored: (\S+))"
    );
    // [INFO] Spawning discoverable Hideout Tidal Island Hideout
    static const QRegularExpression hideoutRe(
        R"(Spawning discoverable Hideout (.+))"
    );
    // [INFO] Queueing for PVP match "US-ASTHC CTF Open" with 0 other players
    static const QRegularExpression pvpQueueRe(
        R"re(Queueing for PVP match "([^"]+)" with (\d+) other players)re"
    );
    // /passives command — multi-line block:
    // : 95 total Passive Skill Points (91 allocated)
    static const QRegularExpression passivesTotalRe(
        R"((\d+) total Passive Skill Points \((\d+) allocated\))"
    );
    // : 6 total Ascendancy Skill Points (6 allocated)
    static const QRegularExpression passivesAscRe(
        R"((\d+) total Ascendancy Skill Points \((\d+) allocated\))"
    );
    // : 71 Passive Skill Points from character level
    static const QRegularExpression passivesLevelRe(
        R"((\d+) Passive Skill Points from character level)"
    );
    // : 24 Passive Skill Points from quests:
    static const QRegularExpression passivesQuestTotalRe(
        R"((\d+) Passive Skill Points from quests:)"
    );
    // : (1 from The Dweller of the Deep)
    static const QRegularExpression passivesQuestEntryRe(
        R"(\((\d+) from (.+)\))"
    );
    // Noise: "Client couldn't execute a triggered action from the server." and
    // "Instant/Triggered action..." each emit 1–5 followup key=N / key: N lines.
    static const QRegularExpression triggerFollowupRe(
        R"([\w ]+[=:] ?\d+)"
    );
    // [INFO] Failed to create ruleset 130 (PlayerHarbingerRules)
    static const QRegularExpression rulesetFailedRe(
        R"(Failed to create ruleset \d+ \(([^)]+)\))"
    );
    // [INFO] [InGameAudioManager] TalkingPetAudioEvent 'PlayerRevivedGreaterOrEqual200Times' triggered
    static const QRegularExpression talkingPetRe(
        R"(TalkingPetAudioEvent '([^']+)')"
    );

    // ── prepared statements ──────────────────────────────────────────────────

    sqlite3_stmt *areaUpsertStmt      = nullptr;
    sqlite3_stmt *areaInsertIgnoreStmt = nullptr;
    sqlite3_stmt *areaSelectStmt      = nullptr;
    sqlite3_stmt *moveInsertStmt      = nullptr;
    sqlite3_stmt *accountUpsertStmt   = nullptr;
    sqlite3_stmt *guildMemberStmt     = nullptr;
    sqlite3_stmt *guildMemberInsertStmt = nullptr;
    sqlite3_stmt *channelUpsertStmt   = nullptr;
    sqlite3_stmt *channelSelectStmt   = nullptr;
    sqlite3_stmt *channelJoinStmt     = nullptr;
    sqlite3_stmt *classUpsertStmt     = nullptr;
    sqlite3_stmt *classSelectStmt     = nullptr;
    sqlite3_stmt *charUpsertStmt      = nullptr;
    sqlite3_stmt *charSelectStmt      = nullptr;
    sqlite3_stmt *levelEventStmt      = nullptr;
    sqlite3_stmt *sessionInsertStmt   = nullptr;
    sqlite3_stmt *sessionSelectStmt   = nullptr;
    sqlite3_stmt *sessionCloseStmt    = nullptr;
    sqlite3_stmt *afkStmt             = nullptr;
    sqlite3_stmt *spanInsertStmt      = nullptr;
    sqlite3_stmt *spanSelectStmt      = nullptr;
    sqlite3_stmt *spanCloseStmt       = nullptr;
    sqlite3_stmt *spanUpdateCharStmt  = nullptr;
    sqlite3_stmt *questEventStmt       = nullptr;
    sqlite3_stmt *passiveUpsertStmt    = nullptr;
    sqlite3_stmt *passiveSelectStmt    = nullptr;
    sqlite3_stmt *passiveAllocStmt     = nullptr;
    sqlite3_stmt *whisperStmt          = nullptr;
    sqlite3_stmt *deathStmt            = nullptr;
    sqlite3_stmt *guildUpsertStmt      = nullptr;
    sqlite3_stmt *guildSelectStmt      = nullptr;
    sqlite3_stmt *pubCharUpsertStmt    = nullptr;
    sqlite3_stmt *pubCharSelectStmt    = nullptr;
    sqlite3_stmt *pubCharGuildStmt     = nullptr;
    sqlite3_stmt *chatStmt             = nullptr;
    sqlite3_stmt *achievUpsertStmt     = nullptr;
    sqlite3_stmt *achievSelectStmt     = nullptr;
    sqlite3_stmt *achievEventStmt      = nullptr;
    sqlite3_stmt *hideoutUpsertStmt    = nullptr;
    sqlite3_stmt *hideoutSelectStmt    = nullptr;
    sqlite3_stmt *hideoutEventStmt     = nullptr;
    sqlite3_stmt *pvpMatchUpsertStmt   = nullptr;
    sqlite3_stmt *pvpMatchSelectStmt   = nullptr;
    sqlite3_stmt *pvpQueueEventStmt    = nullptr;
    sqlite3_stmt *pvpQueueCancelStmt   = nullptr;
    sqlite3_stmt *playedEventStmt        = nullptr;
    sqlite3_stmt *charPlayedStmt         = nullptr;
    sqlite3_stmt *charPlayedFromSpanStmt = nullptr;
    sqlite3_stmt *passSnapInsertStmt     = nullptr;
    sqlite3_stmt *passSnapSelectStmt     = nullptr;
    sqlite3_stmt *passQuestUpsertStmt    = nullptr;
    sqlite3_stmt *passQuestSelectStmt    = nullptr;
    sqlite3_stmt *passSnapQuestStmt      = nullptr;
    sqlite3_stmt *rulesetFailedStmt       = nullptr;
    sqlite3_stmt *generalEventStmt        = nullptr;
    sqlite3_stmt *clientScreenEventStmt   = nullptr;
    sqlite3_stmt *sourceStmt           = nullptr;
    sqlite3_stmt *eventInsertStmt      = nullptr;

    sqlite3_prepare_v2(db,
        "INSERT INTO areas(code, level, display_name) VALUES(?,?,?) "
        "ON CONFLICT(code) DO UPDATE SET level=excluded.level, display_name=excluded.display_name;",
        -1, &areaUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO areas(code, level, display_name) VALUES(?,0,?);",
        -1, &areaInsertIgnoreStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id, type, subtype FROM areas WHERE code=?;",
        -1, &areaSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO area_moves(install_id, area_id, entered_at) VALUES(?,?,?);",
        -1, &moveInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO accounts(name, guild_name) VALUES('unknown', ?) "
        "ON CONFLICT(name) DO UPDATE SET guild_name=excluded.guild_name;",
        -1, &accountUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO accounts(name) VALUES(?);",
        -1, &guildMemberStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO guild_members(guild_name, account_id) "
        "SELECT ?, id FROM accounts WHERE name=?;",
        -1, &guildMemberInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO chat_channels(number, lang, name) VALUES(?,?,?) "
        "ON CONFLICT(number) DO UPDATE SET lang=excluded.lang, "
        "name=COALESCE(excluded.name, chat_channels.name);",
        -1, &channelUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM chat_channels WHERE number=?;",
        -1, &channelSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO chat_channel_joins(install_id, channel_id, joined_at) VALUES(?,?,?);",
        -1, &channelJoinStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO classes(name) VALUES(?);",
        -1, &classUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM classes WHERE name=?;",
        -1, &classSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO characters(name, class_id, level) VALUES(?,?,?) "
        "ON CONFLICT(name) DO UPDATE SET class_id=excluded.class_id, level=excluded.level;",
        -1, &charUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM characters WHERE name=?;",
        -1, &charSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO character_level_events(install_id, char_id, level, occurred_at) VALUES(?,?,?,?);",
        -1, &levelEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO sessions(install_id, started_at) VALUES(?,?);",
        -1, &sessionInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM sessions WHERE install_id=? AND started_at=?;",
        -1, &sessionSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE sessions SET ended_at=?, total_secs=?, afk_secs=?, active_secs=?, "
        "char_id=?, area_id=? WHERE id=?;",
        -1, &sessionCloseStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO session_afk(session_id, afk_on_at, afk_off_at) VALUES(?,?,?) "
        "ON CONFLICT(session_id, afk_on_at) DO UPDATE SET afk_off_at=excluded.afk_off_at;",
        -1, &afkStmt, nullptr);
    // area_id and char_id may be NULL (char select / unknown character).
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO area_time_spans(session_id, area_id, char_id, entered_at) VALUES(?,?,?,?);",
        -1, &spanInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM area_time_spans WHERE session_id=? AND entered_at=?;",
        -1, &spanSelectStmt, nullptr);
    // duration computed from stored entered_at so we never need it in memory.
    sqlite3_prepare_v2(db,
        "UPDATE area_time_spans SET "
        "exited_at=?, "
        "duration_secs=CAST((julianday(?)-julianday(entered_at))*86400.0 AS INTEGER), "
        "afk_secs=? "
        "WHERE id=?;",
        -1, &spanCloseStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE area_time_spans SET char_id=? WHERE id=?;",
        -1, &spanUpdateCharStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO quest_events(session_id, area_id, event_type, occurred_at) VALUES(?,?,?,?);",
        -1, &questEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO passive_skills(code, name, is_mastery) VALUES(?,?,?) "
        "ON CONFLICT(code) DO UPDATE SET name=excluded.name;",
        -1, &passiveUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM passive_skills WHERE code=?;",
        -1, &passiveSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO passive_skill_allocations(session_id, char_id, passive_skill_id, action, allocated_at) VALUES(?,?,?,?,?);",
        -1, &passiveAllocStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO whispers(session_id, direction, player_name, guild_id, message, occurred_at) VALUES(?,?,?,?,?,?);",
        -1, &whisperStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO character_deaths(session_id, char_id, area_id, level, occurred_at) VALUES(?,?,?,?,?);",
        -1, &deathStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO guilds(tag) VALUES(?);",
        -1, &guildUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM guilds WHERE tag=?;",
        -1, &guildSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO public_chars(name) VALUES(?);",
        -1, &pubCharUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM public_chars WHERE name=?;",
        -1, &pubCharSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE public_chars SET guild_id=? WHERE id=?;",
        -1, &pubCharGuildStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO chats(session_id, public_char_id, channel, guild_id, message, occurred_at) VALUES(?,?,?,?,?,?);",
        -1, &chatStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO character_played_events(session_id, span_id, played_secs, occurred_at) VALUES(?,?,?,?);",
        -1, &playedEventStmt, nullptr);
    // Immediate update when char is already known at event time.
    sqlite3_prepare_v2(db,
        "UPDATE characters SET played_secs=MAX(played_secs,?) WHERE id=?;",
        -1, &charPlayedStmt, nullptr);
    // Called from the level-up handler: syncs played_secs for any played events
    // already recorded in the span that just had its char_id filled in.
    sqlite3_prepare_v2(db,
        "UPDATE characters SET played_secs=MAX(played_secs,"
        "COALESCE((SELECT MAX(played_secs) FROM character_played_events WHERE span_id=?),0)) WHERE id=?;",
        -1, &charPlayedFromSpanStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO passive_point_snapshots"
        "(session_id, char_id, occurred_at, total_points, allocated_points,"
        " ascendancy_total, ascendancy_allocated, level_points, quest_points)"
        " VALUES(?,?,?,?,?,?,?,?,?);",
        -1, &passSnapInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM passive_point_snapshots WHERE session_id=? AND occurred_at=?;",
        -1, &passSnapSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO passive_quest_sources(name) VALUES(?);",
        -1, &passQuestUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM passive_quest_sources WHERE name=?;",
        -1, &passQuestSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO passive_snapshot_quests(snapshot_id, quest_id, points) VALUES(?,?,?);",
        -1, &passSnapQuestStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO achievements(code) VALUES(?);",
        -1, &achievUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM achievements WHERE code=?;",
        -1, &achievSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO achievement_events(session_id, achievement_id, occurred_at) VALUES(?,?,?);",
        -1, &achievEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO hideouts(name) VALUES(?);",
        -1, &hideoutUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM hideouts WHERE name=?;",
        -1, &hideoutSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO hideout_discovered_events(session_id, hideout_id, area_id, occurred_at) VALUES(?,?,?,?);",
        -1, &hideoutEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO pvp_matches(name) VALUES(?);",
        -1, &pvpMatchUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM pvp_matches WHERE name=?;",
        -1, &pvpMatchSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO pvp_queue_events(session_id, match_id, player_count, occurred_at) VALUES(?,?,?,?);",
        -1, &pvpQueueEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE pvp_queue_events SET cancelled_at=? WHERE id=?;",
        -1, &pvpQueueCancelStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO zone_ruleset_failed_events(session_id, area_id, ruleset_name, occurred_at) VALUES(?,?,?,?);",
        -1, &rulesetFailedStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO general_events(session_id, area_id, event_type, occurred_at) VALUES(?,?,?,?);",
        -1, &generalEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO client_screen_events(install_id, event_type, occurred_at) VALUES(?,?,?);",
        -1, &clientScreenEventStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE installs SET "
        "file_created_at=?, file_modified_at=?, file_size=?, last_byte_offset=? "
        "WHERE id=?;",
        -1, &sourceStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO events(occurred_at, event_type, source_id) VALUES(?,?,?);",
        -1, &eventInsertStmt, nullptr);

    // ── helpers ──────────────────────────────────────────────────────────────

    // Call immediately after sqlite3_step(someEventStmt) — before any other step —
    // so sqlite3_changes and sqlite3_last_insert_rowid still reflect that insert.
    auto insertEvent = [&](const QByteArray &tsBytes, const char *eventType) {
        if (sqlite3_changes(db) == 0) return;
        const qint64 sourceId = sqlite3_last_insert_rowid(db);
        sqlite3_bind_text (eventInsertStmt, 1, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
        sqlite3_bind_text (eventInsertStmt, 2, eventType, -1, SQLITE_STATIC);
        sqlite3_bind_int64(eventInsertStmt, 3, sourceId);
        sqlite3_step(eventInsertStmt);
        sqlite3_reset(eventInsertStmt);
    };

    auto flushSource = [&](qint64 offset) {
        sqlite3_bind_int64(sourceStmt, 1, fileCreatedAt);
        sqlite3_bind_int64(sourceStmt, 2, fileModifiedAt);
        sqlite3_bind_int64(sourceStmt, 3, fileSize);
        sqlite3_bind_int64(sourceStmt, 4, offset);
        sqlite3_bind_int64(sourceStmt, 5, m_installId);
        sqlite3_step(sourceStmt);
        sqlite3_reset(sourceStmt);
    };

    auto tsToSecs = [](const QString &ts) -> qint64 {
        return QDateTime::fromString(ts, "yyyy-MM-dd HH:mm:ss").toSecsSinceEpoch();
    };

    // ── session / span state ─────────────────────────────────────────────────

    qint64  sessionId         = -1;
    QString sessionStartTs;
    qint64  sessionAfkSecs    = 0;
    QString afkOnTs;
    QString altTabOutTs;
    qint64  sessionCharId     = -1;
    int     sessionCharLevel  = -1;
    qint64  sessionAreaId     = -1;

    qint64  currentSpanId     = -1;
    qint64  currentSpanAfkSecs = 0;
    qint64  lastPvpQueueEventId = -1;

    QString lastTs;

    // Closes the current area_time_span, recording exited_at, duration, and
    // accumulated AFK for that span.  If AFK is active when the span closes
    // (e.g. the player portalled while AFK) the partial AFK is captured and
    // afkOnTs is reset to endTs so the next span continues accumulating it.
    auto closeSpan = [&](const QString &endTs) {
        if (currentSpanId < 0 || endTs.isEmpty()) return;

        if (!afkOnTs.isEmpty()) {
            currentSpanAfkSecs += qMax(0LL, tsToSecs(endTs) - tsToSecs(afkOnTs));
            afkOnTs = endTs;  // AFK continues; reset origin to new span boundary
        }

        const QByteArray endTsBytes = endTs.toUtf8();
        sqlite3_bind_text (spanCloseStmt, 1, endTsBytes.constData(), endTsBytes.size(), SQLITE_STATIC);
        sqlite3_bind_text (spanCloseStmt, 2, endTsBytes.constData(), endTsBytes.size(), SQLITE_STATIC);
        sqlite3_bind_int64(spanCloseStmt, 3, currentSpanAfkSecs);
        sqlite3_bind_int64(spanCloseStmt, 4, currentSpanId);
        sqlite3_step(spanCloseStmt);
        sqlite3_reset(spanCloseStmt);

        currentSpanId      = -1;
        currentSpanAfkSecs = 0;
    };

    // Opens a new area_time_span for the given area (areaId=-1 → char select).
    // Uses INSERT OR IGNORE + SELECT so resume re-processing is safe.
    auto openSpan = [&](const QString &ts, qint64 areaId) {
        if (sessionId < 0) {
            qDebug() << "[ingest] openSpan dropped: sessionId=-1 ts=" << ts << "areaId=" << areaId;
            return;
        }

        const QByteArray tsBytes = ts.toUtf8();
        sqlite3_bind_int64(spanInsertStmt, 1, sessionId);
        if (areaId < 0)       sqlite3_bind_null (spanInsertStmt, 2);
        else                  sqlite3_bind_int64(spanInsertStmt, 2, areaId);
        if (sessionCharId < 0) sqlite3_bind_null (spanInsertStmt, 3);
        else                   sqlite3_bind_int64(spanInsertStmt, 3, sessionCharId);
        sqlite3_bind_text(spanInsertStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
        sqlite3_step(spanInsertStmt);
        sqlite3_reset(spanInsertStmt);

        if (sqlite3_changes(db) > 0) {
            currentSpanId = sqlite3_last_insert_rowid(db);
        } else {
            sqlite3_bind_int64(spanSelectStmt, 1, sessionId);
            sqlite3_bind_text (spanSelectStmt, 2, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
            currentSpanId = -1;
            if (sqlite3_step(spanSelectStmt) == SQLITE_ROW)
                currentSpanId = sqlite3_column_int64(spanSelectStmt, 0);
            sqlite3_reset(spanSelectStmt);
        }
        currentSpanAfkSecs = 0;
    };

    // Closes the current session, capping any open AFK / span first.
    auto closeSession = [&](const QString &endTs) {
        if (sessionId < 0 || endTs.isEmpty()) return;

        closeSpan(endTs);

        if (!afkOnTs.isEmpty()) {
            sessionAfkSecs += qMax(0LL, tsToSecs(endTs) - tsToSecs(afkOnTs));
            const QByteArray onBytes  = afkOnTs.toUtf8();
            const QByteArray offBytes = endTs.toUtf8();
            sqlite3_bind_int64(afkStmt, 1, sessionId);
            sqlite3_bind_text (afkStmt, 2, onBytes.constData(),  onBytes.size(),  SQLITE_STATIC);
            sqlite3_bind_text (afkStmt, 3, offBytes.constData(), offBytes.size(), SQLITE_STATIC);
            sqlite3_step(afkStmt);
            sqlite3_reset(afkStmt);
            afkOnTs.clear();
        }

        const qint64 totalSecs  = qMax(0LL, tsToSecs(endTs) - tsToSecs(sessionStartTs));
        const qint64 activeSecs = qMax(0LL, totalSecs - sessionAfkSecs);

        const QByteArray endTsBytes = endTs.toUtf8();
        sqlite3_bind_text(sessionCloseStmt, 1, endTsBytes.constData(), endTsBytes.size(), SQLITE_STATIC);
        sqlite3_bind_int64(sessionCloseStmt, 2, totalSecs);
        sqlite3_bind_int64(sessionCloseStmt, 3, sessionAfkSecs);
        sqlite3_bind_int64(sessionCloseStmt, 4, activeSecs);
        if (sessionCharId < 0) sqlite3_bind_null  (sessionCloseStmt, 5);
        else                   sqlite3_bind_int64 (sessionCloseStmt, 5, sessionCharId);
        if (sessionAreaId < 0) sqlite3_bind_null  (sessionCloseStmt, 6);
        else                   sqlite3_bind_int64 (sessionCloseStmt, 6, sessionAreaId);
        sqlite3_bind_int64(sessionCloseStmt, 7, sessionId);
        sqlite3_step(sessionCloseStmt);
        sqlite3_reset(sessionCloseStmt);

        sessionId      = -1;
        sessionStartTs.clear();
        sessionAfkSecs = 0;
        sessionCharId       = -1;
        sessionAreaId       = -1;
        lastPvpQueueEventId = -1;
    };

    // ── /passives block state ────────────────────────────────────────────────

    struct PassivesBlock {
        QString ts;
        int totalPoints = 0, allocatedPoints = 0;
        int ascTotal = 0, ascAllocated = 0;
        int levelPoints = 0, questPoints = 0;
        QVector<QPair<QString,int>> quests;
        bool active = false;
    };
    PassivesBlock pendingPassives;

    auto flushPassives = [&]() {
        if (!pendingPassives.active || sessionId < 0) {
            pendingPassives = PassivesBlock{};
            return;
        }
        const QByteArray tsBytes = pendingPassives.ts.toUtf8();

        sqlite3_bind_int64(passSnapInsertStmt, 1, sessionId);
        if (sessionCharId < 0) sqlite3_bind_null (passSnapInsertStmt, 2);
        else                   sqlite3_bind_int64(passSnapInsertStmt, 2, sessionCharId);
        sqlite3_bind_text(passSnapInsertStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
        sqlite3_bind_int (passSnapInsertStmt, 4, pendingPassives.totalPoints);
        sqlite3_bind_int (passSnapInsertStmt, 5, pendingPassives.allocatedPoints);
        sqlite3_bind_int (passSnapInsertStmt, 6, pendingPassives.ascTotal);
        sqlite3_bind_int (passSnapInsertStmt, 7, pendingPassives.ascAllocated);
        sqlite3_bind_int (passSnapInsertStmt, 8, pendingPassives.levelPoints);
        sqlite3_bind_int (passSnapInsertStmt, 9, pendingPassives.questPoints);
        sqlite3_step(passSnapInsertStmt);
        sqlite3_reset(passSnapInsertStmt);

        sqlite3_bind_int64(passSnapSelectStmt, 1, sessionId);
        sqlite3_bind_text (passSnapSelectStmt, 2, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
        qint64 snapId = -1;
        if (sqlite3_step(passSnapSelectStmt) == SQLITE_ROW)
            snapId = sqlite3_column_int64(passSnapSelectStmt, 0);
        sqlite3_reset(passSnapSelectStmt);

        if (snapId >= 0) {
            for (const auto &entry : pendingPassives.quests) {
                const QByteArray questBytes = entry.first.toUtf8();

                sqlite3_bind_text(passQuestUpsertStmt, 1, questBytes.constData(), questBytes.size(), SQLITE_STATIC);
                sqlite3_step(passQuestUpsertStmt);
                sqlite3_reset(passQuestUpsertStmt);

                sqlite3_bind_text(passQuestSelectStmt, 1, questBytes.constData(), questBytes.size(), SQLITE_STATIC);
                qint64 questId = -1;
                if (sqlite3_step(passQuestSelectStmt) == SQLITE_ROW)
                    questId = sqlite3_column_int64(passQuestSelectStmt, 0);
                sqlite3_reset(passQuestSelectStmt);

                if (questId >= 0) {
                    sqlite3_bind_int64(passSnapQuestStmt, 1, snapId);
                    sqlite3_bind_int64(passSnapQuestStmt, 2, questId);
                    sqlite3_bind_int  (passSnapQuestStmt, 3, entry.second);
                    sqlite3_step(passSnapQuestStmt);
                    sqlite3_reset(passSnapQuestStmt);
                }
            }
        }

        pendingPassives = PassivesBlock{};
    };

    // ── main loop ────────────────────────────────────────────────────────────

    // Client screen state machine — tracks where the player is in the UI flow.
    // Area data (pendingCode/pendingLevel) is separate transient state for zone loading.
    //
    // Three observed flows:
    //
    //   Regular launch:
    //     LOG FILE OPENING → Unknown
    //     [SCENE] Set Source [(unknown)]                          → LoginScreen
    //     Async connecting … / Connected …                       → ConnectingFromLogin → CharSelect
    //     [SCENE] Set Source [<zone>]  (or You have entered)     → InZone
    //
    //   Go back to login screen (from zone):
    //     [SCENE] Set Source [(null)]
    //     [SCENE] Set Source [(unknown)]                         → LoginScreen
    //
    //   Go back to character select (from zone):
    //     Async connecting … / Connected …                       → ConnectingFromZone → AwaitingScene
    //     [SCENE] Set Source [(null)]
    //     [SCENE] Set Source [(unknown)]                         → CharSelect
    enum class LocState { Unknown, LoginScreen, ConnectingFromLogin, ConnectingFromZone, AwaitingScene, CharSelect, InZone };
    LocState locState = LocState::Unknown;

    QString currentGuild;
    QString pendingCode;
    int     pendingLevel           = 0;
    bool    skipTriggerFollowup    = false;

    constexpr int kChunkSize    = 10'000;
    qint64        safeCommitPos = m_resumeOffset > 0 ? m_resumeOffset : 0;
    int           chunkCount    = 0;
    int           totalVisits   = 0;
    bool          caughtUp      = false;
    int           chunkCommits  = 0;
    const qint64  ingestStart   = m_resumeOffset > 0 ? m_resumeOffset : 0;

    // Emits AltTabBack with elapsed duration if the player is currently alt-tabbed.
    // Called on explicit [WINDOW] Gained focus and on player-action events (zone
    // transition, own whisper) that imply the player is back.
    auto clearAltTab = [&](const QString &gainedTs) {
        if (altTabOutTs.isEmpty()) return;
        const qint64 dur = qMax(0LL, tsToSecs(gainedTs) - tsToSecs(altTabOutTs));
        altTabOutTs.clear();
        if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
            emit liveEventParsed(LiveEvent{LiveEventType::AltTabBack, gainedTs, {{"duration_secs", dur}}});
    };

    execSql(db, "BEGIN;");

    // If resuming mid-session (app restarted while game was running), the LOG FILE
    // OPENING for the current session was already processed in a previous run, so
    // sessionId stays -1 and openSpan() silently drops every zone transition.
    // Recover sessionId and currentSpanId from the DB so new zone spans attach
    // to the correct session and the previous open span gets properly closed.
    if (m_resumeOffset > 0) {
        qDebug() << "[ingest] resumeOffset=" << m_resumeOffset << "— attempting mid-session recovery";
        sqlite3_stmt *recoverStmt = nullptr;
        sqlite3_prepare_v2(db,
            "SELECT s.id, ats.id "
            "FROM sessions s "
            "LEFT JOIN area_time_spans ats "
            "  ON ats.session_id = s.id AND ats.exited_at IS NULL "
            "WHERE s.install_id = ? AND s.ended_at IS NULL "
            "ORDER BY s.started_at DESC, ats.entered_at DESC "
            "LIMIT 1;",
            -1, &recoverStmt, nullptr);
        sqlite3_bind_int64(recoverStmt, 1, m_installId);
        if (sqlite3_step(recoverStmt) == SQLITE_ROW) {
            sessionId    = sqlite3_column_int64(recoverStmt, 0);
            if (sqlite3_column_type(recoverStmt, 1) != SQLITE_NULL)
                currentSpanId = sqlite3_column_int64(recoverStmt, 1);
            locState = LocState::InZone;
            qDebug() << "[ingest] mid-session recovery: sessionId=" << sessionId
                     << "currentSpanId=" << currentSpanId;
        } else {
            qDebug() << "[ingest] mid-session recovery: no open session for installId=" << m_installId;
        }
        sqlite3_finalize(recoverStmt);
    }

    while (!isCancelled()) {
        if (file.atEnd()) {
            if (!m_liveMode.load(std::memory_order_relaxed)) break;

            // Commit what we have so new events are visible to the UI immediately,
            // then sleep before polling for more content.
            if (chunkCount > 0 || !caughtUp) {
                if (chunkCount > 0) {
                    const qint64 commitStart = wallClock.elapsed();
                    flushSource(file.pos());
                    execSql(db, "COMMIT;");
                    totalSize = qMax(totalSize, file.size());
                    const int pct = totalSize > 0
                        ? static_cast<int>((file.pos() * 100LL) / totalSize) : 0;
                    qDebug() << "[ingest] eof-commit"
                             << pct << "% pos=" << file.pos() << "/" << totalSize
                             << "visits=" << totalVisits
                             << "commit_ms=" << (wallClock.elapsed() - commitStart)
                             << "elapsed_ms=" << wallClock.elapsed();
                    chunkCount = 0;
                    execSql(db, "BEGIN;");
                }
                emit progress(100, QStringLiteral("%1 events").arg(totalVisits));
                caughtUp = true;
            }
            QThread::msleep(250);
            continue;
        }

        const qint64  lineStartPos = file.pos();
        const QString line         = QString::fromUtf8(file.readLine()).trimmed();

        // ── session boundary ─────────────────────────────────────────────────
        // LOG FILE OPENING lines have a different format with no hex ID or bracket:
        //   "2026/06/03 22:30:25 ***** LOG FILE OPENING *****"
        // They don't match lineRe, so handle them before the main parse gate.
        if (line.contains(QLatin1String("LOG FILE OPENING"))) {
            static const QRegularExpression logTsRe(
                R"(^(\d{4}/\d{2}/\d{2} \d{2}:\d{2}:\d{2}))");
            const auto tsM = logTsRe.match(line);
            if (tsM.hasMatch()) {
                QString ts = tsM.captured(1);
                ts[4] = '-'; ts[7] = '-';
                const QByteArray tsBytes = ts.toUtf8();
                const QString prevTs = lastTs;
                lastTs = ts;

                flushPassives();
                altTabOutTs.clear(); // don't carry alt-tab state across sessions
                closeSession(prevTs);

                sqlite3_bind_int64(sessionInsertStmt, 1, m_installId);
                sqlite3_bind_text (sessionInsertStmt, 2, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(sessionInsertStmt);
                sqlite3_reset(sessionInsertStmt);

                sqlite3_bind_int64(sessionSelectStmt, 1, m_installId);
                sqlite3_bind_text (sessionSelectStmt, 2, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sessionId = -1;
                if (sqlite3_step(sessionSelectStmt) == SQLITE_ROW)
                    sessionId = sqlite3_column_int64(sessionSelectStmt, 0);
                sqlite3_reset(sessionSelectStmt);

                sessionStartTs   = ts;
                sessionAfkSecs   = 0;
                sessionCharId    = -1;
                sessionCharLevel = -1;
                sessionAreaId    = -1;
                locState         = LocState::Unknown;
                pendingCode.clear();
                pendingLevel     = 0;

                openSpan(ts, -1);

                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::SessionStart, ts, {}});
            }
            continue;
        }

        const auto hdr = lineRe.match(line);
        if (!hdr.hasMatch()) continue;

        QString ts = hdr.captured(1);
        ts[4] = '-'; ts[7] = '-';   // 2026/06/03 → 2026-06-03
        const QByteArray tsBytes = ts.toUtf8();
        const QString prevTs = lastTs;
        lastTs = ts;

        const QString level   = hdr.captured(2);
        const QString message = hdr.captured(4).trimmed();

        // ── noise filter ─────────────────────────────────────────────────────
        if (skipTriggerFollowup) {
            if (triggerFollowupRe.match(message).hasMatch())
                continue;
            skipTriggerFollowup = false;
        }
        if (message.startsWith(QLatin1String("Client couldn't execute a triggered action")) ||
            message.startsWith(QLatin1String("Instant/Triggered action"))) {
            skipTriggerFollowup = true;
            continue;
        }

        if (level == QLatin1String("DEBUG")) {
            const auto genM = generatingRe.match(message);
            if (genM.hasMatch()) {
                pendingLevel = genM.captured(1).toInt();
                pendingCode  = genM.captured(2);
            }

        } else if (level == QLatin1String("INFO")) {

            // /passives multi-line state machine — must run before all other INFO checks
            if (pendingPassives.active) {
                const auto ascM = passivesAscRe.match(message);
                if (ascM.hasMatch()) {
                    pendingPassives.ascTotal     = ascM.captured(1).toInt();
                    pendingPassives.ascAllocated = ascM.captured(2).toInt();
                    continue;
                }
                const auto lvlM = passivesLevelRe.match(message);
                if (lvlM.hasMatch()) {
                    pendingPassives.levelPoints = lvlM.captured(1).toInt();
                    continue;
                }
                const auto qtM = passivesQuestTotalRe.match(message);
                if (qtM.hasMatch()) {
                    pendingPassives.questPoints = qtM.captured(1).toInt();
                    continue;
                }
                const auto qeM = passivesQuestEntryRe.match(message);
                if (qeM.hasMatch()) {
                    pendingPassives.quests.append({qeM.captured(2).trimmed(), qeM.captured(1).toInt()});
                    continue;
                }
                // No passives line matched — block is complete; flush and fall through
                flushPassives();
            }
            const auto passivesTotalM = passivesTotalRe.match(message);
            if (passivesTotalM.hasMatch() && sessionId >= 0) {
                flushPassives(); // safety: clear any stale state
                pendingPassives.active          = true;
                pendingPassives.ts              = ts;
                pendingPassives.totalPoints     = passivesTotalM.captured(1).toInt();
                pendingPassives.allocatedPoints = passivesTotalM.captured(2).toInt();
                continue;
            }

            // Window focus ([WINDOW] bracket tag)
            if (hdr.captured(3) == QLatin1String("WINDOW")) {
                if (message == QLatin1String("Lost focus")) {
                    altTabOutTs = ts;
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::AltTabOut, ts, {}});
                } else if (message == QLatin1String("Gained focus")) {
                    clearAltTab(ts);
                }
            }

            // Guild
            const auto guildM = guildRe.match(message);
            if (guildM.hasMatch()) {
                currentGuild = guildM.captured(1);
                const QByteArray guildBytes = currentGuild.toUtf8();
                sqlite3_bind_text(accountUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                sqlite3_step(accountUpsertStmt);
                sqlite3_reset(accountUpsertStmt);
            }

            // Guild details changed — also tells us the current guild
            const auto guildDetailsM = guildDetailsRe.match(message);
            if (guildDetailsM.hasMatch()) {
                currentGuild = guildDetailsM.captured(1).trimmed();
                const QByteArray guildBytes = currentGuild.toUtf8();
                sqlite3_bind_text(accountUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                sqlite3_step(accountUpsertStmt);
                sqlite3_reset(accountUpsertStmt);
            }

            // Guild member updated — record the account name and add to guild_members
            const auto guildMemberM = guildMemberRe.match(message);
            if (guildMemberM.hasMatch()) {
                const QByteArray nameBytes = guildMemberM.captured(1).toUtf8();
                sqlite3_bind_text(guildMemberStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                sqlite3_step(guildMemberStmt);
                sqlite3_reset(guildMemberStmt);

                if (!currentGuild.isEmpty()) {
                    const QByteArray guildBytes = currentGuild.toUtf8();
                    sqlite3_bind_text(guildMemberInsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text(guildMemberInsertStmt, 2, nameBytes.constData(),  nameBytes.size(),  SQLITE_STATIC);
                    sqlite3_step(guildMemberInsertStmt);
                    sqlite3_reset(guildMemberInsertStmt);
                }
            }

            // Chat channel join
            const auto chanM = chatChannelRe.match(message);
            if (chanM.hasMatch()) {
                const int num = chanM.captured(1).remove(QLatin1Char(',')).toInt();
                const QByteArray langBytes = chanM.captured(2).toUtf8();
                const QString label = m_channelNames.value(num);

                sqlite3_bind_int (channelUpsertStmt, 1, num);
                sqlite3_bind_text(channelUpsertStmt, 2, langBytes.constData(), langBytes.size(), SQLITE_STATIC);
                if (label.isEmpty())
                    sqlite3_bind_null(channelUpsertStmt, 3);
                else {
                    const QByteArray labelBytes = label.toUtf8();
                    sqlite3_bind_text(channelUpsertStmt, 3, labelBytes.constData(), labelBytes.size(), SQLITE_TRANSIENT);
                }
                sqlite3_step(channelUpsertStmt);
                sqlite3_reset(channelUpsertStmt);

                sqlite3_bind_int(channelSelectStmt, 1, num);
                qint64 channelId = -1;
                if (sqlite3_step(channelSelectStmt) == SQLITE_ROW)
                    channelId = sqlite3_column_int64(channelSelectStmt, 0);
                sqlite3_reset(channelSelectStmt);

                if (channelId >= 0) {
                    sqlite3_bind_int64(channelJoinStmt, 1, m_installId);
                    sqlite3_bind_int64(channelJoinStmt, 2, channelId);
                    sqlite3_bind_text (channelJoinStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(channelJoinStmt);
                    sqlite3_reset(channelJoinStmt);
                }
            }

            // Character level-up
            const auto lvlM = levelUpRe.match(message);
            if (lvlM.hasMatch()) {
                const QByteArray charNameBytes  = lvlM.captured(1).toUtf8();
                const QByteArray charClassBytes = lvlM.captured(2).toUtf8();
                const int        charLevel      = lvlM.captured(3).toInt();

                sqlite3_bind_text(classUpsertStmt, 1, charClassBytes.constData(), charClassBytes.size(), SQLITE_STATIC);
                sqlite3_step(classUpsertStmt);
                sqlite3_reset(classUpsertStmt);

                sqlite3_bind_text(classSelectStmt, 1, charClassBytes.constData(), charClassBytes.size(), SQLITE_STATIC);
                qint64 classId = -1;
                if (sqlite3_step(classSelectStmt) == SQLITE_ROW)
                    classId = sqlite3_column_int64(classSelectStmt, 0);
                sqlite3_reset(classSelectStmt);

                if (classId >= 0) {
                    sqlite3_bind_text (charUpsertStmt, 1, charNameBytes.constData(), charNameBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_int64(charUpsertStmt, 2, classId);
                    sqlite3_bind_int  (charUpsertStmt, 3, charLevel);
                    sqlite3_step(charUpsertStmt);
                    sqlite3_reset(charUpsertStmt);

                    sqlite3_bind_text(charSelectStmt, 1, charNameBytes.constData(), charNameBytes.size(), SQLITE_STATIC);
                    qint64 charId = -1;
                    if (sqlite3_step(charSelectStmt) == SQLITE_ROW)
                        charId = sqlite3_column_int64(charSelectStmt, 0);
                    sqlite3_reset(charSelectStmt);

                    if (charId >= 0) {
                        sqlite3_bind_int64(levelEventStmt, 1, m_installId);
                        sqlite3_bind_int64(levelEventStmt, 2, charId);
                        sqlite3_bind_int  (levelEventStmt, 3, charLevel);
                        sqlite3_bind_text (levelEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                        sqlite3_step(levelEventStmt);
                        insertEvent(tsBytes, "level_up");
                        sqlite3_reset(levelEventStmt);

                        if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                            emit liveEventParsed(LiveEvent{LiveEventType::LevelUp, ts, {
                                {"character",  lvlM.captured(1)},
                                {"char_class", lvlM.captured(2)},
                                {"level",      charLevel}
                            }});

                        sessionCharId    = charId;
                        sessionCharLevel = charLevel;

                        // Backfill char_id on the current open span now that we know it.
                        if (currentSpanId >= 0) {
                            sqlite3_bind_int64(spanUpdateCharStmt, 1, charId);
                            sqlite3_bind_int64(spanUpdateCharStmt, 2, currentSpanId);
                            sqlite3_step(spanUpdateCharStmt);
                            sqlite3_reset(spanUpdateCharStmt);

                            // Sync played_secs for any /played events recorded in this span
                            // before the character was known.
                            sqlite3_bind_int64(charPlayedFromSpanStmt, 1, currentSpanId);
                            sqlite3_bind_int64(charPlayedFromSpanStmt, 2, charId);
                            sqlite3_step(charPlayedFromSpanStmt);
                            sqlite3_reset(charPlayedFromSpanStmt);
                        }
                    }
                }
            }

            // AFK toggle
            const auto afkM = afkRe.match(message);
            if (afkM.hasMatch()) {
                if (afkM.captured(1) == QLatin1String("ON")) {
                    afkOnTs = ts;
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::AfkOn, ts, {}});
                } else if (!afkOnTs.isEmpty()) {
                    const qint64 afkDur = qMax(0LL, tsToSecs(ts) - tsToSecs(afkOnTs));
                    sessionAfkSecs     += afkDur;
                    currentSpanAfkSecs += afkDur;

                    const QByteArray onBytes = afkOnTs.toUtf8();
                    if (sessionId >= 0) {
                        sqlite3_bind_int64(afkStmt, 1, sessionId);
                        sqlite3_bind_text (afkStmt, 2, onBytes.constData(),  onBytes.size(),  SQLITE_STATIC);
                        sqlite3_bind_text (afkStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                        sqlite3_step(afkStmt);
                        sqlite3_reset(afkStmt);
                    }
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::AfkOff, ts, {{"duration_secs", afkDur}}});
                    afkOnTs.clear();
                }
            }

            // Quest events
            if (sessionId >= 0 && message.contains(QLatin1String("0 monsters remain."))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "monsters_cleared", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "monsters_cleared"}}});
            }

            if (sessionId >= 0 && message.contains(QLatin1String("You have received a Passive Skill Point."))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_skill_point_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "passive_skill_point_received"}}});
            }

            if (sessionId >= 0 && message.contains(QLatin1String("Passive Skill Points."))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_skill_points_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "passive_skill_points_received"}}});
            }

            if (sessionId >= 0 && message.contains(QLatin1String("Passive Respec Points"))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_respec_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "passive_respec_received"}}});
            }

            // Fires twice: once after Act 5 Kitava (-30%) and once after Act 10 Kitava (-60%).
            if (sessionId >= 0 && message.contains(QLatin1String("Kitava's merciless affliction"))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "kitava_resistance_penalty", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "kitava_resistance_penalty"}}});
            }

            if (sessionId >= 0 && message.contains(QLatin1String("There has been a patch that you need to update to."))) {
                sqlite3_bind_int64(generalEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (generalEventStmt, 2);
                else                   sqlite3_bind_int64(generalEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (generalEventStmt, 3, "patch_required", -1, SQLITE_STATIC);
                sqlite3_bind_text (generalEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(generalEventStmt);
                insertEvent(tsBytes, "general");
                sqlite3_reset(generalEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::GeneralEvent, ts, {{"event_type", "patch_required"}}});
            }

            if (sessionId >= 0 && message.contains(QLatin1String("Not logged in to steam. Achievements will not work"))) {
                static const QByteArray kSteamNotLoggedIn("steam_not_logged_in");
                sqlite3_bind_text(achievUpsertStmt, 1, kSteamNotLoggedIn.constData(), kSteamNotLoggedIn.size(), SQLITE_STATIC);
                sqlite3_step(achievUpsertStmt);
                sqlite3_reset(achievUpsertStmt);

                sqlite3_bind_text(achievSelectStmt, 1, kSteamNotLoggedIn.constData(), kSteamNotLoggedIn.size(), SQLITE_STATIC);
                qint64 achievId = -1;
                if (sqlite3_step(achievSelectStmt) == SQLITE_ROW)
                    achievId = sqlite3_column_int64(achievSelectStmt, 0);
                sqlite3_reset(achievSelectStmt);

                if (achievId >= 0) {
                    sqlite3_bind_int64(achievEventStmt, 1, sessionId);
                    sqlite3_bind_int64(achievEventStmt, 2, achievId);
                    sqlite3_bind_text (achievEventStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(achievEventStmt);
                    insertEvent(tsBytes, "achievement");
                    sqlite3_reset(achievEventStmt);
                }
            }

            if (sessionId >= 0 && message.contains(QLatin1String("InstanceClientLabyrinthCraftResultOptionsList recieved"))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "labyrinth_craft_options_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                insertEvent(tsBytes, "quest");
                sqlite3_reset(questEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::QuestEvent, ts, {{"event_type", "labyrinth_craft_options_received"}}});
            }

            if (sessionId >= 0) {
                const auto rulesetM = rulesetFailedRe.match(message);
                if (rulesetM.hasMatch()) {
                    const QByteArray nameBytes = rulesetM.captured(1).toUtf8();
                    sqlite3_bind_int64(rulesetFailedStmt, 1, sessionId);
                    if (sessionAreaId < 0) sqlite3_bind_null (rulesetFailedStmt, 2);
                    else                   sqlite3_bind_int64(rulesetFailedStmt, 2, sessionAreaId);
                    sqlite3_bind_text (rulesetFailedStmt, 3, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text (rulesetFailedStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(rulesetFailedStmt);
                    insertEvent(tsBytes, "ruleset_failed");
                    sqlite3_reset(rulesetFailedStmt);
                }
            }

            if (sessionId >= 0) {
                const auto petM = talkingPetRe.match(message);
                if (petM.hasMatch()) {
                    const QByteArray nameBytes = petM.captured(1).toUtf8();
                    sqlite3_bind_int64(questEventStmt, 1, sessionId);
                    if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                    else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                    sqlite3_bind_text (questEventStmt, 3, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(questEventStmt);
                    insertEvent(tsBytes, "quest");
                    sqlite3_reset(questEventStmt);
                }
            }

            // /played command — total in-game time on the current character
            if (playedRe.match(message).hasMatch() && sessionId >= 0) {
                qint64 playedSecs = 0;
                auto unitIt = playedUnitRe.globalMatch(message);
                while (unitIt.hasNext()) {
                    const auto m  = unitIt.next();
                    const qint64 val = m.captured(1).toLongLong();
                    const QChar  u   = m.captured(2).at(0);
                    if      (u == QLatin1Char('h')) playedSecs += val * 3600;
                    else if (u == QLatin1Char('m')) playedSecs += val * 60;
                    else                            playedSecs += val;
                }

                sqlite3_bind_int64(playedEventStmt, 1, sessionId);
                if (currentSpanId < 0) sqlite3_bind_null (playedEventStmt, 2);
                else                   sqlite3_bind_int64(playedEventStmt, 2, currentSpanId);
                sqlite3_bind_int64(playedEventStmt, 3, playedSecs);
                sqlite3_bind_text (playedEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(playedEventStmt);
                sqlite3_reset(playedEventStmt);

                // If we already know the character, update played_secs immediately.
                if (sessionCharId >= 0) {
                    sqlite3_bind_int64(charPlayedStmt, 1, playedSecs);
                    sqlite3_bind_int64(charPlayedStmt, 2, sessionCharId);
                    sqlite3_step(charPlayedStmt);
                    sqlite3_reset(charPlayedStmt);
                }
            }

            // Achievement unlocked
            const auto achievM = achievementRe.match(message);
            if (achievM.hasMatch() && sessionId >= 0) {
                const QByteArray codeBytes = achievM.captured(1).toUtf8();

                sqlite3_bind_text(achievUpsertStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                sqlite3_step(achievUpsertStmt);
                sqlite3_reset(achievUpsertStmt);

                sqlite3_bind_text(achievSelectStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                qint64 achievId = -1;
                if (sqlite3_step(achievSelectStmt) == SQLITE_ROW)
                    achievId = sqlite3_column_int64(achievSelectStmt, 0);
                sqlite3_reset(achievSelectStmt);

                if (achievId >= 0) {
                    sqlite3_bind_int64(achievEventStmt, 1, sessionId);
                    sqlite3_bind_int64(achievEventStmt, 2, achievId);
                    sqlite3_bind_text (achievEventStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(achievEventStmt);
                    insertEvent(tsBytes, "achievement");
                    sqlite3_reset(achievEventStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::Achievement, ts, {{"name", achievM.captured(1)}}});
                }
            }

            // Hideout discovered
            const auto hideoutM = hideoutRe.match(message);
            if (hideoutM.hasMatch() && sessionId >= 0) {
                const QByteArray nameBytes = hideoutM.captured(1).trimmed().toUtf8();

                sqlite3_bind_text(hideoutUpsertStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                sqlite3_step(hideoutUpsertStmt);
                sqlite3_reset(hideoutUpsertStmt);

                sqlite3_bind_text(hideoutSelectStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                qint64 hideoutId = -1;
                if (sqlite3_step(hideoutSelectStmt) == SQLITE_ROW)
                    hideoutId = sqlite3_column_int64(hideoutSelectStmt, 0);
                sqlite3_reset(hideoutSelectStmt);

                if (hideoutId >= 0) {
                    sqlite3_bind_int64(hideoutEventStmt, 1, sessionId);
                    sqlite3_bind_int64(hideoutEventStmt, 2, hideoutId);
                    if (sessionAreaId < 0) sqlite3_bind_null (hideoutEventStmt, 3);
                    else                   sqlite3_bind_int64(hideoutEventStmt, 3, sessionAreaId);
                    sqlite3_bind_text (hideoutEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(hideoutEventStmt);
                    insertEvent(tsBytes, "hideout");
                    sqlite3_reset(hideoutEventStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::HideoutDiscovered, ts, {{"name", hideoutM.captured(1).trimmed()}}});
                }
            }

            // PVP queue
            const auto pvpM = pvpQueueRe.match(message);
            if (pvpM.hasMatch() && sessionId >= 0) {
                const QByteArray nameBytes = pvpM.captured(1).toUtf8();
                const int playerCount = pvpM.captured(2).toInt();

                sqlite3_bind_text(pvpMatchUpsertStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                sqlite3_step(pvpMatchUpsertStmt);
                sqlite3_reset(pvpMatchUpsertStmt);

                sqlite3_bind_text(pvpMatchSelectStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                qint64 matchId = -1;
                if (sqlite3_step(pvpMatchSelectStmt) == SQLITE_ROW)
                    matchId = sqlite3_column_int64(pvpMatchSelectStmt, 0);
                sqlite3_reset(pvpMatchSelectStmt);

                if (matchId >= 0) {
                    sqlite3_bind_int64(pvpQueueEventStmt, 1, sessionId);
                    sqlite3_bind_int64(pvpQueueEventStmt, 2, matchId);
                    sqlite3_bind_int  (pvpQueueEventStmt, 3, playerCount);
                    sqlite3_bind_text (pvpQueueEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(pvpQueueEventStmt);
                    if (sqlite3_changes(db) > 0)
                        lastPvpQueueEventId = sqlite3_last_insert_rowid(db);
                    insertEvent(tsBytes, "pvp_queue");
                    sqlite3_reset(pvpQueueEventStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::PvpQueue, ts, {
                            {"match_name",   pvpM.captured(1)},
                            {"other_players", playerCount}
                        }});
                }
            }

            // PVP queue cancelled
            if (message.contains(QLatin1String("Cancelled PVP queue")) && lastPvpQueueEventId >= 0) {
                sqlite3_bind_text (pvpQueueCancelStmt, 1, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_bind_int64(pvpQueueCancelStmt, 2, lastPvpQueueEventId);
                sqlite3_step(pvpQueueCancelStmt);
                sqlite3_reset(pvpQueueCancelStmt);
                lastPvpQueueEventId = -1;
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::PvpQueueCancelled, ts, {}});
            }

            // Passive skill allocation / unallocation
            const auto passiveM = passiveAllocRe.match(message);
            if (passiveM.hasMatch() && sessionId >= 0) {
                const QByteArray actionBytes = passiveM.captured(1).toLower().toUtf8();
                const QByteArray codeBytes   = passiveM.captured(2).toUtf8();
                const QByteArray nameBytes   = passiveM.captured(3).toUtf8();

                sqlite3_bind_text(passiveUpsertStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                sqlite3_bind_text(passiveUpsertStmt, 2, nameBytes.constData(),  nameBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_int (passiveUpsertStmt, 3, 0);
                sqlite3_step(passiveUpsertStmt);
                sqlite3_reset(passiveUpsertStmt);

                sqlite3_bind_text(passiveSelectStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                qint64 passiveId = -1;
                if (sqlite3_step(passiveSelectStmt) == SQLITE_ROW)
                    passiveId = sqlite3_column_int64(passiveSelectStmt, 0);
                sqlite3_reset(passiveSelectStmt);

                if (passiveId >= 0) {
                    sqlite3_bind_int64(passiveAllocStmt, 1, sessionId);
                    if (sessionCharId < 0) sqlite3_bind_null (passiveAllocStmt, 2);
                    else                   sqlite3_bind_int64(passiveAllocStmt, 2, sessionCharId);
                    sqlite3_bind_int64(passiveAllocStmt, 3, passiveId);
                    sqlite3_bind_text (passiveAllocStmt, 4, actionBytes.constData(), actionBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text (passiveAllocStmt, 5, tsBytes.constData(),     tsBytes.size(),     SQLITE_STATIC);
                    sqlite3_step(passiveAllocStmt);
                    insertEvent(tsBytes, "passive_alloc");
                    sqlite3_reset(passiveAllocStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                        const bool alloc = (passiveM.captured(1).toLower() == QLatin1String("allocated"));
                        emit liveEventParsed(LiveEvent{
                            alloc ? LiveEventType::PassiveAllocated : LiveEventType::PassiveUnallocated,
                            ts,
                            {{"skill_id", passiveM.captured(2)}, {"skill_name", passiveM.captured(3)}, {"is_mastery", false}}
                        });
                    }
                }
            }

            // Mastery allocation / unallocation
            const auto masteryM = masteryAllocRe.match(message);
            if (masteryM.hasMatch() && sessionId >= 0) {
                const QByteArray actionBytes = masteryM.captured(1).toLower().toUtf8();
                const QByteArray codeBytes   = masteryM.captured(2).toUtf8();
                const QByteArray nameBytes   = masteryM.captured(3).toUtf8();

                sqlite3_bind_text(passiveUpsertStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                sqlite3_bind_text(passiveUpsertStmt, 2, nameBytes.constData(),  nameBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_int (passiveUpsertStmt, 3, 1);
                sqlite3_step(passiveUpsertStmt);
                sqlite3_reset(passiveUpsertStmt);

                sqlite3_bind_text(passiveSelectStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                qint64 passiveId = -1;
                if (sqlite3_step(passiveSelectStmt) == SQLITE_ROW)
                    passiveId = sqlite3_column_int64(passiveSelectStmt, 0);
                sqlite3_reset(passiveSelectStmt);

                if (passiveId >= 0) {
                    sqlite3_bind_int64(passiveAllocStmt, 1, sessionId);
                    if (sessionCharId < 0) sqlite3_bind_null (passiveAllocStmt, 2);
                    else                   sqlite3_bind_int64(passiveAllocStmt, 2, sessionCharId);
                    sqlite3_bind_int64(passiveAllocStmt, 3, passiveId);
                    sqlite3_bind_text (passiveAllocStmt, 4, actionBytes.constData(), actionBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text (passiveAllocStmt, 5, tsBytes.constData(),     tsBytes.size(),     SQLITE_STATIC);
                    sqlite3_step(passiveAllocStmt);
                    insertEvent(tsBytes, "passive_alloc");
                    sqlite3_reset(passiveAllocStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                        const bool alloc = (masteryM.captured(1).toLower() == QLatin1String("allocated"));
                        emit liveEventParsed(LiveEvent{
                            alloc ? LiveEventType::PassiveAllocated : LiveEventType::PassiveUnallocated,
                            ts,
                            {{"skill_id", masteryM.captured(2)}, {"skill_name", masteryM.captured(3)}, {"is_mastery", true}}
                        });
                    }
                }
            }

            // Whispers
            const auto whisperM = whisperRe.match(message);
            if (whisperM.hasMatch() && sessionId >= 0) {
                const bool       isFrom     = (whisperM.captured(1) == QLatin1String("From"));
                const QByteArray dirBytes   = isFrom ? QByteArrayLiteral("from") : QByteArrayLiteral("to");
                const QString    guildTag   = whisperM.captured(2);
                const QByteArray guildBytes = guildTag.toUtf8();
                const QByteArray nameBytes  = whisperM.captured(3).toUtf8();
                const QByteArray msgBytes   = whisperM.captured(4).toUtf8();

                qint64 guildId = -1;
                if (!guildTag.isEmpty()) {
                    sqlite3_bind_text(guildUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                    sqlite3_step(guildUpsertStmt);
                    sqlite3_reset(guildUpsertStmt);
                    sqlite3_bind_text(guildSelectStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                    if (sqlite3_step(guildSelectStmt) == SQLITE_ROW)
                        guildId = sqlite3_column_int64(guildSelectStmt, 0);
                    sqlite3_reset(guildSelectStmt);
                }

                sqlite3_bind_int64(whisperStmt, 1, sessionId);
                sqlite3_bind_text (whisperStmt, 2, dirBytes.constData(),  dirBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_text (whisperStmt, 3, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                if (guildId < 0) sqlite3_bind_null (whisperStmt, 4);
                else             sqlite3_bind_int64(whisperStmt, 4, guildId);
                sqlite3_bind_text (whisperStmt, 5, msgBytes.constData(),  msgBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_text (whisperStmt, 6, tsBytes.constData(),   tsBytes.size(),   SQLITE_STATIC);
                sqlite3_step(whisperStmt);
                insertEvent(tsBytes, "whisper");
                sqlite3_reset(whisperStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                    if (!isFrom) clearAltTab(ts); // sending a whisper = player is focused
                    emit liveEventParsed(LiveEvent{LiveEventType::Whisper, ts, {
                        {"direction", isFrom ? QString("from") : QString("to")},
                        {"player",    whisperM.captured(3)},
                        {"message",   whisperM.captured(4)}
                    }});
                }
            }

            // Character death
            const auto deathM = deathRe.match(message);
            if (deathM.hasMatch() && sessionId >= 0) {
                const QByteArray charNameBytes = deathM.captured(1).toUtf8();

                sqlite3_bind_text(charSelectStmt, 1, charNameBytes.constData(), charNameBytes.size(), SQLITE_STATIC);
                qint64 deadCharId = -1;
                if (sqlite3_step(charSelectStmt) == SQLITE_ROW)
                    deadCharId = sqlite3_column_int64(charSelectStmt, 0);
                sqlite3_reset(charSelectStmt);

                if (deadCharId >= 0) {
                    sqlite3_bind_int64(deathStmt, 1, sessionId);
                    sqlite3_bind_int64(deathStmt, 2, deadCharId);
                    if (sessionAreaId < 0) sqlite3_bind_null (deathStmt, 3);
                    else                   sqlite3_bind_int64(deathStmt, 3, sessionAreaId);
                    if (deadCharId == sessionCharId && sessionCharLevel > 0)
                        sqlite3_bind_int(deathStmt, 4, sessionCharLevel);
                    else
                        sqlite3_bind_null(deathStmt, 4);
                    sqlite3_bind_text(deathStmt, 5, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                    sqlite3_step(deathStmt);
                    insertEvent(tsBytes, "death");
                    sqlite3_reset(deathStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                        emit liveEventParsed(LiveEvent{LiveEventType::CharacterDeath, ts, {{"character", deathM.captured(1)}}});
                }
            }

            // Public chat (#global $trade %party &guild)
            const auto chatM = chatRe.match(message);
            if (chatM.hasMatch() && sessionId >= 0) {
                const QByteArray channelBytes  = chatM.captured(1).toUtf8();
                const QString    guildTag      = chatM.captured(2);
                const QByteArray guildBytes    = guildTag.toUtf8();
                const QByteArray speakerBytes  = chatM.captured(3).toUtf8();
                const QByteArray messageBytes  = chatM.captured(4).toUtf8();

                qint64 guildId = -1;
                if (!guildTag.isEmpty()) {
                    sqlite3_bind_text(guildUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                    sqlite3_step(guildUpsertStmt);
                    sqlite3_reset(guildUpsertStmt);
                    sqlite3_bind_text(guildSelectStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                    if (sqlite3_step(guildSelectStmt) == SQLITE_ROW)
                        guildId = sqlite3_column_int64(guildSelectStmt, 0);
                    sqlite3_reset(guildSelectStmt);
                }

                sqlite3_bind_text(pubCharUpsertStmt, 1, speakerBytes.constData(), speakerBytes.size(), SQLITE_STATIC);
                sqlite3_step(pubCharUpsertStmt);
                sqlite3_reset(pubCharUpsertStmt);

                sqlite3_bind_text(pubCharSelectStmt, 1, speakerBytes.constData(), speakerBytes.size(), SQLITE_STATIC);
                qint64 pubCharId = -1;
                if (sqlite3_step(pubCharSelectStmt) == SQLITE_ROW)
                    pubCharId = sqlite3_column_int64(pubCharSelectStmt, 0);
                sqlite3_reset(pubCharSelectStmt);

                if (pubCharId >= 0) {
                    if (guildId >= 0) {
                        sqlite3_bind_int64(pubCharGuildStmt, 1, guildId);
                        sqlite3_bind_int64(pubCharGuildStmt, 2, pubCharId);
                        sqlite3_step(pubCharGuildStmt);
                        sqlite3_reset(pubCharGuildStmt);
                    }

                    sqlite3_bind_int64(chatStmt, 1, sessionId);
                    sqlite3_bind_int64(chatStmt, 2, pubCharId);
                    sqlite3_bind_text (chatStmt, 3, channelBytes.constData(), channelBytes.size(), SQLITE_STATIC);
                    if (guildId < 0) sqlite3_bind_null (chatStmt, 4);
                    else             sqlite3_bind_int64(chatStmt, 4, guildId);
                    sqlite3_bind_text (chatStmt, 5, messageBytes.constData(), messageBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_text (chatStmt, 6, tsBytes.constData(),      tsBytes.size(),      SQLITE_STATIC);
                    sqlite3_step(chatStmt);
                    sqlite3_reset(chatStmt);
                    if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                        QVariantMap chatData;
                        chatData[QStringLiteral("channel")] = chatM.captured(1);
                        chatData[QStringLiteral("player")]  = chatM.captured(3);
                        chatData[QStringLiteral("message")] = chatM.captured(4);
                        if (!guildTag.isEmpty())
                            chatData[QStringLiteral("guild_tag")] = guildTag;
                        emit liveEventParsed(LiveEvent{LiveEventType::Chat, ts, chatData});
                    }
                }
            }

            // Area entered (correlated with pending Generating line)
            if (!pendingCode.isEmpty()
                && locState != LocState::LoginScreen
                && locState != LocState::ConnectingFromLogin
                && locState != LocState::ConnectingFromZone
                && locState != LocState::AwaitingScene) {
                const auto entM = enteredRe.match(message);
                if (entM.hasMatch()) {
                    const QByteArray codeBytes = pendingCode.toUtf8();
                    const QByteArray nameBytes = entM.captured(1).toUtf8();

                    sqlite3_bind_text(areaUpsertStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                    sqlite3_bind_int (areaUpsertStmt, 2, pendingLevel);
                    sqlite3_bind_text(areaUpsertStmt, 3, nameBytes.constData(),  nameBytes.size(),  SQLITE_STATIC);
                    sqlite3_step(areaUpsertStmt);
                    sqlite3_reset(areaUpsertStmt);

                    sqlite3_bind_text(areaSelectStmt, 1, codeBytes.constData(), codeBytes.size(), SQLITE_STATIC);
                    qint64 areaId = -1;
                    QString areaType;
                    QString areaSubtype;
                    if (sqlite3_step(areaSelectStmt) == SQLITE_ROW) {
                        areaId = sqlite3_column_int64(areaSelectStmt, 0);
                        if (auto *p = sqlite3_column_text(areaSelectStmt, 1))
                            areaType = QString::fromUtf8(reinterpret_cast<const char *>(p));
                        if (auto *p = sqlite3_column_text(areaSelectStmt, 2))
                            areaSubtype = QString::fromUtf8(reinterpret_cast<const char *>(p));
                    }
                    sqlite3_reset(areaSelectStmt);

                    if (areaId >= 0) {
                        sqlite3_bind_int64(moveInsertStmt, 1, m_installId);
                        sqlite3_bind_int64(moveInsertStmt, 2, areaId);
                        sqlite3_bind_text (moveInsertStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                        sqlite3_step(moveInsertStmt);
                        sqlite3_reset(moveInsertStmt);
                        ++totalVisits;
                        safeCommitPos = lineStartPos;
                        sessionAreaId = areaId;

                        // Close the previous span (char select or prior area) and open one for this area.
                        closeSpan(ts);
                        openSpan(ts, areaId);
                        locState = LocState::InZone;

                        if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                            clearAltTab(ts);
                            emit liveEventParsed(LiveEvent{LiveEventType::AreaEntered, ts, {
                                {"area_name",    entM.captured(1)},
                                {"area_code",    pendingCode},
                                {"area_level",   pendingLevel},
                                {"area_type",    areaType},
                                {"area_subtype", areaSubtype}
                            }});
                        }
                    }

                    pendingCode.clear();
                }
            }

            // [SCENE] Set Source — login screen marker or fallback area transition
            {
                const auto sceneM = sceneSourceRe.match(message);
                if (sceneM.hasMatch()) {
                    const QString sceneName = sceneM.captured(1);
                    if (sceneName == QLatin1String("(unknown)")) {
                        const bool toCharSelect = (locState == LocState::AwaitingScene);
                        const char *screenType  = toCharSelect ? "char_select" : "login_screen";
                        sqlite3_bind_int64(clientScreenEventStmt, 1, m_installId);
                        sqlite3_bind_text (clientScreenEventStmt, 2, screenType, -1, SQLITE_STATIC);
                        sqlite3_bind_text (clientScreenEventStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                        sqlite3_step(clientScreenEventStmt);
                        insertEvent(tsBytes, "client_screen");
                        sqlite3_reset(clientScreenEventStmt);
                        pendingCode.clear();
                        if (toCharSelect) {
                            locState = LocState::CharSelect;
                            if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                                emit liveEventParsed(LiveEvent{LiveEventType::CharSelect, ts, {}});
                        } else {
                            locState = LocState::LoginScreen;
                            if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                                emit liveEventParsed(LiveEvent{LiveEventType::LoginScreen, ts, {}});
                        }
                    } else if (sceneName != QLatin1String("(null)") && pendingCode.isEmpty() && sessionId >= 0
                               && locState != LocState::LoginScreen
                               && locState != LocState::ConnectingFromLogin
                               && locState != LocState::ConnectingFromZone
                               && locState != LocState::AwaitingScene) {
                        // Fallback area transition when no Generating level preceded it
                        const QByteArray nameBytes = sceneName.toUtf8();

                        sqlite3_bind_text(areaInsertIgnoreStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                        sqlite3_bind_text(areaInsertIgnoreStmt, 2, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                        sqlite3_step(areaInsertIgnoreStmt);
                        sqlite3_reset(areaInsertIgnoreStmt);

                        sqlite3_bind_text(areaSelectStmt, 1, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                        qint64 areaId = -1;
                        if (sqlite3_step(areaSelectStmt) == SQLITE_ROW)
                            areaId = sqlite3_column_int64(areaSelectStmt, 0);
                        sqlite3_reset(areaSelectStmt);

                        if (areaId >= 0) {
                            sqlite3_bind_int64(moveInsertStmt, 1, m_installId);
                            sqlite3_bind_int64(moveInsertStmt, 2, areaId);
                            sqlite3_bind_text (moveInsertStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                            sqlite3_step(moveInsertStmt);
                            sqlite3_reset(moveInsertStmt);
                            ++totalVisits;
                            safeCommitPos = lineStartPos;
                            sessionAreaId = areaId;

                            closeSpan(ts);
                            openSpan(ts, areaId);
                            locState = LocState::InZone;

                            if (caughtUp && m_liveMode.load(std::memory_order_relaxed)) {
                                clearAltTab(ts);
                                emit liveEventParsed(LiveEvent{LiveEventType::AreaEntered, ts, {
                                    {"area_name",  sceneName},
                                    {"area_code",  sceneName},
                                    {"area_level", 0}
                                }});
                            }
                        }
                    }
                }
            }

            // Character select screen transitions (see flow diagram above).
            if (locState == LocState::LoginScreen && message.contains(QLatin1String("Async connecting to ")))
                locState = LocState::ConnectingFromLogin;
            else if (locState == LocState::InZone && message.contains(QLatin1String("Async connecting to ")))
                locState = LocState::ConnectingFromZone;

            if (locState == LocState::ConnectingFromLogin && message.contains(QLatin1String("Connected to "))) {
                locState = LocState::CharSelect;
                sqlite3_bind_int64(clientScreenEventStmt, 1, m_installId);
                sqlite3_bind_text (clientScreenEventStmt, 2, "char_select", -1, SQLITE_STATIC);
                sqlite3_bind_text (clientScreenEventStmt, 3, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(clientScreenEventStmt);
                insertEvent(tsBytes, "client_screen");
                sqlite3_reset(clientScreenEventStmt);
                if (caughtUp && m_liveMode.load(std::memory_order_relaxed))
                    emit liveEventParsed(LiveEvent{LiveEventType::CharSelect, ts, {}});
            } else if (locState == LocState::ConnectingFromZone && message.contains(QLatin1String("Connected to ")))
                locState = LocState::AwaitingScene;
        }

        if (++chunkCount >= kChunkSize) {
            const qint64 commitStart = wallClock.elapsed();
            flushSource(safeCommitPos);
            execSql(db, "COMMIT;");
            ++chunkCommits;
            totalSize = qMax(totalSize, file.size());
            const int pct = totalSize > 0
                ? static_cast<int>((file.pos() * 100LL) / totalSize) : 0;
            qDebug() << "[ingest] commit"
                     << pct << "% pos=" << file.pos() << "/" << totalSize
                     << "visits=" << totalVisits
                     << "commit_ms=" << (wallClock.elapsed() - commitStart)
                     << "elapsed_ms=" << wallClock.elapsed();
            emit progress(pct, QStringLiteral("%1 events").arg(totalVisits));
            chunkCount = 0;
            execSql(db, "BEGIN;");
        }
    }

    flushPassives();
    // Never close the session here — closeOrphanSessions handles closure at startup
    // for installs where the game is no longer running.  Closing here would kill the
    // open session whenever the user restarts the app while the game is still running.
    flushSource(file.pos());
    {
        const qint64 commitStart = wallClock.elapsed();
        execSql(db, "COMMIT;");
        qDebug() << "[ingest] final commit_ms=" << (wallClock.elapsed() - commitStart);
    }

    sqlite3_finalize(areaUpsertStmt);
    sqlite3_finalize(areaInsertIgnoreStmt);
    sqlite3_finalize(areaSelectStmt);
    sqlite3_finalize(moveInsertStmt);
    sqlite3_finalize(accountUpsertStmt);
    sqlite3_finalize(guildMemberStmt);
    sqlite3_finalize(guildMemberInsertStmt);
    sqlite3_finalize(channelUpsertStmt);
    sqlite3_finalize(channelSelectStmt);
    sqlite3_finalize(channelJoinStmt);
    sqlite3_finalize(classUpsertStmt);
    sqlite3_finalize(classSelectStmt);
    sqlite3_finalize(charUpsertStmt);
    sqlite3_finalize(charSelectStmt);
    sqlite3_finalize(levelEventStmt);
    sqlite3_finalize(sessionInsertStmt);
    sqlite3_finalize(sessionSelectStmt);
    sqlite3_finalize(sessionCloseStmt);
    sqlite3_finalize(afkStmt);
    sqlite3_finalize(spanInsertStmt);
    sqlite3_finalize(spanSelectStmt);
    sqlite3_finalize(spanCloseStmt);
    sqlite3_finalize(spanUpdateCharStmt);
    sqlite3_finalize(questEventStmt);
    sqlite3_finalize(passiveUpsertStmt);
    sqlite3_finalize(passiveSelectStmt);
    sqlite3_finalize(passiveAllocStmt);
    sqlite3_finalize(whisperStmt);
    sqlite3_finalize(deathStmt);
    sqlite3_finalize(guildUpsertStmt);
    sqlite3_finalize(guildSelectStmt);
    sqlite3_finalize(pubCharUpsertStmt);
    sqlite3_finalize(pubCharSelectStmt);
    sqlite3_finalize(pubCharGuildStmt);
    sqlite3_finalize(chatStmt);
    sqlite3_finalize(achievUpsertStmt);
    sqlite3_finalize(achievSelectStmt);
    sqlite3_finalize(achievEventStmt);
    sqlite3_finalize(hideoutUpsertStmt);
    sqlite3_finalize(hideoutSelectStmt);
    sqlite3_finalize(hideoutEventStmt);
    sqlite3_finalize(pvpMatchUpsertStmt);
    sqlite3_finalize(pvpMatchSelectStmt);
    sqlite3_finalize(pvpQueueEventStmt);
    sqlite3_finalize(pvpQueueCancelStmt);
    sqlite3_finalize(playedEventStmt);
    sqlite3_finalize(charPlayedStmt);
    sqlite3_finalize(charPlayedFromSpanStmt);
    sqlite3_finalize(passSnapInsertStmt);
    sqlite3_finalize(passSnapSelectStmt);
    sqlite3_finalize(passQuestUpsertStmt);
    sqlite3_finalize(passQuestSelectStmt);
    sqlite3_finalize(passSnapQuestStmt);
    sqlite3_finalize(rulesetFailedStmt);
    sqlite3_finalize(generalEventStmt);
    sqlite3_finalize(clientScreenEventStmt);
    sqlite3_finalize(sourceStmt);
    sqlite3_finalize(eventInsertStmt);
    sqlite3_close(db);

    qDebug() << "[ingest] done total_ms=" << wallClock.elapsed()
             << "visits=" << totalVisits
             << "avg_chunk_mb=" << QString::number(
                    chunkCommits > 0 ? ((file.pos() - ingestStart) / 1048576.0) / chunkCommits : 0.0, 'f', 2);
    emit progress(100, QStringLiteral("%1 events").arg(totalVisits));
    emit finished();
}
