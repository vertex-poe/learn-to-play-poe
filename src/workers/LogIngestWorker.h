#pragma once

#include "workers/BackgroundWorker.h"
#include "events/LiveEvent.h"
#include <QHash>
#include <QString>
#include <atomic>

class LogIngestWorker : public BackgroundWorker
{
    Q_OBJECT
public:
    LogIngestWorker(const QString &dbPath, qint64 installId,
                    const QString &logPath, qint64 resumeOffset,
                    const QHash<int, QString> &channelNames = {},
                    bool liveMode = false,
                    QObject *parent = nullptr);

    void start() override;

    // Switch from live-tail to drain-and-finish. Safe to call from any thread.
    void finalize() { m_liveMode.store(false, std::memory_order_relaxed); }

signals:
    void liveEventParsed(LiveEvent event);

private:
    QString              m_dbPath;
    qint64               m_installId;
    QString              m_logPath;
    qint64               m_resumeOffset;
    QHash<int, QString>  m_channelNames;
    std::atomic<bool>    m_liveMode;
};
