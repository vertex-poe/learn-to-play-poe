#include "LogIngestWorker.h"

#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <sqlite3.h>

static quint64 fnv1a64(const QByteArray &data)
{
    quint64 hash = 14695981039346656037ULL;
    for (unsigned char c : data) {
        hash ^= static_cast<quint64>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static void execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (err) sqlite3_free(err);
}

LogIngestWorker::LogIngestWorker(const QString &dbPath, qint64 installId,
                                 const QString &logPath, qint64 resumeOffset,
                                 QObject *parent)
    : BackgroundWorker(parent)
    , m_dbPath(dbPath)
    , m_installId(installId)
    , m_logPath(logPath)
    , m_resumeOffset(resumeOffset)
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

    // PoE log timestamp: "2024/12/01 10:15:32 ..."
    static const QRegularExpression tsRe(
        R"(^(\d{4})/(\d{2})/(\d{2}) (\d{2}:\d{2}:\d{2}) )");

    sqlite3_stmt *logStmt    = nullptr;
    sqlite3_stmt *sourceStmt = nullptr;
    sqlite3_prepare_v2(db,
        "INSERT OR IGNORE INTO logs(install_id, logged_at, line, line_hash) "
        "VALUES(?,?,?,?);",
        -1, &logStmt, nullptr);
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

    constexpr int kChunkSize = 10'000;
    int chunkCount = 0;
    int totalLines = 0;

    execSql(db, "BEGIN;");

    while (!file.atEnd() && !isCancelled()) {
        const QByteArray rawLine = file.readLine();
        const QString line = QString::fromUtf8(rawLine).trimmed();
        if (line.isEmpty()) continue;

        QString loggedAt;
        const auto m = tsRe.match(line);
        if (m.hasMatch())
            loggedAt = QStringLiteral("%1-%2-%3 %4")
                           .arg(m.captured(1), m.captured(2),
                                m.captured(3), m.captured(4));

        const QByteArray lineBytes     = line.toUtf8();
        const QByteArray loggedAtBytes = loggedAt.toUtf8();
        const qint64     hash = static_cast<qint64>(fnv1a64(lineBytes));

        sqlite3_bind_int64(logStmt, 1, m_installId);
        sqlite3_bind_text (logStmt, 2, loggedAtBytes.constData(), loggedAtBytes.size(), SQLITE_STATIC);
        sqlite3_bind_text (logStmt, 3, lineBytes.constData(),     lineBytes.size(),     SQLITE_STATIC);
        sqlite3_bind_int64(logStmt, 4, hash);
        sqlite3_step(logStmt);
        sqlite3_reset(logStmt);

        ++totalLines;
        ++chunkCount;

        if (chunkCount >= kChunkSize) {
            flushSource(file.pos());
            execSql(db, "COMMIT;");

            const int pct = totalSize > 0
                ? static_cast<int>((file.pos() * 100LL) / totalSize) : 0;
            emit progress(pct, QStringLiteral("%1 lines").arg(totalLines));

            chunkCount = 0;
            execSql(db, "BEGIN;");
        }
    }

    flushSource(file.pos());
    execSql(db, "COMMIT;");

    sqlite3_finalize(logStmt);
    sqlite3_finalize(sourceStmt);
    sqlite3_close(db);

    emit progress(100, QStringLiteral("%1 lines").arg(totalLines));
    emit finished();
}
