#include "workers/CloseOrphanSessionsWorker.h"
#include "db/Database.h"
#include <QDebug>

CloseOrphanSessionsWorker::CloseOrphanSessionsWorker(
    const QString &dbPath,
    const QStringList &runningInstallPaths,
    QObject *parent)
    : BackgroundWorker(parent)
    , m_dbPath(dbPath)
    , m_runningInstallPaths(runningInstallPaths)
{}

void CloseOrphanSessionsWorker::start()
{
    Database db(m_dbPath);
    if (!db.isOpen()) {
        emit failed(QStringLiteral("Cannot open database for session cleanup: %1")
                    .arg(db.lastError()));
        return;
    }

    const int closed = db.closeOrphanSessions(m_runningInstallPaths);
    if (closed > 0)
        qDebug() << "[poll] closeOrphanSessions closed=" << closed;

    emit progress(100, {});  // Monitoring state — hides this task from the task panel
    emit sessionsClosed(closed);
    emit finished();
}
