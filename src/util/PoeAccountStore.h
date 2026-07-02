#pragma once

#include <QObject>
#include <QString>

class PoeInfoClient;

// Hands POESESSID off to poe-info-service and asks it whether one is already
// stored, rather than persisting it in this app — poe-info-service is the
// sole owner of the credential per ADR-004/ADR-005 and never returns the
// value itself, only presence.
class PoeAccountStore : public QObject
{
    Q_OBJECT
public:
    explicit PoeAccountStore(PoeInfoClient *client, QObject *parent = nullptr);

    void checkSession();
    void storeSession(const QString &poesessid);
    void deleteSession();

signals:
    void sessionChecked(bool present);
    void sessionStored(bool ok);
    void sessionDeleted(bool ok);

private:
    PoeInfoClient *m_client{};
    static constexpr const char *kKey = "poesessid";
};
