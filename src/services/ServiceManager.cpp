#include "services/ServiceManager.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcess>

#include <toml++/toml.hpp>

ServiceManager::ServiceManager(QObject *parent)
    : QObject(parent)
{
    loadConfig();
}

ServiceManager::~ServiceManager()
{
    stop();
}

void ServiceManager::loadConfig()
{
    const QString path = QCoreApplication::applicationDirPath() + "/poe-info-service.toml";
    if (!QFile::exists(path))
        return;
    try {
        auto tbl = toml::parse_file(path.toStdString());
        m_host = QString::fromStdString(tbl["bind"].value_or(std::string("127.0.0.1")));
        m_port = tbl["port"].value_or(47652);
    } catch (const toml::parse_error &e) {
        qWarning() << "ServiceManager: failed to parse poe-info-service.toml:" << e.what();
    }
}

void ServiceManager::start(const QString &dbPath, const QString &logPath)
{
    if (m_process)
        return;

    QString binary = QCoreApplication::applicationDirPath() + "/poe-info-service";
#ifdef Q_OS_WIN
    binary += ".exe";
#endif
    if (!QFile::exists(binary)) {
        qWarning() << "ServiceManager: binary not found at" << binary;
        return;
    }

    QStringList args;
    args << "--port" << QString::number(m_port)
         << "--bind" << m_host;
    if (!dbPath.isEmpty())
        args << "--db-path" << dbPath;
    if (!logPath.isEmpty())
        args << "--log-path" << logPath;

    m_process = new QProcess(this);
    m_process->setProgram(binary);
    m_process->setArguments(args);
    m_process->start();

    if (!m_process->waitForStarted(3000)) {
        qWarning() << "ServiceManager: failed to start poe-info-service";
        delete m_process;
        m_process = nullptr;
        return;
    }
    qDebug() << "ServiceManager: started poe-info-service pid" << m_process->processId();
}

void ServiceManager::stop()
{
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(3000))
        m_process->kill();
    delete m_process;
    m_process = nullptr;
}
