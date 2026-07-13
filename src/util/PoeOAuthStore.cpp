#include "util/PoeOAuthStore.h"

#include "services/PoeInfoClient.h"

#include <QJsonObject>

PoeOAuthStore::PoeOAuthStore(PoeInfoClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{
    m_client->subscribe(QStringLiteral("poeOAuthStatus"),
                         [this](QJsonObject payload) { emitStatus(payload); });
}

void PoeOAuthStore::checkStatus()
{
    m_client->request(QStringLiteral("poe.oauth.status"), {},
                       [this](QJsonObject payload, QString error) {
        if (!error.isEmpty())
            return;
        emitStatus(payload);
    });
}

void PoeOAuthStore::login()
{
    // The response only says whether the flow (re)started — see
    // PoeOAuthStore::login's doc comment. The interesting outcome comes
    // through the "poeOAuthStatus" subscription set up in the constructor.
    m_client->request(QStringLiteral("poe.oauth.login"), {}, [](QJsonObject, QString) {});
}

void PoeOAuthStore::logout()
{
    m_client->request(QStringLiteral("poe.oauth.logout"), {}, [](QJsonObject, QString) {});
}

void PoeOAuthStore::emitStatus(const QJsonObject &payload)
{
    emit statusChanged(
        payload[QStringLiteral("authorized")].toBool(),
        payload[QStringLiteral("inProgress")].toBool(),
        payload[QStringLiteral("username")].toString(),
        payload[QStringLiteral("scope")].toString(),
        static_cast<qint64>(payload[QStringLiteral("accessExpiration")].toDouble()),
        payload[QStringLiteral("error")].toString());
}
