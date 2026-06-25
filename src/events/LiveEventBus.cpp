#include "events/LiveEventBus.h"

#include <algorithm>

LiveEventBus* LiveEventBus::s_instance = nullptr;

LiveEventBus::LiveEventBus()
    : m_flushTimer(new QTimer(this))
{
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(16);
    connect(m_flushTimer, &QTimer::timeout, this, &LiveEventBus::flush);
}

LiveEventBus* LiveEventBus::instance()
{
    if (!s_instance) {
        qRegisterMetaType<LiveEvent>("LiveEvent");
        s_instance = new LiveEventBus;
    }
    return s_instance;
}

int LiveEventBus::subscribe(const QString& eventType, Handler handler)
{
    const int id = m_nextId++;
    m_subs.push_back({id, eventType, std::move(handler)});
    return id;
}

void LiveEventBus::unsubscribe(int id)
{
    m_subs.erase(
        std::remove_if(m_subs.begin(), m_subs.end(),
                       [id](const Sub& s) { return s.id == id; }),
        m_subs.end());
}

void LiveEventBus::dispatch(const LiveEvent& event)
{
    m_pending.append(event);
    m_flushTimer->start(); // restart — 100ms from the most recent event
}

void LiveEventBus::flush()
{
    QList<LiveEvent> batch;
    batch.swap(m_pending);
    const bool bulk = batch.size() > 50;
    for (const LiveEvent &event : batch) {
        for (const Sub &sub : m_subs)
            if (sub.type.isEmpty() || sub.type == event.type)
                sub.fn(event);
        emit eventFired(event, bulk);
    }
}
