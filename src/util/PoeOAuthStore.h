#pragma once

#include <QObject>
#include <QString>

class PoeInfoClient;
class QJsonObject;

// Drives poe-info-service's poe.oauth.* WebSocket methods for the official
// Path of Exile OAuth 2.0 API. Unlike PoeAccountStore/SteamAccountStore,
// this class never hands poe-info-service a secret to store — poe-info-
// service originates and owns this credential itself, opening the system
// browser and running a local loopback listener (see ADR-004 in
// poe-info-service/docs/decisions/). This class only asks it to start or
// stop that flow and relays the live status poe-info-service publishes on
// the "poeOAuthStatus" topic, so the login/logout buttons update themselves
// without polling.
class PoeOAuthStore : public QObject
{
    Q_OBJECT
public:
    explicit PoeOAuthStore(PoeInfoClient *client, QObject *parent = nullptr);

    // Requests a fresh status snapshot; result arrives via statusChanged,
    // same as an unsolicited "poeOAuthStatus" push.
    void checkStatus();

    // Starts an interactive login (opens the user's system browser). The
    // eventual outcome arrives via statusChanged, not this call directly —
    // poe-info-service's response to poe.oauth.login only confirms whether
    // the flow (re)started.
    void login();

    void logout();

signals:
    // Mirrors poe-info-service's PoeOAuthStatusPayload (internal/proto).
    // username/scope/accessExpiration are only meaningful when authorized
    // is true; error carries the last login/refresh failure's message, if
    // any.
    void statusChanged(bool authorized, bool inProgress, const QString &username,
                        const QString &scope, qint64 accessExpiration, const QString &error);

private:
    PoeInfoClient *m_client{};
    void emitStatus(const QJsonObject &payload);
};
