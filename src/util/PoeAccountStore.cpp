#include "util/PoeAccountStore.h"

#include "services/PoeInfoClient.h"

#include <QJsonObject>

PoeAccountStore::PoeAccountStore(PoeInfoClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{}

void PoeAccountStore::checkSession()
{
    m_client->request(QStringLiteral("credentials.has"),
                       {{QStringLiteral("key"), QLatin1String(kKey)}},
                       [this](QJsonObject payload, QString error) {
        emit sessionChecked(error.isEmpty() && payload[QStringLiteral("present")].toBool());
    });
}

void PoeAccountStore::storeSession(const QString &poesessid)
{
    m_client->request(QStringLiteral("credentials.store"),
                       {{QStringLiteral("key"), QLatin1String(kKey)},
                        {QStringLiteral("value"), poesessid}},
                       [this](QJsonObject /*payload*/, QString error) {
        emit sessionStored(error.isEmpty());
    });
}

void PoeAccountStore::deleteSession()
{
    m_client->request(QStringLiteral("credentials.delete"),
                       {{QStringLiteral("key"), QLatin1String(kKey)}},
                       [this](QJsonObject /*payload*/, QString error) {
        emit sessionDeleted(error.isEmpty());
    });
}
