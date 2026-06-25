#pragma once

#include "workers/BackgroundWorker.h"
#include "db/DatabaseHealth.h"
#include <QString>
#include <QVector>

// Runs a DatabaseHealth tier on a background thread.
// Emit order: progress() updates during the run, then finished() on completion.
// The results vector is delivered via healthComplete() before finished().
class DatabaseHealthWorker : public BackgroundWorker
{
    Q_OBJECT
public:
    enum class Mode { Routine, Full, Repair };

    DatabaseHealthWorker(const QString &dbPath, Mode mode, QObject *parent = nullptr);

    void start() override;

signals:
    void healthComplete(QVector<DatabaseHealth::CheckResult> results);

private:
    QString m_dbPath;
    Mode    m_mode;
};
