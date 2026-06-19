#include "MainWindow.h"
#include "GameOverlay.h"
#include "SettingsDialog.h"
#include "TaskManager.h"
#include "TaskPanel.h"
#include "WindowTracker.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QMenu>
#include <QMenuBar>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle("Learn to Play: Path of Exile 1");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    resize(720, 480);

    m_taskManager = new TaskManager(this);

    m_log = new NotificationsPanel(this);
    m_taskPanel = new TaskPanel(m_taskManager, this);

    auto *container = new QWidget(this);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_log, 1);
    vbox->addWidget(m_taskPanel, 0);
    setCentralWidget(container);

    m_config = AppConfig::load();

    setupMenuBar();
    setupTray();
    log("Application Started", "app",
        QStringLiteral("Config at {%1}").arg(AppConfig::configPath()));

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
}

void MainWindow::onPollTimer()
{
    const QStringList exeNames = m_config.executableNames.isEmpty()
        ? AppConfig::knownExes() : m_config.executableNames;

    const WindowState state = m_tracker->poll(exeNames);

    if (state.found)
        m_lastGameExeName = state.executableName;

    if (m_firstPoll) {
        m_firstPoll = false;
        m_gameFound = state.found;
        if (state.found)
            log("Game is running", "game",
                QStringLiteral("{%1} with PID {%2} at {%3}")
                    .arg(state.executableName)
                    .arg(state.pid)
                    .arg(state.installDir));
    } else if (state.found != m_gameFound) {
        m_gameFound = state.found;
        if (state.found)
            log(QStringLiteral("Game started (%1).").arg(state.executableName));
        else
            log(QStringLiteral("Game closed (%1).").arg(m_lastGameExeName));
    }

    if (state.found && !state.rect.isNull()) {
        m_lastGameRect = state.rect;
    } else if (m_lastGameRect.isNull()) {
        // Default fallback rect used before the game has ever been seen.
        m_lastGameRect = QRect(0, 0, 1280, 720);
    }

    m_overlay->updateGameRect(m_lastGameRect);
    m_overlay->setGameVisible(state.found && m_config.useGameOverlay);

    if (state.found && m_config.autoDetectInstallDir
        && !state.installDir.isEmpty()
        && !m_config.installDirs.contains(state.installDir)) {
        m_config.installDirs << state.installDir;
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

void MainWindow::log(const QString &message, const NotificationStyle &style)
{
    m_log->addNotification(message, style);
}

void MainWindow::log(const QString &title, const QString &tag,
                     const QString &message, const NotificationStyle &style)
{
    m_log->addNotification(title, tag, message, style);
}
