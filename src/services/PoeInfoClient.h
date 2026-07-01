#pragma once

#include <functional>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QString>

class QTimer;
class QWebSocket;

class PoeInfoClient : public QObject
{
    Q_OBJECT
public:
    explicit PoeInfoClient(const QString &host, int port, QObject *parent = nullptr);
    ~PoeInfoClient() override;

    bool isConnected() const;

    // Sends a request to the service. callback(payload, error) is called on
    // the main thread when the response arrives or the connection is lost.
    void request(const QString &method, const QJsonObject &params,
                 std::function<void(QJsonObject, QString)> callback);

signals:
    void connected();
    void disconnected();

private slots:
    void onConnected();
    void onDisconnected();
    void onTextMessageReceived(const QString &message);
    void tryConnect();

private:
    QWebSocket *m_socket{};
    QTimer     *m_reconnectTimer{};
    QString     m_host;
    int         m_port;
    int         m_nextId{1};
    QHash<QString, std::function<void(QJsonObject, QString)>> m_pending;
};
