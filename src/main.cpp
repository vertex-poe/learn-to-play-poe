#include "core/AppConfig.h"
#include "core/Cli.h"
#include "core/MainWindow.h"
#include "core/PerfProbe.h"
#include "ui/Theme.h"

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

// ---------------------------------------------------------------------------
// Crash handler — Windows only
// ---------------------------------------------------------------------------
#ifdef Q_OS_WIN
#include <windows.h>
#include <DbgHelp.h>
#include <cstdlib>
#include <exception>

static wchar_t s_crashLogPath [MAX_PATH] = {};
static wchar_t s_crashDumpPath[MAX_PATH] = {};

// Safe to call from the crash handler: no heap, no CRT, direct Win32 I/O.
static void writeCrashEntry(const char *detail)
{
    if (!s_crashLogPath[0]) return;
    HANDLE h = CreateFileW(s_crashLogPath,
                           FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    char line[512];
    int n = wsprintfA(line, "%02d:%02d:%02d.000 [F] [crash] %s\n",
                      (int)t.wHour, (int)t.wMinute, (int)t.wSecond, detail);
    DWORD written;
    WriteFile(h, line, (DWORD)n, &written, nullptr);
    CloseHandle(h);
}

static void writeCrashDump(EXCEPTION_POINTERS *ep)
{
    if (!s_crashDumpPath[0]) return;
    HANDLE hFile = CreateFileW(s_crashDumpPath, GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;

    MINIDUMP_EXCEPTION_INFORMATION mei{};
    if (ep) {
        mei.ThreadId          = GetCurrentThreadId();
        mei.ExceptionPointers = ep;
        mei.ClientPointers    = FALSE;
    }
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                      static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithThreadInfo),
                      ep ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(hFile);
}

static LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS *ep)
{
    char detail[256];
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const void *addr = ep->ExceptionRecord->ExceptionAddress;
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const char *rw     = ep->ExceptionRecord->ExceptionInformation[0] == 1 ? "write" : "read";
        const void *target = reinterpret_cast<void *>(ep->ExceptionRecord->ExceptionInformation[1]);
        wsprintfA(detail, "ACCESS_VIOLATION (%s %p) at %p", rw, target, addr);
    } else {
        wsprintfA(detail, "exception code=0x%08X at %p", static_cast<unsigned>(code), addr);
    }
    writeCrashEntry(detail);
    writeCrashDump(ep);
    return EXCEPTION_CONTINUE_SEARCH; // let Windows show its normal crash dialog
}

static void terminateHandler()
{
    writeCrashEntry("std::terminate called (uncaught exception or pure-virtual call)");
    writeCrashDump(nullptr);
    std::abort();
}

static void setupCrashHandler(const QString &logPath)
{
    QString dumpPath = logPath;
    if (dumpPath.endsWith(QLatin1String(".log")))
        dumpPath.chop(4);
    dumpPath += QLatin1String("_crash.dmp");

    int n = logPath.toWCharArray(s_crashLogPath);
    s_crashLogPath[n] = L'\0';
    n = dumpPath.toWCharArray(s_crashDumpPath);
    s_crashDumpPath[n] = L'\0';

    SetUnhandledExceptionFilter(unhandledExceptionFilter);
    std::set_terminate(terminateHandler);
}
#endif
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    bool timingMode = false;

    // Perf-mode flags (all optional; --perf-scenario enables perf mode).
    bool perfMode     = false;
    bool perfBaseline = true;   // true=baseline, false=swap_early
    int  perfDefTab   = -1;     // config dt index 0-6 override (-1 = use config)
    int  perfSwapNav  = -1;     // NavBar index for swap target (-1 = auto)
    QString perfRunJson;        // path where app writes per-run JSON

    for (int i = 1; i < argc; ++i) {
        const QLatin1String arg(argv[i]);
        if (arg == "--startup-timing") {
            timingMode = true;
        } else if (arg.startsWith(QLatin1String("--perf-scenario="))) {
            perfMode     = true;
            perfBaseline = QLatin1String(argv[i] + 16) == "baseline";
        } else if (arg.startsWith(QLatin1String("--default-tab="))) {
            perfDefTab = QLatin1String(argv[i] + 14).toInt();
        } else if (arg.startsWith(QLatin1String("--perf-swap-nav="))) {
            perfSwapNav = QLatin1String(argv[i] + 16).toInt();
        } else if (arg.startsWith(QLatin1String("--perf-run-json="))) {
            perfRunJson = QString::fromUtf8(argv[i] + 16);
        }
    }

    if (timingMode) {
        qputenv("L2P_STARTUP_TIMING_MODE", "1");
        fputs("STARTUP_TIMING:started\n", stdout);
        fflush(stdout);
    }

    // Load config early so we can honour debug_log before MainWindow starts up,
    // and before QApplication initializes so we catch early crashes.
    const AppConfig earlyConfig = AppConfig::load();
    if (earlyConfig.debugLog) {
        QString logPath = AppConfig::configPath();
        logPath.chop(5); // strip ".toml"
        logPath += ".log";
        s_logFile = new QFile(logPath);
        if (s_logFile->open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
            s_logStream = new QTextStream(s_logFile);
            qInstallMessageHandler(fileMessageHandler);
#ifdef Q_OS_WIN
            setupCrashHandler(s_logFile->fileName());
#endif
        }
    }

    if (perfMode) {
        PerfProbe::instance().startClock();
        qputenv("L2P_PERF_MODE", "1");
        if (perfDefTab >= 0)
            qputenv("L2P_PERF_DEFAULT_TAB", QByteArray::number(perfDefTab));
        if (perfSwapNav >= 0)
            qputenv("L2P_PERF_SWAP_NAV", QByteArray::number(perfSwapNav));
    }

    if (!timingMode && !perfMode) {
        const int cliResult = cliDispatch(argc, argv);
        if (cliResult != -1)
            return cliResult;
    }

    // Must be set before QApplication so QtWebEngineProcess.exe inherits the flag.
    // This removes Chromium's automation-mode markers (navigator.webdriver etc.)
    // at the engine level, which is more robust than patching them in JavaScript.
    qputenv("QTWEBENGINE_CHROMIUM_FLAGS", "--disable-blink-features=AutomationControlled");

    PerfProbe::instance().markDebug("main_before_app");
    QApplication app(argc, argv);
    PerfProbe::instance().markDebug("main_after_app");
    
    app.setApplicationName("Learn to Play PoE1");
    app.setApplicationVersion("0.1.0");
    app.setQuitOnLastWindowClosed(false);

    PerfProbe::instance().markDebug("main_before_theme");
    Theme::apply(app);
    PerfProbe::instance().markDebug("main_after_theme");

    if (perfMode) {
        // Default navIdx mapping: dt → navIdx (same array as MainWindow constructor)
        static const int kNavIdx[] = { 0, 1, 1, 2, 3, 4, 4 };
        const int dt = (perfDefTab >= 0 && perfDefTab <= 6) ? perfDefTab : 6;
        const int defaultNavIdx = kNavIdx[dt];
        // Default swap target: navIdx 0 unless we start there, then navIdx 4
        const int swapNavIdx = (perfSwapNav >= 0) ? perfSwapNav
                               : (defaultNavIdx == 0 ? 4 : 0);
        PerfProbe::instance().enable(
            perfBaseline ? PerfProbe::Scenario::Baseline : PerfProbe::Scenario::SwapEarly,
            defaultNavIdx, swapNavIdx, perfRunJson);
    }

    PerfProbe::instance().markDebug("main_before_mainwindow");
    MainWindow window;
    PerfProbe::instance().markDebug("main_after_mainwindow");
    if (timingMode || perfMode || !window.startMinimized()) {
        PerfProbe::instance().markDebug("main_before_window_show");
        window.show();
        PerfProbe::instance().markDebug("main_after_window_show");
    }

    if (perfMode)
        window.publishPerfHitboxes();

    const int ret = app.exec();

    qInstallMessageHandler(nullptr);
    delete s_logStream; s_logStream = nullptr;
    delete s_logFile;   s_logFile   = nullptr;
    return ret;
}
