#include "FakePoeInfoServer.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QWebSocket>

FakePoeInfoServer::FakePoeInfoServer(QObject *parent)
    : QObject(parent)
    , m_server(QStringLiteral("fake-poe-info-service"), QWebSocketServer::NonSecureMode)
{
    m_server.listen(QHostAddress::LocalHost, 0);
    connect(&m_server, &QWebSocketServer::newConnection, this, &FakePoeInfoServer::onNewConnection);
}

quint16 FakePoeInfoServer::port() const
{
    return m_server.serverPort();
}

void FakePoeInfoServer::queueResponse(const QString &method, const QJsonObject &payload, const QString &error)
{
    m_queuedResponses[method].append({payload, error});
}

int FakePoeInfoServer::requestCount(const QString &method) const
{
    return m_requestCounts.value(method, 0);
}

QJsonObject FakePoeInfoServer::lastParams(const QString &method) const
{
    return m_lastParams.value(method);
}

void FakePoeInfoServer::publishEvent(const QString &topic, const QJsonObject &payload)
{
    const QJsonObject msg{
        {QStringLiteral("type"),    QStringLiteral("event")},
        {QStringLiteral("topic"),   topic},
        {QStringLiteral("payload"), payload},
    };
    const QString data = QString::fromUtf8(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    const QList<QWebSocket *> sockets = m_sockets;
    for (QWebSocket *socket : sockets)
        socket->sendTextMessage(data);
}

void FakePoeInfoServer::onNewConnection()
{
    QWebSocket *socket = m_server.nextPendingConnection();
    m_sockets.append(socket);
    connect(socket, &QWebSocket::textMessageReceived, this,
            [this, socket](const QString &message) { onTextMessageReceived(socket, message); });
    connect(socket, &QWebSocket::disconnected, this, [this, socket]() {
        m_sockets.removeAll(socket);
        socket->deleteLater();
    });
}

void FakePoeInfoServer::onTextMessageReceived(QWebSocket *socket, const QString &message)
{
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    if (obj[QStringLiteral("type")].toString() != QStringLiteral("request"))
        return;

    const QString method = obj[QStringLiteral("method")].toString();
    const QString id = obj[QStringLiteral("id")].toString();
    m_requestCounts[method] = m_requestCounts.value(method, 0) + 1;
    m_lastParams[method] = obj[QStringLiteral("payload")].toObject();

    QJsonObject payload;
    QString error;
    QList<QPair<QJsonObject, QString>> &queue = m_queuedResponses[method];
    if (!queue.isEmpty()) {
        const auto entry = queue.takeFirst();
        payload = entry.first;
        error = entry.second;
    }

    const QJsonObject resp{
        {QStringLiteral("type"), QStringLiteral("response")},
        {QStringLiteral("id"), id},
        {QStringLiteral("payload"), payload},
        {QStringLiteral("error"), error},
    };
    socket->sendTextMessage(QJsonDocument(resp).toJson(QJsonDocument::Compact));
}
