#include "LogIngestWorker.h"

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
    sqlite3_stmt *sourceStmt          = nullptr;

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
        "UPDATE installs SET "
        "file_created_at=?, file_modified_at=?, file_size=?, last_byte_offset=? "
        "WHERE id=?;",
        -1, &sourceStmt, nullptr);

    auto flushSource = [&](qint64 offset) {
        sqlite3_bind_int64(sourceStmt, 1, fileCreatedAt);
        sqlite3_bind_int64(sourceStmt, 2, fileModifiedAt);
        sqlite3_bind_int64(sourceStmt, 3, fileSize);
        sqlite3_bind_int64(sourceStmt, 4, offset);
        sqlite3_bind_int64(sourceStmt, 5, m_installId);
        sqlite3_step(sourceStmt);
        sqlite3_reset(sourceStmt);
    };

    // Pending state: set when we see a Generating line, cleared on the matching entered line.
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

        const QString level   = hdr.captured(2);
        const QString message = hdr.captured(4).trimmed();

        if (level == QLatin1String("DEBUG")) {
            const auto genM = generatingRe.match(message);
            if (genM.hasMatch()) {
                pendingLevel = genM.captured(1).toInt();
                pendingCode  = genM.captured(2);
            }
        } else if (level == QLatin1String("INFO")) {
            QString ts = hdr.captured(1);
            ts[4] = '-'; ts[7] = '-';   // 2026/06/03 → 2026-06-03
            const QByteArray tsBytes = ts.toUtf8();

            const auto guildM = guildRe.match(message);
            if (guildM.hasMatch()) {
                const QByteArray guildBytes = guildM.captured(1).toUtf8();
                sqlite3_bind_text(accountUpsertStmt, 1, guildBytes.constData(), guildBytes.size(), SQLITE_STATIC);
                sqlite3_step(accountUpsertStmt);
                sqlite3_reset(accountUpsertStmt);
            }

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

                if (classId < 0) continue;

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
                }
            }

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
    sqlite3_finalize(sourceStmt);
    sqlite3_close(db);

    emit progress(100, QStringLiteral("%1 area visits").arg(totalVisits));
    emit finished();
}
