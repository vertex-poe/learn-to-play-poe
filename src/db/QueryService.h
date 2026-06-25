#pragma once

#include "db/Database.h"
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

class QThread;

class QueryService : public QObject
{
    Q_OBJECT
public:
    struct CurrentPageData {
        QList<Database::SessionEventRecord>        sessionEvents;
        QList<Database::ZoneTransitionRecord>      zones;
        QList<Database::ClientScreenEventRecord>   clientScreenEvents;
        QList<Database::AfkRecord>                 afkRecords;
        QList<Database::AltTabRecord>              altTabRecords;
    };

    explicit QueryService(const QString &dbPath, QObject *parent = nullptr);
    ~QueryService() override;

    // All callbacks are guaranteed to run on the caller's (UI) thread.
    // Fire-and-deliver — no built-in coalescing. Pages serialize concurrent
    // requests via their own m_rebuildInFlight flags.

    void fetchCurrentPageData(int sessionEventLimit, int zoneLimit,
        std::function<void(CurrentPageData)> cb);

    void fetchZoneTransitions(int limit, int offset,
        std::function<void(QList<Database::ZoneTransitionRecord>)> cb);

    void fetchSessions(int limit, int offset,
        std::function<void(QList<Database::SessionRecord>)> cb);

    void fetchSessionEvents(int limit, int offset,
        std::function<void(QList<Database::SessionEventRecord>)> cb);

    void fetchChats(const QSet<QChar> &channels, bool includeDms,
        int limit, const QString &fromDate, const QString &toDate, int offset,
        std::function<void(QList<Database::ChatRecord>)> cb);

    void fetchChatDates(const QSet<QChar> &channels, bool includeDms,
        std::function<void(QStringList)> cb);

    void fetchWhispers(const QString &playerFilter, int limit, int offset,
        std::function<void(QList<Database::WhisperRecord>)> cb);

    void fetchWhisperPartnersWithDates(
        std::function<void(QList<Database::PartnerRecord>)> cb);

private:
    class Worker;
    Worker  *m_worker{nullptr};
    QThread *m_thread{nullptr};
};
