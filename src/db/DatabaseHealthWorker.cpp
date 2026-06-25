#include "db/DatabaseHealthWorker.h"

DatabaseHealthWorker::DatabaseHealthWorker(const QString &dbPath, Mode mode, QObject *parent)
    : BackgroundWorker(parent)
    , m_dbPath(dbPath)
    , m_mode(mode)
{}

void DatabaseHealthWorker::start()
{
    DatabaseHealth health(m_dbPath);
    if (!health.isOpen()) {
        emit failed(QStringLiteral("Cannot open database for health check"));
        return;
    }

    QVector<DatabaseHealth::CheckResult> results;

    switch (m_mode) {
    case Mode::Routine:
        emit progress(0, QStringLiteral("Running routine maintenance…"));
        results = health.runRoutine();
        break;

    case Mode::Full:
        emit progress(0, QStringLiteral("Running full health check…"));
        results = health.runFull();
        // Empty results mean the interval hasn't elapsed yet — not an error.
        break;

    case Mode::Repair:
        emit progress(0,  QStringLiteral("Rebuilding indexes…"));
        // runRepair is long; emit coarse progress milestones via the result count.
        results = health.runRepair();
        break;
    }

    emit progress(100, QStringLiteral("Done"));
    emit healthComplete(results);
    emit finished();
}
