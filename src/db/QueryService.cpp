#include "db/QueryService.h"
#include <QMetaObject>
#include <QPointer>
#include <QThread>

// Worker runs on a dedicated thread and holds its own read-only DB connection.
// The destructor of QueryService calls m_thread->quit() + wait() before
// deleting this, so m_db is always valid for the lifetime of any queued lambda.
class QueryService::Worker : public QObject
{
    Q_OBJECT
public:
    explicit Worker(const QString &dbPath) : m_db(dbPath, true) {}
    Database &db() { return m_db; }
private:
    Database m_db;
};

QueryService::QueryService(const QString &dbPath, QObject *parent)
    : QObject(parent)
{
    m_worker = new Worker(dbPath);
    m_thread = new QThread(this);
    m_worker->moveToThread(m_thread);
    m_thread->start();
}

QueryService::~QueryService()
{
    m_thread->quit();
    m_thread->wait(); // all queued lambdas complete before we delete the worker
    delete m_worker;
}

// ---------------------------------------------------------------------------
// Helper macro-like pattern: outer lambda runs on worker thread, inner lambda
// is posted back to the UI thread (this object's thread). The QPointer<self>
// guard in the inner lambda handles post-destruction delivery safely.
// ---------------------------------------------------------------------------

void QueryService::fetchCurrentPageData(int sessionEventLimit, int zoneLimit,
    std::function<void(CurrentPageData)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, sessionEventLimit, zoneLimit,
         cb = std::move(cb)]() mutable {
            CurrentPageData data;
            if (sessionEventLimit > 0)
                data.sessionEvents = worker->db().fetchSessionEvents(sessionEventLimit);
            if (zoneLimit > 0) {
                data.zones = worker->db().fetchZoneTransitions(zoneLimit, 0);
                data.clientScreenEvents = worker->db().fetchClientScreenEvents();
                data.afkRecords = worker->db().fetchAfkRecords(zoneLimit);
                data.altTabRecords = worker->db().fetchAltTabRecords(zoneLimit);
            }
            QMetaObject::invokeMethod(qs,
                [self, data = std::move(data), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(data));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchSessionPageData(qint64 sessionId, int zoneLimit,
    std::function<void(CurrentPageData)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, sessionId, zoneLimit, cb = std::move(cb)]() mutable {
            CurrentPageData data;
            data.zones = worker->db().fetchZoneTransitions(zoneLimit, 0, sessionId);
            data.clientScreenEvents = worker->db().fetchClientScreenEvents(sessionId);
            data.afkRecords = worker->db().fetchAfkRecords(zoneLimit, sessionId);
            data.altTabRecords = worker->db().fetchAltTabRecords(zoneLimit, sessionId);
            QMetaObject::invokeMethod(qs,
                [self, data = std::move(data), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(data));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchZoneTransitions(int limit, int offset,
    std::function<void(QList<Database::ZoneTransitionRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, limit, offset, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchZoneTransitions(limit, offset);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchZoneTransitionsForSession(qint64 sessionId, int limit, int offset,
    std::function<void(QList<Database::ZoneTransitionRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, sessionId, limit, offset, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchZoneTransitions(limit, offset, sessionId);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchSessions(int limit, int offset,
    std::function<void(QList<Database::SessionRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, limit, offset, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchSessions(limit, offset);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchSessionEvents(int limit, int offset,
    std::function<void(QList<Database::SessionEventRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, limit, offset, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchSessionEvents(limit, offset);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchChats(const QSet<QChar> &channels, bool includeDms,
    int limit, const QString &fromDate, const QString &toDate, int offset,
    std::function<void(QList<Database::ChatRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, channels, includeDms, limit, fromDate, toDate, offset,
         cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchChats(channels, includeDms, limit, fromDate, toDate, offset);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchChatDates(const QSet<QChar> &channels, bool includeDms,
    std::function<void(QStringList)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, channels, includeDms, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchChatDates(channels, includeDms);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchWhispers(const QString &playerFilter, int limit, int offset,
    std::function<void(QList<Database::WhisperRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, playerFilter, limit, offset, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchWhispers(playerFilter, limit, offset);
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void QueryService::fetchWhisperPartnersWithDates(
    std::function<void(QList<Database::PartnerRecord>)> cb)
{
    QPointer<QueryService> self(this);
    auto *worker = m_worker;
    QMetaObject::invokeMethod(worker,
        [worker, qs = this, self, cb = std::move(cb)]() mutable {
            auto result = worker->db().fetchWhisperPartnersWithDates();
            QMetaObject::invokeMethod(qs,
                [self, result = std::move(result), cb = std::move(cb)]() mutable {
                    if (self) cb(std::move(result));
                }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

#include "QueryService.moc"
