#include "LogIngestWorker.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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
                                 QObject *parent)
    : BackgroundWorker(parent)
    , m_dbPath(dbPath)
    , m_installId(installId)
    , m_logPath(logPath)
    , m_resumeOffset(resumeOffset)
    , m_channelNames(channelNames)
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

    const qint64 totalSize = file.size();
    if (m_resumeOffset > 0 && m_resumeOffset < totalSize)
        file.seek(m_resumeOffset);

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
    // [INFO] Joined guild named Unicorns with 5 members
    static const QRegularExpression guildRe(
        R"(Joined guild named (.+?) with \d+ members)"
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
    // [INFO] @From YaYtOtEmZ: hey  /  @To YaYtOtEmZ: hello
    static const QRegularExpression whisperRe(
        R"(@(From|To) ([^:]+): (.*))"
    );
    // [INFO] Successfully (allocated|unallocated) passive skill id: accuracy581, name: Projectile Damage and Attack Speed
    static const QRegularExpression passiveAllocRe(
        R"(Successfully (allocated|unallocated) passive skill id: ([^,]+), name: (.+))"
    );
    // [INFO] Successfully (allocated|unallocated) mastery effect id: 48385, mastery: mastery_elemental99, name: Elemental Mastery
    static const QRegularExpression masteryAllocRe(
        R"(Successfully (allocated|unallocated) mastery effect id: ([^,]+), mastery: [^,]+, name: (.+))"
    );

    // ── prepared statements ──────────────────────────────────────────────────

    sqlite3_stmt *areaUpsertStmt      = nullptr;
    sqlite3_stmt *areaSelectStmt      = nullptr;
    sqlite3_stmt *moveInsertStmt      = nullptr;
    sqlite3_stmt *accountUpsertStmt   = nullptr;
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
    sqlite3_stmt *sourceStmt           = nullptr;

    sqlite3_prepare_v2(db,
        "INSERT INTO areas(code, level, display_name) VALUES(?,?,?) "
        "ON CONFLICT(code) DO UPDATE SET level=excluded.level, display_name=excluded.display_name;",
        -1, &areaUpsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "SELECT id FROM areas WHERE code=?;",
        -1, &areaSelectStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO area_moves(install_id, area_id, entered_at) VALUES(?,?,?);",
        -1, &moveInsertStmt, nullptr);
    sqlite3_prepare_v2(db,
        "INSERT INTO accounts(name, guild_name) VALUES('unknown', ?) "
        "ON CONFLICT(name) DO UPDATE SET guild_name=excluded.guild_name;",
        -1, &accountUpsertStmt, nullptr);
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
        "INSERT OR IGNORE INTO whispers(session_id, direction, player_name, message, occurred_at) VALUES(?,?,?,?,?);",
        -1, &whisperStmt, nullptr);
    sqlite3_prepare_v2(db,
        "UPDATE installs SET "
        "file_created_at=?, file_modified_at=?, file_size=?, last_byte_offset=? "
        "WHERE id=?;",
        -1, &sourceStmt, nullptr);

    // ── helpers ──────────────────────────────────────────────────────────────

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
    qint64  sessionCharId     = -1;
    qint64  sessionAreaId     = -1;

    qint64  currentSpanId     = -1;
    qint64  currentSpanAfkSecs = 0;

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
        if (sessionId < 0) return;

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
        sessionCharId  = -1;
        sessionAreaId  = -1;
    };

    // ── main loop ────────────────────────────────────────────────────────────

    QString pendingCode;
    int     pendingLevel  = 0;

    constexpr int kChunkSize    = 10'000;
    qint64        safeCommitPos = m_resumeOffset > 0 ? m_resumeOffset : 0;
    int           chunkCount    = 0;
    int           totalVisits   = 0;

    execSql(db, "BEGIN;");

    while (!file.atEnd() && !isCancelled()) {
        const qint64  lineStartPos = file.pos();
        const QString line         = QString::fromUtf8(file.readLine()).trimmed();

        const auto hdr = lineRe.match(line);
        if (!hdr.hasMatch()) continue;

        QString ts = hdr.captured(1);
        ts[4] = '-'; ts[7] = '-';   // 2026/06/03 → 2026-06-03
        const QByteArray tsBytes = ts.toUtf8();
        const QString prevTs = lastTs;
        lastTs = ts;

        const QString level   = hdr.captured(2);
        const QString message = hdr.captured(4).trimmed();

        // ── session boundary ─────────────────────────────────────────────────
        if (message.contains(QLatin1String("LOG FILE OPENING"))) {
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

            sessionStartTs = ts;
            sessionAfkSecs = 0;
            sessionCharId  = -1;
            sessionAreaId  = -1;

            // Session starts at char select — open a NULL-area span.
            openSpan(ts, -1);

        } else if (level == QLatin1String("DEBUG")) {
            const auto genM = generatingRe.match(message);
            if (genM.hasMatch()) {
                pendingLevel = genM.captured(1).toInt();
                pendingCode  = genM.captured(2);
            }

        } else if (level == QLatin1String("INFO")) {

            // Guild
            const auto guildM = guildRe.match(message);
            if (guildM.hasMatch()) {
                const QByteArray guildBytes = guildM.captured(1).toUtf8();
                sqlite3_bind_text(accountUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                sqlite3_step(accountUpsertStmt);
                sqlite3_reset(accountUpsertStmt);
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
                        sqlite3_reset(levelEventStmt);

                        sessionCharId = charId;

                        // Backfill char_id on the current open span now that we know it.
                        if (currentSpanId >= 0) {
                            sqlite3_bind_int64(spanUpdateCharStmt, 1, charId);
                            sqlite3_bind_int64(spanUpdateCharStmt, 2, currentSpanId);
                            sqlite3_step(spanUpdateCharStmt);
                            sqlite3_reset(spanUpdateCharStmt);
                        }
                    }
                }
            }

            // AFK toggle
            const auto afkM = afkRe.match(message);
            if (afkM.hasMatch()) {
                if (afkM.captured(1) == QLatin1String("ON")) {
                    afkOnTs = ts;
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
                sqlite3_reset(questEventStmt);
            }

            if (sessionId >= 0 && message.contains(QLatin1String("You have received a Passive Skill Point."))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_skill_point_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                sqlite3_reset(questEventStmt);
            }

            if (sessionId >= 0 && message.contains(QLatin1String("Passive Skill Points."))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_skill_points_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                sqlite3_reset(questEventStmt);
            }

            if (sessionId >= 0 && message.contains(QLatin1String("Passive Respec Points"))) {
                sqlite3_bind_int64(questEventStmt, 1, sessionId);
                if (sessionAreaId < 0) sqlite3_bind_null (questEventStmt, 2);
                else                   sqlite3_bind_int64(questEventStmt, 2, sessionAreaId);
                sqlite3_bind_text (questEventStmt, 3, "passive_respec_received", -1, SQLITE_STATIC);
                sqlite3_bind_text (questEventStmt, 4, tsBytes.constData(), tsBytes.size(), SQLITE_STATIC);
                sqlite3_step(questEventStmt);
                sqlite3_reset(questEventStmt);
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
                    sqlite3_reset(passiveAllocStmt);
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
                    sqlite3_reset(passiveAllocStmt);
                }
            }

            // Whispers
            const auto whisperM = whisperRe.match(message);
            if (whisperM.hasMatch() && sessionId >= 0) {
                const QByteArray dirBytes  = (whisperM.captured(1) == QLatin1String("From")
                                              ? QByteArrayLiteral("from")
                                              : QByteArrayLiteral("to"));
                const QByteArray nameBytes = whisperM.captured(2).toUtf8();
                const QByteArray msgBytes  = whisperM.captured(3).toUtf8();
                sqlite3_bind_int64(whisperStmt, 1, sessionId);
                sqlite3_bind_text (whisperStmt, 2, dirBytes.constData(),  dirBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_text (whisperStmt, 3, nameBytes.constData(), nameBytes.size(), SQLITE_STATIC);
                sqlite3_bind_text (whisperStmt, 4, msgBytes.constData(),  msgBytes.size(),  SQLITE_STATIC);
                sqlite3_bind_text (whisperStmt, 5, tsBytes.constData(),   tsBytes.size(),   SQLITE_STATIC);
                sqlite3_step(whisperStmt);
                sqlite3_reset(whisperStmt);
            }

            // Area entered (correlated with pending Generating line)
            if (!pendingCode.isEmpty()) {
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

                        // Close the previous span (char select or prior area) and open one for this area.
                        closeSpan(ts);
                        openSpan(ts, areaId);
                    }

                    pendingCode.clear();
                }
            }
        }

        if (++chunkCount >= kChunkSize) {
            flushSource(safeCommitPos);
            execSql(db, "COMMIT;");
            emit progress(
                totalSize > 0 ? static_cast<int>((file.pos() * 100LL) / totalSize) : 0,
                QStringLiteral("%1 area visits").arg(totalVisits));
            chunkCount = 0;
            execSql(db, "BEGIN;");
        }
    }

    closeSession(lastTs);
    flushSource(file.pos());
    execSql(db, "COMMIT;");

    sqlite3_finalize(areaUpsertStmt);
    sqlite3_finalize(areaSelectStmt);
    sqlite3_finalize(moveInsertStmt);
    sqlite3_finalize(accountUpsertStmt);
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
    sqlite3_finalize(sourceStmt);
    sqlite3_close(db);

    emit progress(100, QStringLiteral("%1 area visits").arg(totalVisits));
    emit finished();
}
