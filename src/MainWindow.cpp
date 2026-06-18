#include "MainWindow.h"
#include "GameOverlay.h"
#include "SettingsDialog.h"
#include "WindowTracker.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QPlainTextEdit>
#include <QSystemTrayIcon>
#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Learn to Play: Path of Exile 1");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    resize(720, 480);

    m_log = new QPlainTextEdit(this);
    m_log->setReadOnly(true);
    setCentralWidget(m_log);

    m_config = AppConfig::load();

    setupMenuBar();
    setupTray();
    log(QStringLiteral("Application started. Config: %1").arg(AppConfig::configPath()));

    m_tracker = WindowTracker::create();

    m_overlay = new GameOverlay(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);
    m_pollTimer->start();
}

MainWindow::~MainWindow()
{
    delete m_tracker;
}

void MainWindow::setupMenuBar()
{
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Settings", this, &MainWindow::showSettings);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", qApp, &QApplication::quit);
}

void MainWindow::setupTray()
{
    const QIcon icon(":/icons/vertex-icon.png");

    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("Open", this, &MainWindow::showWindow);
    m_trayMenu->addAction("Settings", this, &MainWindow::showSettings);
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("Exit", qApp, &QApplication::quit);

    m_tray = new QSystemTrayIcon(icon, this);
    m_tray->setContextMenu(m_trayMenu);
    m_tray->setToolTip("Learn to Play PoE1");
    connect(m_tray, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
    m_tray->show();
}

void MainWindow::showWindow()
{
    show();
    raise();
    activateWindow();
}

void MainWindow::showSettings()
{
    if (!m_settingsDialog) {
        m_settingsDialog = new SettingsDialog(m_config, this);
        connect(m_settingsDialog, &SettingsDialog::configChanged,
                this, &MainWindow::onConfigChanged);
    }
    m_settingsDialog->show();
    m_settingsDialog->raise();
    m_settingsDialog->activateWindow();
}

void MainWindow::onConfigChanged()
{
    log("Settings saved.");
    // Reset detected state so the tracker re-evaluates with updated exe name.
    m_detectedInstallDir.clear();
}

void MainWindow::onPollTimer()
{
#ifdef Q_OS_WIN
    const QString exeName = m_config.windowsExecutableName.isEmpty()
        ? AppConfig::defaultWindowsExe : m_config.windowsExecutableName;
#else
    const QString exeName = m_config.linuxExecutableName.isEmpty()
        ? AppConfig::defaultLinuxExe : m_config.linuxExecutableName;
#endif

    const WindowState state = m_tracker->poll(exeName);

    if (m_firstPoll) {
        m_firstPoll = false;
        m_gameFound = state.found;
        if (state.found)
            log(QStringLiteral("Game is already running (%1).").arg(exeName));
    } else if (state.found != m_gameFound) {
        m_gameFound = state.found;
        if (state.found)
            log(QStringLiteral("Game started (%1).").arg(exeName));
        else
            log(QStringLiteral("Game closed (%1).").arg(exeName));
    }

    if (state.found) {
        m_lastGameRect = state.rect;
    } else if (m_lastGameRect.isNull()) {
        // Default fallback rect used before the game has ever been seen.
        m_lastGameRect = QRect(0, 0, 1280, 720);
    }

    m_overlay->updateGameRect(m_lastGameRect);
    m_overlay->setGameVisible(state.found && m_config.useGameOverlay);

    if (state.found && m_config.autoDetectInstallDir
        && !state.installDir.isEmpty()
        && state.installDir != m_detectedInstallDir) {
        m_detectedInstallDir    = state.installDir;
        m_config.installDir     = state.installDir;
        m_config.save();
        log(QStringLiteral("Install directory auto-detected: %1").arg(state.installDir));
    }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        showWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_config.minimizeToTray && m_tray->isVisible()) {
        hide();
        event->ignore();
    } else {
        qApp->quit();
    }
}

void MainWindow::log(const QString &message)
{
    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    m_log->appendPlainText(QStringLiteral("[%1] %2").arg(ts, message));
}
