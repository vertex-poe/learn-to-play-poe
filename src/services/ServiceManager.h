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

    void start(const QString &dbPath, const QString &logPath);
    void stop();

    QString host() const { return m_host; }
    int     port() const { return m_port; }

private:
    void loadConfig();

    QProcess *m_process{};
    QString   m_host{QStringLiteral("127.0.0.1")};
    int       m_port{47652};
};
