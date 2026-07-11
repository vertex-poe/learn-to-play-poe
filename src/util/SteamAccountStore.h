#pragma once

#include <QObject>
#include <QString>

class PoeInfoClient;

// Hands a Steam Web API key off to poe-info-service the same way
// PoeAccountStore hands off POESESSID: poe-info-service is the sole owner of
// the credential and never returns the value itself, only presence. See
// poe-info-service/internal/server/steam.go's steamAPIKeyCredKey — the
// "steamApiKey" key string here must match it exactly.
class SteamAccountStore : public QObject
{
    Q_OBJECT
public:
    explicit SteamAccountStore(PoeInfoClient *client, QObject *parent = nullptr);

    void checkKey();
    void storeKey(const QString &apiKey);
    void deleteKey();

signals:
    void keyChecked(bool present);
    void keyStored(bool ok);
    void keyDeleted(bool ok);

private:
    PoeInfoClient *m_client{};
    static constexpr const char *kKey = "steamApiKey";
};
