#include "services/ServiceManager.h"

#include "core/AppConfig.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QProcess>
#include <QByteArray>

#include <toml++/toml.hpp>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(Q_OS_LINUX)
#include <csignal>
#include <sys/prctl.h>
#endif

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

void ServiceManager::start(const QString &serviceDataDir, const QString &installDir)
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
    if (!serviceDataDir.isEmpty())
        args << "--data-dir" << serviceDataDir;
    if (!installDir.isEmpty()) {
        args << "--install-dir" << installDir
             << "--log-path"    << installDir + "/logs/Client.txt";
    }
    args << "--config-path" << AppConfig::configPath();
    const QByteArray serviceLog = qgetenv("L2P_SERVICE_LOG");
    if (!serviceLog.isEmpty())
        args << "--service-log" << QString::fromUtf8(serviceLog);

    qDebug() << "ServiceManager: launching" << binary << args;

    m_process = new QProcess(this);
    m_process->setProgram(binary);
    m_process->setArguments(args);
#ifdef Q_OS_LINUX
    // If we die without running stop() (crash, force-kill, etc.), make the
    // kernel kill the child too instead of leaving it to squat m_port.
    m_process->setChildProcessModifier([] { prctl(PR_SET_PDEATHSIG, SIGKILL); });
#endif
    connect(m_process, &QProcess::finished, this,
            [](int exitCode, QProcess::ExitStatus status) {
        qDebug() << "ServiceManager: poe-info-service exited, code" << exitCode
                  << "status" << (status == QProcess::NormalExit ? "normal" : "crashed")
                  << "(this is expected if an incumbent instance won singleton negotiation)";
    });
    m_process->start();

    if (!m_process->waitForStarted(3000)) {
        qWarning() << "ServiceManager: failed to start poe-info-service";
        delete m_process;
        m_process = nullptr;
        return;
    }
    qDebug() << "ServiceManager: started poe-info-service pid" << m_process->processId()
              << "host" << m_host << "port" << m_port;

#ifdef Q_OS_WIN
    // Assign the child to a job object with KILL_ON_JOB_CLOSE so Windows tears
    // it down if we die without running stop() — otherwise it leaks and squats
    // m_port, and the next launch's client silently can't connect.
    m_jobHandle = CreateJobObjectW(nullptr, nullptr);
    if (m_jobHandle) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_jobHandle, JobObjectExtendedLimitInformation, &info, sizeof(info));

        HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE,
                                       static_cast<DWORD>(m_process->processId()));
        if (hProcess) {
            if (!AssignProcessToJobObject(m_jobHandle, hProcess))
                qWarning() << "ServiceManager: failed to assign poe-info-service to job object";
            CloseHandle(hProcess);
        } else {
            qWarning() << "ServiceManager: failed to open poe-info-service process handle";
        }
    }
#endif
}

void ServiceManager::stop()
{
#ifdef Q_OS_WIN
    if (m_jobHandle) {
        CloseHandle(static_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
#endif
    if (!m_process)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(3000))
        m_process->kill();
    delete m_process;
    m_process = nullptr;
}
