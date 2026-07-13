#pragma once

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPair>
#include <QWebSocketServer>

class QWebSocket;

// Minimal in-process stand-in for poe-info-service's WebSocket endpoint.
// A real PoeInfoClient connects to this exactly as it would the real
// service, so tests exercise PoeInfoClient's actual wire protocol and
// reconnect logic instead of mocking it away (PoeInfoClient itself isn't
// mockable: concrete QObject, non-virtual request(), real QWebSocket).
//
// Speaks the "request"/"response" half of the protocol used by
// PoeInfoClient::request() — enough to drive SessionViewPage/LogPage's
// request-retry-on-error paths — plus publishEvent() for the "event"/topic
// half used by PoeInfoClient::subscribe() (e.g. PoeOAuthStore's
// "poeOAuthStatus" subscription). Incoming "subscribe" messages themselves
// are not tracked/filtered on — publishEvent() simply broadcasts to every
// connected socket, since PoeInfoClient already filters incoming events by
// its own per-topic subscription table, so an unfiltered broadcast is
// observably identical to the real service for a single test client.
class FakePoeInfoServer : public QObject
{
    Q_OBJECT
public:
    explicit FakePoeInfoServer(QObject *parent = nullptr);

    quint16 port() const;

    // Queues one response for the next request of `method`, consumed in FIFO
    // order. Once a method's queue is empty, further requests for it get a
    // default success response with an empty payload.
    void queueResponse(const QString &method, const QJsonObject &payload, const QString &error = {});

    int requestCount(const QString &method) const;

    // The payload ("params") of the most recent request for `method`, or an
    // empty object if none has been received yet.
    QJsonObject lastParams(const QString &method) const;

    // Broadcasts an "event" message on topic to every currently connected
    // socket — see the class doc comment for why no subscribe-tracking is
    // needed for this to behave correctly from a single test client's view.
    void publishEvent(const QString &topic, const QJsonObject &payload);

private:
    void onNewConnection();
    void onTextMessageReceived(QWebSocket *socket, const QString &message);

    QWebSocketServer m_server;
    QHash<QString, QList<QPair<QJsonObject, QString>>> m_queuedResponses;
    QHash<QString, int> m_requestCounts;
    QHash<QString, QJsonObject> m_lastParams;
    QList<QWebSocket *> m_sockets;
};
