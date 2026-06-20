#pragma once

#include "LiveEvent.h"

#include <QObject>
#include <QVector>
#include <functional>

// Singleton pub/sub bus for live game events.
// LogIngestWorker emits liveEventParsed → this bus's dispatch() slot (queued connection).
// Any code may subscribe via subscribe() or connect to eventFired.
class LiveEventBus : public QObject
{
    Q_OBJECT
public:
    static LiveEventBus* instance();

    using Handler = std::function<void(const LiveEvent&)>;

    // Subscribe to a specific event type, or "" to receive all events.
    // Returns a subscription id for use with unsubscribe().
    int  subscribe(const QString& eventType, Handler handler);
    void unsubscribe(int id);

public slots:
    void dispatch(const LiveEvent& event);

signals:
    void eventFired(const LiveEvent& event);

private:
    LiveEventBus() = default;

    struct Sub {
        int     id;
        QString type;
        Handler fn;
    };

    static LiveEventBus* s_instance;
    QVector<Sub> m_subs;
    int          m_nextId{1};
};
