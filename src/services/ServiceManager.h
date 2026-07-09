#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

class ServiceManager : public QObject
{
    Q_OBJECT
public:
    explicit ServiceManager(QObject *parent = nullptr);
    ~ServiceManager() override;

    // serviceDataDir overrides poe-info-service's default data directory
    // (config + database); pass an empty string (the normal case) to let it
    // resolve its own default — it owns that data, this app does not.
    // installDirs is the configured list of PoE install directory candidates
    // (may be empty if none configured yet), passed through as one
    // --install-dir flag per entry — poe-info-service, not this client,
    // ingests every one that actually exists on disk (deriving each one's
    // Client.txt path), since it owns that filesystem check.
    void start(const QString &serviceDataDir, const QStringList &installDirs);
    void stop();

    QString host() const { return m_host; }
    int     port() const { return m_port; }

private:
    void loadConfig();

    QProcess *m_process{};
    QString   m_host{QStringLiteral("127.0.0.1")};
    int       m_port{47652};
#ifdef Q_OS_WIN
    // Job object the child is assigned to so Windows kills it automatically
    // if this process dies without running stop() (crash, force-kill, etc.);
    // otherwise the child leaks and squats m_port for the next launch.
    void *m_jobHandle{nullptr};
#endif
};
