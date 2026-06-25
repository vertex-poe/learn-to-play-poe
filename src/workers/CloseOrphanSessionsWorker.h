#pragma once

#include "workers/BackgroundWorker.h"
#include <QStringList>

// Runs Database::closeOrphanSessions on the DbWrite worker thread so the poll
// timer never blocks the UI thread on a SQLite write.
// Emits sessionsClosed(count) before finished(); count is the number of sessions
// that were closed (0 is the common case when all installs are still running).
class CloseOrphanSessionsWorker : public BackgroundWorker
{
    Q_OBJECT
public:
    CloseOrphanSessionsWorker(const QString &dbPath,
                              const QStringList &runningInstallPaths,
                              QObject *parent = nullptr);
    void start() override;

signals:
    void sessionsClosed(int count);

private:
    QString     m_dbPath;
    QStringList m_runningInstallPaths;
};
