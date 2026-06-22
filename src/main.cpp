#include "AppConfig.h"
#include "Cli.h"
#include "MainWindow.h"
#include "Theme.h"

#include <QApplication>
#include <QDateTime>
#include <QFile>
#include <QMutex>
#include <QTextStream>

static QFile       *s_logFile   = nullptr;
static QTextStream *s_logStream = nullptr;
static QMutex       s_logMutex;

static void fileMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
    if (!s_logStream) return;
    const char *level = "D";
    if      (type == QtWarningMsg)  level = "W";
    else if (type == QtCriticalMsg) level = "E";
    else if (type == QtFatalMsg)    level = "F";
    const QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz")
                         + " [" + level + "] " + msg + '\n';
    QMutexLocker lock(&s_logMutex);
    *s_logStream << line;
    s_logStream->flush();
}

int main(int argc, char *argv[])
{
    const int cliResult = cliDispatch(argc, argv);
    if (cliResult != -1)
        return cliResult;

    QApplication app(argc, argv);
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);

    // Load config early so we can honour debug_log before MainWindow starts up.
    const AppConfig earlyConfig = AppConfig::load();
    if (earlyConfig.debugLog) {
        QString logPath = AppConfig::configPath();
        logPath.chop(5); // strip ".toml"
        logPath += ".log";
        s_logFile = new QFile(logPath);
        if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            s_logStream = new QTextStream(s_logFile);
            qInstallMessageHandler(fileMessageHandler);
        }
    }

    Theme::apply(app);

    MainWindow window;
    if (!window.startMinimized())
        window.show();

    const int ret = app.exec();

    qInstallMessageHandler(nullptr);
    delete s_logStream; s_logStream = nullptr;
    delete s_logFile;   s_logFile   = nullptr;
    return ret;
}
