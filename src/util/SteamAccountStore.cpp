#include "util/SteamAccountStore.h"

#include "services/PoeInfoClient.h"

#include <QJsonObject>

SteamAccountStore::SteamAccountStore(PoeInfoClient *client, QObject *parent)
    : QObject(parent), m_client(client)
{}

void SteamAccountStore::checkKey()
{
    m_client->request(QStringLiteral("credentials.has"),
                       {{QStringLiteral("key"), QLatin1String(kKey)}},
                       [this](QJsonObject payload, QString error) {
        emit keyChecked(error.isEmpty() && payload[QStringLiteral("present")].toBool());
    });
}

void SteamAccountStore::storeKey(const QString &apiKey)
{
    m_client->request(QStringLiteral("credentials.store"),
                       {{QStringLiteral("key"), QLatin1String(kKey)},
                        {QStringLiteral("value"), apiKey}},
                       [this](QJsonObject /*payload*/, QString error) {
        emit keyStored(error.isEmpty());
    });
}

void SteamAccountStore::deleteKey()
{
    m_client->request(QStringLiteral("credentials.delete"),
                       {{QStringLiteral("key"), QLatin1String(kKey)}},
                       [this](QJsonObject /*payload*/, QString error) {
        emit keyDeleted(error.isEmpty());
    });
}
