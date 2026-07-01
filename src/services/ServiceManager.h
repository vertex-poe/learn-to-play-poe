#pragma once

#include <QObject>
#include <QString>

class QProcess;

class ServiceManager : public QObject
{
    Q_OBJECT
public:
    explicit ServiceManager(QObject *parent = nullptr);
    ~ServiceManager() override;

    // installDir is the PoE install directory (may be empty if none configured
    // yet); the Client.txt path and install identity are both derived from it.
    void start(const QString &dbPath, const QString &installDir);
    void stop();

    QString host() const { return m_host; }
    int     port() const { return m_port; }

private:
    void loadConfig();

    QProcess *m_process{};
    QString   m_host{QStringLiteral("127.0.0.1")};
    int       m_port{47652};
};
