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

    // dbPath overrides poe-info-service's default database location; pass an
    // empty string (the normal case) to let it resolve its own default
    // (poe-info-service.db next to poe-info-service.toml) — it owns this
    // database, this app does not. installDir is the PoE install directory
    // (may be empty if none configured yet); the Client.txt path and install
    // identity are both derived from it.
    void start(const QString &dbPath, const QString &installDir);
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
