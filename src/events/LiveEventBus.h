#pragma once

#include "events/LiveEvent.h"

#include <QList>
#include <QObject>
#include <QTimer>
#include <QVector>
#include <functional>

// Singleton pub/sub bus for live game events.
// poe-info-service tails and parses Client.txt; MainWindow subscribes to its
// "clientlog" WebSocket topic via PoeInfoClient and calls dispatch() with
// each event. Any code may subscribe via subscribe() or connect to eventFired.
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
    void eventFired(const LiveEvent &event, bool bulk);

private:
    explicit LiveEventBus();

    void flush();

    struct Sub {
        int     id;
        QString type;
        Handler fn;
    };

    static LiveEventBus* s_instance;
    QVector<Sub>     m_subs;
    int              m_nextId{1};
    QList<LiveEvent> m_pending;
    QTimer          *m_flushTimer;
};
