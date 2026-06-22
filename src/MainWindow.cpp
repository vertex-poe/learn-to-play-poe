#include "MainWindow.h"
#include "ChatPage.h"
#include "CurrentPage.h"
#include "Database.h"
#include "DmPage.h"
#include "PastPage.h"
#include "GameOverlay.h"
#include "LiveEventBus.h"
#include "LiveEventRuleEngine.h"
#include "LogIngestWorker.h"
#include "SettingsPage.h"
#include "TaskManager.h"
#include "TaskPanel.h"
#include "WindowTracker.h"

#include <QApplication>
#include <QCloseEvent>
#include <QScreen>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTime>
#include <QTimer>
#include "NavBar.h"
#include "Theme.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    QElapsedTimer startupTimer; startupTimer.start();
    qDebug() << "[startup] begin";

    setWindowTitle("Learn to Play: Path of Exile 1");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    resize(720, 480);

    m_taskManager = new TaskManager(this);

    m_currentPage = new CurrentPage(this);
    m_taskPanel = new TaskPanel(m_taskManager, this);

    m_stack    = new QStackedWidget(this);
    m_pastPage = new PastPage(nullptr, this);
    m_chatPage = new ChatPage(nullptr, this);
    m_dmPage   = new DmPage(nullptr, this);

    m_stack->addWidget(m_pastPage);    // Past
    m_stack->addWidget(m_currentPage); // Current
    m_stack->addWidget(m_chatPage);    // Chats
    m_stack->addWidget(m_dmPage);      // DMs

    m_navBar = new NavBar({"Past", "Current", "Chats", "DMs"}, this);
    m_navBar->setCurrentIndex(1);
    m_stack->setCurrentIndex(1);
    connect(m_navBar, &NavBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_navBar, &NavBar::settingsClicked, this, &MainWindow::onGearClicked);

    auto *container = new QWidget(this);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_navBar);
    vbox->addWidget(m_stack, 1);
    vbox->addWidget(m_taskPanel, 0);
    setCentralWidget(container);

    qDebug() << "[startup] UI built in" << startupTimer.elapsed() << "ms";
    m_config = AppConfig::load();
    qDebug() << "[startup] config loaded in" << startupTimer.elapsed() << "ms";
    m_navBar->setCurrentIndex(qBound(0, m_config.defaultTab, 3));

    m_settingsPage = new SettingsPage(m_config, this);
    m_stack->addWidget(m_settingsPage); // Settings - index 4
    connect(m_settingsPage, &SettingsPage::configChanged,
            this, &MainWindow::onConfigChanged);

    // Restore saved window geometry; if the saved screen no longer exists, keep
    // the saved size but let the OS decide placement.
    const WindowGeometry &wg = m_config.windowGeometry;
    if (!wg.screen.isEmpty()) {
        bool screenFound = false;
        for (QScreen *s : QApplication::screens()) {
            if (s->name() == wg.screen) { screenFound = true; break; }
        }
        resize(wg.width, wg.height);
        if (screenFound)
            move(wg.x, wg.y);
    }

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this]() {
        WindowGeometry &wg = m_config.windowGeometry;
        wg.x      = x();
        wg.y      = y();
        wg.width  = width();
        wg.height = height();
        if (QScreen *s = screen())
            wg.screen = s->name();
        m_config.save();
    });

    m_ruleEngine = new LiveEventRuleEngine(this);
    m_ruleEngine->setRules(m_config.liveAlertRules);
    connect(m_ruleEngine, &LiveEventRuleEngine::notifyRequested,
            this, [this](const QString &title, const QString &tag, const QString &msg) {
                log(title, tag, msg);
            });

    setupTray();

    m_statusLabel = new QLabel(this);
    {
        QFont f = m_statusLabel->font();
        f.setPointSizeF(Theme::fontSm);
        m_statusLabel->setFont(f);
    }
    statusBar()->addPermanentWidget(m_statusLabel);

    connect(m_taskManager, &TaskManager::taskAdded,   this, &MainWindow::onTaskUpdated);
    connect(m_taskManager, &TaskManager::taskUpdated, this, &MainWindow::onTaskUpdated);

    QString dbPath = AppConfig::configPath();
    dbPath.chop(5); // strip ".toml"
    dbPath += ".db";
    qDebug() << "[startup] opening DB:" << dbPath;
    m_db = new Database(dbPath);
    qDebug() << "[startup] DB open in" << startupTimer.elapsed() << "ms, ok=" << m_db->isOpen();
    if (m_db->isOpen()) {
        qDebug() << "[startup] scheduleLogIngestion";
        scheduleLogIngestion();
        qDebug() << "[startup] scheduleLogIngestion done in" << startupTimer.elapsed() << "ms";
        m_currentPage->setDatabase(m_db);
        m_pastPage->setDatabase(m_db);
        m_chatPage->setDatabase(m_db);
        m_chatPage->setShowGuildTags(m_config.showGuildTags);
        m_dmPage->setDatabase(m_db);
        m_dmPage->setShowGuildTags(m_config.showGuildTags);
    } else {
        log("Database error", "db", m_db->lastError());
    }

    qDebug() << "[startup] creating WindowTracker";
    m_tracker = WindowTracker::create();
    qDebug() << "[startup] WindowTracker done in" << startupTimer.elapsed() << "ms";

    m_overlay = new GameOverlay(this);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);
    m_pollTimer->start();
}

MainWindow::~MainWindow()
{
    delete m_tracker;
    delete m_db;
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
    showWindow();
    m_navBar->setGearActive(true);
    m_stack->setCurrentIndex(4);
}

void MainWindow::onTabChanged(int index)
{
    m_navBar->setGearActive(false);
    m_stack->setCurrentIndex(index);
}

void MainWindow::onGearClicked()
{
    if (m_stack->currentIndex() == 4)
        onTabChanged(m_navBar->currentIndex());
    else
        showSettings();
}

void MainWindow::onConfigChanged()
{
    log("Settings saved.");
    m_chatPage->setShowGuildTags(m_config.showGuildTags);
    m_dmPage->setShowGuildTags(m_config.showGuildTags);
    m_ruleEngine->setRules(m_config.liveAlertRules);
}

void MainWindow::onPollTimer()
{
    const QStringList exeNames = m_config.executableNames.isEmpty()
        ? AppConfig::knownExes() : m_config.executableNames;

    const WindowState state = m_tracker->poll(exeNames);

    if (state.found) {
        m_lastGameExeName = state.executableName;
        m_lastGamePid     = state.pid;
    }

    if (m_firstPoll) {
        m_firstPoll = false;
        m_gameFound = state.found;
        refreshStatusBar();
        if (state.found)
            log("Game is running", "game",
                QStringLiteral("{%1} with PID {%2} at {%3}")
                    .arg(state.executableName)
                    .arg(state.pid)
                    .arg(state.installDir));
    } else if (state.found != m_gameFound) {
        m_gameFound = state.found;
        refreshStatusBar();
        if (state.found)
            log("Game is running", "game",
                QStringLiteral("{%1} with PID {%2} at {%3}")
                    .arg(state.executableName)
                    .arg(state.pid)
                    .arg(state.installDir));
        else
            log(QStringLiteral("Game closed (%1).").arg(m_lastGamePid));
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
        // The live-ingest block below will start watching since the game is running;
        // nothing extra needed here for newly discovered dirs.
    }

    if (m_db && m_db->isOpen()) {
        if (m_gameFound && !m_liveWorker) {
            // Game is running but we have no live tail — start one.
            // Prefer the exact dir reported by the tracker; fall back to configured dirs.
            const QStringList candidates = !state.installDir.isEmpty()
                ? QStringList{state.installDir}
                : m_config.installDirs;
            for (const QString &dir : candidates) {
                if (QFileInfo::exists(dir + "/logs/Client.txt")) {
                    startLiveIngest(dir);
                    break;
                }
            }
        } else if (!m_gameFound && m_liveWorker) {
            // Game closed — drain any remaining log content then stop.
            stopLiveIngest();
            m_pastPage->markDirty();
            m_currentPage->setDatabase(m_db);
        }
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
    m_currentPage->addNotification(message, style);
}

void MainWindow::log(const QString &title, const QString &tag,
                     const QString &message, const NotificationStyle &style)
{
    m_currentPage->addNotification(title, tag, message, style);
}

void MainWindow::scheduleLogIngestion()
{
    for (const QString &dir : m_config.installDirs)
        maybeIngestClientLog(dir);
}

void MainWindow::onTaskUpdated(int id)
{
    const QList<TaskRecord> &all = m_taskManager->tasks();

    int active = 0, totalPct = 0, running = 0;
    QString activeLabel;
    for (const auto &r : all) {
        if (r.status != TaskStatus::Pending && r.status != TaskStatus::Running)
            continue;
        ++active;
        if (r.status == TaskStatus::Running) {
            totalPct += r.percent;
            ++running;
            if (activeLabel.isEmpty())
                activeLabel = r.name.startsWith("Ingest ") ? "parsing logs" : r.name;
        }
    }

    if (active > 0) {
        const int pct = running > 0 ? totalPct / running : 0;
        const QString content = active == 1
            ? QStringLiteral("%1% · %2").arg(pct).arg(activeLabel)
            : QStringLiteral("%1 tasks · %2% · %3").arg(active).arg(pct).arg(activeLabel);
        setStatusContent(content);
        return;
    }

    // All tasks done — show completion for the one that just finished.
    for (const auto &r : all) {
        if (r.id != id) continue;
        const QString t = QTime::currentTime().toString("HH:mm");
        if (r.status == TaskStatus::Finished || r.status == TaskStatus::Monitoring) {
            if (r.name.startsWith("Ingest "))
                setStatusContent(QString());   // let idle message take over
            else
                setStatusContent(QStringLiteral("%1 · %2").arg(t, r.name));
        } else if (r.status == TaskStatus::Failed) {
            setStatusContent(QStringLiteral("%1 · Failed").arg(t));
        } else if (r.status == TaskStatus::Cancelled) {
            setStatusContent(QStringLiteral("%1 · Cancelled").arg(t));
        }
        break;
    }
}

void MainWindow::setStatusContent(const QString &content)
{
    m_lastStatusContent = content;
    refreshStatusBar();
}

void MainWindow::refreshStatusBar()
{
    if (m_lastStatusContent.isEmpty()) {
        m_statusLabel->setText(m_gameFound ? "Waiting for new game info" : "Waiting for game launch");
    } else {
        m_statusLabel->setText(m_lastStatusContent);
    }
}

void MainWindow::maybeIngestClientLog(const QString &installDir, bool liveMode)
{
    const QString logPath = installDir + "/logs/Client.txt";
    if (!QFileInfo::exists(logPath))
        return;

    const QString taskName = QStringLiteral("Ingest %1").arg(logPath);
    for (const TaskRecord &t : m_taskManager->tasks()) {
        if (t.name == taskName
                && (t.status == TaskStatus::Pending
                    || t.status == TaskStatus::Running
                    || t.status == TaskStatus::Monitoring)) {
            if (liveMode) {
                // Cancel the existing batch worker so live mode can take over
                // from its last committed offset.
                m_taskManager->cancel(t.id);
            } else {
                return;  // batch already running, don't duplicate
            }
            break;
        }
    }

    const Database::InstallState inst = m_db->upsertInstall(installDir);
    if (inst.id < 0)
        return;

    // Skip unchanged files in batch mode only — in live mode we always watch.
    if (!liveMode) {
        const QFileInfo fi(logPath);
        const bool alreadyIngested = inst.fileModifiedAt > 0
            && inst.fileModifiedAt == fi.lastModified().toSecsSinceEpoch()
            && inst.fileSize       == fi.size();
        if (alreadyIngested)
            return;
    }

    const qint64 resumeOffset = inst.lastByteOffset;

    auto *worker = new LogIngestWorker(
        m_db->path(), inst.id, logPath, resumeOffset, m_config.channelNames, liveMode);

    if (liveMode) {
        connect(worker, &LogIngestWorker::liveEventParsed,
                LiveEventBus::instance(), &LiveEventBus::dispatch,
                Qt::QueuedConnection);
    }

    m_taskManager->submit(taskName, TaskKind::DbWrite, worker);

    if (liveMode)
        m_liveWorker = worker;
}

void MainWindow::startLiveIngest(const QString &installDir)
{
    maybeIngestClientLog(installDir, /*liveMode=*/true);
}

void MainWindow::stopLiveIngest()
{
    if (m_liveWorker) {
        m_liveWorker->finalize();
        m_liveWorker = nullptr;
    }
}
