#include "services/PoeInfoClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWebSocket>

static constexpr int kReconnectIntervalMs = 3000;

PoeInfoClient::PoeInfoClient(const QString &host, int port, QObject *parent)
    : QObject(parent), m_host(host), m_port(port)
{
    m_socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    connect(m_socket, &QWebSocket::connected,           this, &PoeInfoClient::onConnected);
    connect(m_socket, &QWebSocket::disconnected,        this, &PoeInfoClient::onDisconnected);
    connect(m_socket, &QWebSocket::textMessageReceived, this, &PoeInfoClient::onTextMessageReceived);

    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(kReconnectIntervalMs);
    connect(m_reconnectTimer, &QTimer::timeout, this, &PoeInfoClient::tryConnect);

    tryConnect();
}

PoeInfoClient::~PoeInfoClient()
{
    m_reconnectTimer->stop();
    m_socket->abort();
}

bool PoeInfoClient::isConnected() const
{
    return m_socket->state() == QAbstractSocket::ConnectedState;
}

void PoeInfoClient::request(const QString &method, const QJsonObject &params,
                            std::function<void(QJsonObject, QString)> callback)
{
    const QString id = QString::number(m_nextId++);
    m_pending.insert(id, std::move(callback));

    const QJsonObject msg{
        {QStringLiteral("type"),    QStringLiteral("request")},
        {QStringLiteral("id"),      id},
        {QStringLiteral("method"),  method},
        {QStringLiteral("payload"), params},
    };
    m_socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void PoeInfoClient::subscribe(const QString &topic, std::function<void(QJsonObject)> handler)
{
    m_subscriptions[topic].append(std::move(handler));
    if (isConnected())
        sendSubscribe(topic);
}

void PoeInfoClient::sendSubscribe(const QString &topic)
{
    const QJsonObject msg{
        {QStringLiteral("type"),  QStringLiteral("subscribe")},
        {QStringLiteral("topic"), topic},
    };
    m_socket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
}

void PoeInfoClient::onConnected()
{
    qDebug() << "PoeInfoClient: connected to" << m_host << ":" << m_port;
    for (auto it = m_subscriptions.constBegin(); it != m_subscriptions.constEnd(); ++it)
        sendSubscribe(it.key());
    emit connected();
}

void PoeInfoClient::onDisconnected()
{
    qDebug() << "PoeInfoClient: disconnected";
    // Drain pending callbacks so callers don't stall waiting for responses.
    auto pending = std::move(m_pending);
    for (auto &cb : pending)
        cb({}, QStringLiteral("connection lost"));

    m_reconnectTimer->start();
    emit disconnected();
}

void PoeInfoClient::onTextMessageReceived(const QString &message)
{
    const QJsonObject obj = QJsonDocument::fromJson(message.toUtf8()).object();
    const QString type = obj[QStringLiteral("type")].toString();

    if (type == QStringLiteral("event")) {
        const QString topic = obj[QStringLiteral("topic")].toString();
        const auto it = m_subscriptions.constFind(topic);
        if (it == m_subscriptions.constEnd())
            return;
        const QJsonObject payload = obj[QStringLiteral("payload")].toObject();
        for (const auto &handler : it.value())
            handler(payload);
        return;
    }

    if (type != QStringLiteral("response"))
        return;

    const QString id = obj[QStringLiteral("id")].toString();
    auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;

    auto cb = std::move(it.value());
    m_pending.erase(it);

    cb(obj[QStringLiteral("payload")].toObject(),
       obj[QStringLiteral("error")].toString());
}

void PoeInfoClient::tryConnect()
{
    const QUrl url(QStringLiteral("ws://%1:%2/ws").arg(m_host).arg(m_port));
    m_socket->open(url);
}
