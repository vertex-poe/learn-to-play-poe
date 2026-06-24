#pragma once

#include <QObject>
#include <QString>

class PoeAccountStore : public QObject
{
    Q_OBJECT
public:
    explicit PoeAccountStore(QObject *parent = nullptr);

    void readSession();
    void writeSession(const QString &poesessid);
    void deleteSession();

signals:
    void sessionRead(const QString &poesessid); // empty = not found
    void sessionWritten(bool ok);
    void sessionDeleted(bool ok);

private:
    static constexpr const char *kService = "l2p-poe1";
    static constexpr const char *kKey     = "poesessid";
};
