#pragma once

#include "BackgroundWorker.h"
#include <QString>

class LogIngestWorker : public BackgroundWorker
{
    Q_OBJECT
public:
    LogIngestWorker(const QString &dbPath, qint64 installId,
                    const QString &logPath, qint64 resumeOffset,
                    QObject *parent = nullptr);

    void start() override;

private:
    QString m_dbPath;
    qint64  m_installId;
    QString m_logPath;
    qint64  m_resumeOffset;
};
