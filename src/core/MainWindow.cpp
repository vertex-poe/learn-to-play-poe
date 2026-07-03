// src/core/MainWindow.cpp (C++)

#include "core/MainWindow.h"
#include "core/DeferredTaskQueue.h"
#include "core/PaintProbeFilter.h"
#include "core/PerfProbe.h"
#include "util/ScopedBudget.h"
#include "ui/chat/ChatPage.h"
#include "ui/log/SessionViewPage.h"
#include "ui/chat/DmPage.h"
#include "ui/log/LogPage.h"
#include "ui/overlay/GameOverlay.h"
#include "events/LiveEventBus.h"
#include "events/LiveEventRuleEngine.h"
#include "ui/settings/SettingsPage.h"
#include "services/ServiceManager.h"
#include "services/PoeInfoClient.h"
#include "workers/TaskManager.h"
#include "ui/TaskPanel.h"
#include "platform/WindowTracker.h"

#include <QApplication>
#include <QCloseEvent>
#include <QScreen>
#include <QDateTime>
#include <QDebug>
#include <QElapsedTimer>
#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QStatusBar>
#include <QSystemTrayIcon>
#include <QTime>
#include <QTimer>
#include "ui/NavBar.h"
#include "ui/Theme.h"

#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWidget>

static QWidget *makePlaceholder(const QString &text, QWidget *parent)
{
    auto *w = new QWidget(parent);
    auto *lbl = new QLabel(text, w);
    auto *layout = new QVBoxLayout(w);
    lbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(lbl);
    return w;
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    const bool timingMode = qgetenv("L2P_STARTUP_TIMING_MODE") == "1";
    m_timingMode = timingMode;
    QElapsedTimer startupTimer;
    startupTimer.start();
    qDebug() << "[startup] begin";

    setWindowTitle("Learn to Play: Path of Exile");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    resize(720, 480);

    m_taskManager = new TaskManager(this);
    m_serviceManager = new ServiceManager(this);

    m_sessionViewPage = new SessionViewPage(this);
    m_taskPanel = new TaskPanel(m_taskManager, this);

    m_stack = new QStackedWidget(this);
    PerfProbe::instance().markDebug("mainwindow_before_logpage");
    m_logPage = new LogPage(this);
    PerfProbe::instance().markDebug("mainwindow_after_logpage");

    PerfProbe::instance().markDebug("mainwindow_before_chatpage");
    m_chatPage = new ChatPage(this);
    PerfProbe::instance().markDebug("mainwindow_after_chatpage");

    PerfProbe::instance().markDebug("mainwindow_before_dmpage");
    m_dmPage = new DmPage(this);
    PerfProbe::instance().markDebug("mainwindow_after_dmpage");

    m_stack->addWidget(makePlaceholder("Guide coming soon", this));   // TabGuide
    m_stack->addWidget(m_chatPage);                                   // TabChats
    m_stack->addWidget(makePlaceholder("Stash coming soon", this));   // TabStash
    m_stack->addWidget(makePlaceholder("Profile coming soon", this)); // TabProfile
    m_stack->addWidget(m_logPage);                                    // TabLog

    PerfProbe::instance().markDebug("mainwindow_before_navbar");
    m_navBar = new NavBar({"Guide", "Chat", "Stash", "Profile", "Log"}, this);
    PerfProbe::instance().markDebug("mainwindow_after_navbar");

    m_navBar->setCurrentIndex(TabGuide);
    m_stack->setCurrentIndex(TabGuide);
    connect(m_navBar, &NavBar::currentChanged, this, &MainWindow::onTabChanged);
    connect(m_navBar, &NavBar::tabReselected, this, [this](int index)
            {
        // In perf mode, clicking the current tab must not disrupt data loading
        // (e.g. dt=5 shows SessionViewPage on nav tab Log; reselecting Log would
        // otherwise switch the stack back to LogPage mid-load).
        if (PerfProbe::instance().enabled()) return;
        m_stack->setCurrentIndex(index); });
    connect(m_navBar, &NavBar::settingsClicked, this, &MainWindow::onGearClicked);
    connect(m_navBar, &NavBar::searchClicked, this, &MainWindow::onSearchClicked);

    auto *container = new QWidget(this);
    auto *vbox = new QVBoxLayout(container);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_navBar);
    vbox->addWidget(m_stack, 1);
    vbox->addWidget(m_taskPanel, 0);
    setCentralWidget(container);

    qDebug() << "[startup] UI built in" << startupTimer.elapsed() << "ms";
    PerfProbe::instance().markDebug("mainwindow_before_config_load");
    m_config = AppConfig::load();
    PerfProbe::instance().markDebug("mainwindow_after_config_load");
    qDebug() << "[startup] config loaded in" << startupTimer.elapsed() << "ms";

    PerfProbe::instance().markDebug("mainwindow_before_settingspage");
    m_settingsPage = nullptr;
    m_stack->addWidget(makePlaceholder("Settings (Loading...)", this)); // TabSettings placeholder
    PerfProbe::instance().markDebug("mainwindow_after_settingspage");

    m_stack->addWidget(makePlaceholder("Search coming soon", this)); // TabSearch
    m_stack->addWidget(m_sessionViewPage);                    // TabCurrent
    m_stack->addWidget(m_dmPage);                             // TabDms

    PerfProbe::instance().markDebug("mainwindow_before_ruleengine");
    // Restore default tab. DMs and Current are sub-pages (not navbar tabs), so
    // navigate to the parent navbar tab first, then override the stack index.
    const int navIdx[] = {TabGuide, TabChats, TabChats, TabStash, TabProfile, TabLog, TabLog};
    const int stackIdx[] = {TabGuide, TabChats, TabDms, TabStash, TabProfile, TabCurrent, TabLog};

    if (timingMode)
    {
        m_navBar->setCurrentIndex(TabLog);
        m_stack->setCurrentIndex(TabLog);
    }
    else
    {
        int dt = qBound(0, m_config.defaultTab, 6);

        // Perf mode can override the default tab via env var set by main().
        if (PerfProbe::instance().enabled())
        {
            const QByteArray dtEnv = qgetenv("L2P_PERF_DEFAULT_TAB");
            if (!dtEnv.isEmpty())
                dt = qBound(0, dtEnv.toInt(), 6);
        }

        m_navBar->setCurrentIndex(navIdx[dt]);
        if (stackIdx[dt] != navIdx[dt])
            m_stack->setCurrentIndex(stackIdx[dt]);

        // In perf mode: wire up dataLoaded signals and install PaintProbeFilters.
        if (PerfProbe::instance().enabled())
        {
            auto &probe = PerfProbe::instance();
            QWidget *defaultPage = m_stack->widget(stackIdx[dt]);
            probe.setDefaultPageWidget(defaultPage);

            // SessionViewPage: m_content covers m_scroll's viewport entirely, so
            // Qt never delivers QEvent::Paint to any ancestor. Call
            // onDefaultPagePainted() directly from onDefaultPageLoaded() instead.
            if (stackIdx[dt] == TabCurrent)
                probe.setDirectFinalPaint(true);

            // swapNavIdx maps directly to stack widget index for NavBar tabs 0-4.
            const int swapStack = probe.swapNavIdx(); // 0=Guide,1=Chats,2=Stash,3=Profile,4=Log
            QWidget *swapPage = m_stack->widget(swapStack);

            defaultPage->installEventFilter(
                new PaintProbeFilter(PaintProbeFilter::Default, this));
            swapPage->installEventFilter(
                new PaintProbeFilter(PaintProbeFilter::Swap, this));

            // LogPage shows a full-screen loading overlay on first paint; the overlay
            // is opaque and covers LogPage, so paint events go to the overlay rather
            // than to LogPage itself. Install a second filter on the overlay so we
            // catch the swap paint regardless of which widget is visible.
            if (swapStack == TabLog)
                m_logPage->loadingOverlay()->installEventFilter(
                    new PaintProbeFilter(PaintProbeFilter::Swap, this));

            // Connect the default page's dataLoaded signal.
            // For placeholder pages (Guide/Stash/Profile), PerfProbe auto-fires
            // first_load right after first_interaction (no async data fetch needed).
            const bool isPlaceholder = (stackIdx[dt] == TabGuide || stackIdx[dt] == TabStash || stackIdx[dt] == TabProfile);
            probe.setIsPlaceholderPage(isPlaceholder);

            if (stackIdx[dt] == TabLog)
            {
                connect(m_logPage, &LogPage::dataLoaded,
                        this, [&probe]()
                        { probe.onDefaultPageLoaded(); });
            }
            else if (stackIdx[dt] == TabChats)
            {
                connect(m_chatPage, &ChatPage::dataLoaded,
                        this, [&probe]()
                        { probe.onDefaultPageLoaded(); });
            }
            else if (stackIdx[dt] == TabDms)
            {
                connect(m_dmPage, &DmPage::dataLoaded,
                        this, [&probe]()
                        { probe.onDefaultPageLoaded(); });
            }
            else if (stackIdx[dt] == TabCurrent)
            {
                connect(m_sessionViewPage, &SessionViewPage::dataLoaded,
                        this, [&probe]()
                        { probe.onDefaultPageLoaded(); });
            }
        }
    }

    connect(m_logPage, &LogPage::viewSessionRequested,
            this, [this](qint64 sessionId, const QString &startedAt)
            {
                m_sessionViewPage->viewSession(sessionId, startedAt);
                m_stack->setCurrentIndex(TabCurrent);
                schedulePreloads(TabCurrent); });
    connect(m_sessionViewPage, &SessionViewPage::backRequested,
            this, [this]
            {
                m_stack->setCurrentIndex(TabLog);
                schedulePreloads(TabLog); });
    connect(m_chatPage, &ChatPage::viewDmsRequested,
            this, [this]
            {
                m_stack->setCurrentIndex(TabDms);
                schedulePreloads(TabDms); });
    connect(m_dmPage, &DmPage::backRequested,
            this, [this]
            {
                m_stack->setCurrentIndex(TabChats);
                schedulePreloads(TabChats); });
    connect(m_logPage, &LogPage::sessionPreviewRequested,
            this, [this](qint64 sessionId, const QString &startedAt)
            { m_sessionViewPage->preloadSession(sessionId, startedAt); });

    // Restore saved window geometry; if the saved screen no longer exists, keep
    // the saved size but let the OS decide placement.
    const WindowGeometry &wg = m_config.windowGeometry;
    if (!wg.screen.isEmpty())
    {
        bool screenFound = false;
        for (QScreen *s : QApplication::screens())
        {
            if (s->name() == wg.screen)
            {
                screenFound = true;
                break;
            }
        }
        resize(wg.width, wg.height);
        if (screenFound)
            move(wg.x, wg.y);
    }

    connect(qApp, &QCoreApplication::aboutToQuit, this, [this, timingMode]()
            {
        if (timingMode) return;
        WindowGeometry &wg = m_config.windowGeometry;
        wg.x      = x();
        wg.y      = y();
        wg.width  = width();
        wg.height = height();
        if (QScreen *s = screen())
            wg.screen = s->name();
        m_config.save(); });

    PerfProbe::instance().markDebug("mainwindow_before_gameoverlay");

    m_ruleEngine = new LiveEventRuleEngine(this);
    m_ruleEngine->setRules(m_config.liveAlertRules);
    connect(m_ruleEngine, &LiveEventRuleEngine::notifyRequested,
            this, [this](const QString &title, const QString &tag, const QString &msg)
            { log(title, tag, msg); });

    setupTray();

    m_statusLabel = new QLabel(this);
    {
        QFont f = m_statusLabel->font();
        f.setPointSizeF(Theme::fontSm);
        m_statusLabel->setFont(f);
    }
    statusBar()->addPermanentWidget(m_statusLabel);

    connect(m_taskManager, &TaskManager::taskAdded, this, &MainWindow::onTaskUpdated);
    connect(m_taskManager, &TaskManager::taskUpdated, this, &MainWindow::onTaskUpdated);

    // Empty unless a perf test harness needs an isolated db file — poe-info-service
    // resolves its own default location (poe-info-service.db next to
    // poe-info-service.toml) itself when this isn't set, since it owns the database
    // (see ServiceManager::start).
    const QByteArray dbPathOverride = qgetenv("L2P_STARTUP_TIMING_DB");
    const QString    dbPath         = QString::fromUtf8(dbPathOverride);
    const QString    installDir     = m_config.installDirs.isEmpty() ? QString() : m_config.installDirs.first();

    // poe-info-service owns the database (schema creation/migration and all
    // Client.txt ingestion) and must be up before any page requests data
    // through it — start it first and gate the rest of startup on its first
    // successful connection (onServiceReady), rather than opening a local
    // Database handle here.
    m_serviceManager->start(dbPath, installDir);
    m_poeInfoClient = new PoeInfoClient(m_serviceManager->host(), m_serviceManager->port(), this);
    m_poeInfoClient->subscribe(QStringLiteral("clientlog"), [](QJsonObject payload)
                               {
        LiveEvent ev;
        ev.type      = payload[QStringLiteral("type")].toString();
        ev.timestamp = payload[QStringLiteral("timestamp")].toString();
        ev.data      = payload[QStringLiteral("data")].toObject().toVariantMap();
        LiveEventBus::instance()->dispatch(ev); });
    connect(m_poeInfoClient, &PoeInfoClient::connected, this, &MainWindow::onServiceReady);
    if (!m_timingMode)
    {
        QTimer::singleShot(10'000, this, [this]()
                           {
            if (!m_poeInfoClient->isConnected()) {
                log(QStringLiteral("poe-info-service unavailable"), QStringLiteral("service"),
                    QStringLiteral("Chat, DM, and session history will be unavailable until it connects."));
            } });
    }

    m_tracker = WindowTracker::create();

    PerfProbe::instance().markDebug("mainwindow_before_gameoverlay_new");

    m_overlay = nullptr;
    if (!timingMode && !PerfProbe::instance().enabled())
        QTimer::singleShot(0, this, [this]()
                           {
        m_overlay = new GameOverlay(this);
        PerfProbe::instance().markDebug("mainwindow_after_gameoverlay");
        m_overlay->setLayoutGrid(m_config.overlayColumns, m_config.overlayRows);
        m_overlay->setHideoutVisible(m_config.overlayShowHideout);
        m_overlay->setGuildVisible(m_config.overlayShowGuild);
        m_overlay->setMenagerieVisible(m_config.overlayShowMenagerie);
        m_overlay->setMonasteryVisible(m_config.overlayShowMonastery);
        m_overlay->setHeistVisible(m_config.overlayShowHeist);
        m_overlay->setSanctumVisible(m_config.overlayShowSanctum);
        m_overlay->setLadderVisible(m_config.overlayShowLadder);
        m_overlay->setDelveVisible(m_config.overlayShowDelve);
        m_overlay->setKingsmarchVisible(m_config.overlayShowKingsmarch);
        m_overlay->setTimePlayedVisible(m_config.overlayShowTimePlayed);
        m_overlay->setCharacterAgeVisible(m_config.overlayShowCharacterAge);
        m_overlay->setPassivesVisible(m_config.overlayShowPassives);
        m_overlay->setDeathsVisible(m_config.overlayShowDeaths);
        m_overlay->setMonstersRemainingVisible(m_config.overlayShowMonstersRemaining);
        m_overlay->setAtlasPassivesVisible(m_config.overlayShowAtlasPassives);
        m_overlay->setKillsVisible(m_config.overlayShowKills);
        m_overlay->setResetXPVisible(m_config.overlayShowResetXP);
        m_overlay->setReloadItemFilterVisible(m_config.overlayShowReloadItemFilter);
        m_overlay->setL2PVisible(m_config.overlayShowL2P);

        connect(m_overlay, &GameOverlay::showMainWindowRequested, this, [this]() {
            showNormal();
            activateWindow();
            raise();
        }); });

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(1000);
    connect(m_pollTimer, &QTimer::timeout, this, &MainWindow::onPollTimer);
    const bool perfMode = PerfProbe::instance().enabled();
    if (!m_timingMode && !perfMode)
        m_pollTimer->start();
}

MainWindow::~MainWindow()
{
    delete m_tracker;
}

void MainWindow::publishPerfHitboxes()
{
    PerfProbe::instance().publishHitboxesAndConfig(m_navBar, this);
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
    m_tray->setToolTip("Learn to Play PoE");
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

void MainWindow::ensureSettingsPage()
{
    if (m_settingsPage)
        return;
    m_settingsPage = new SettingsPage(m_config, m_poeInfoClient, this);
    connect(m_settingsPage, &SettingsPage::configChanged,
            this, &MainWindow::onConfigChanged);
    QWidget *placeholder = m_stack->widget(TabSettings);
    m_stack->removeWidget(placeholder);
    placeholder->deleteLater();
    m_stack->insertWidget(TabSettings, m_settingsPage);
}

void MainWindow::showSettings()
{
    showWindow();
    m_navBar->setGearActive(true);
    ensureSettingsPage();
    m_stack->setCurrentIndex(TabSettings);
    schedulePreloads(TabSettings);
}

void MainWindow::schedulePreloads(int stackIndex)
{
    // Cancel any queued preloads owned by the tab we just left.
    DeferredTaskQueue::instance().cancelByRequestor(m_lastPreloadRequestor);

    // The new requestor is the page widget that corresponds to the active tab.
    // For tabs that don't enqueue into DeferredTaskQueue this is a no-op but
    // kept consistent so future preloads can use it.
    switch (stackIndex)
    {
    case TabSettings:
        m_lastPreloadRequestor = m_settingsPage;
        break;
    case TabLog:
        m_lastPreloadRequestor = m_logPage;
        break;
    case TabCurrent:
        m_lastPreloadRequestor = m_sessionViewPage;
        break;
    case TabChats:
        m_lastPreloadRequestor = m_chatPage;
        break;
    case TabDms:
        m_lastPreloadRequestor = m_dmPage;
        break;
    default:
        m_lastPreloadRequestor = nullptr;
        break;
    }

    // All navbar data pages get a low-priority background fetch when not currently visible.
    // Use a short delay so the active page's own query goes first.
    if (stackIndex != TabChats && stackIndex != TabDms)
        QTimer::singleShot(500, m_chatPage, &ChatPage::preload);
    if (stackIndex != TabLog && stackIndex != TabCurrent)
        QTimer::singleShot(500, m_logPage, &LogPage::preload);

    switch (stackIndex)
    {
    case TabChats:
        // Preload DMs (one click away via "View DMs" button)
        QTimer::singleShot(300, m_dmPage, &DmPage::preload);
        break;
    case TabDms:
        // Preload Chat (sibling — back button returns here)
        QTimer::singleShot(300, m_chatPage, &ChatPage::preload);
        break;
    case TabLog:
        // Preload current/live session (most likely "View" target)
        QTimer::singleShot(300, m_sessionViewPage, &SessionViewPage::preload);
        break;
    case TabCurrent:
        // Preload the session list in case they click back
        QTimer::singleShot(300, m_logPage, &LogPage::preload);
        break;
    case TabSettings:
        // Preload all settings sub-pages at low priority, tracked so they're
        // cancelled if the user navigates away before they execute.
        if (m_settingsPage)
        {
            QObject *req = m_settingsPage;
            QTimer::singleShot(200, this, [this, req]()
                               {
                if (m_settingsPage)
                    m_settingsPage->preloadSubPages(req); });
        }
        break;
    default:
        break;
    }
}

void MainWindow::onTabChanged(int index)
{
    m_navBar->setGearActive(false);
    m_navBar->setSearchActive(false);
    m_stack->setCurrentIndex(index);
    schedulePreloads(index);
}

void MainWindow::onGearClicked()
{
    if (m_stack->currentIndex() == TabSettings)
        onTabChanged(m_navBar->currentIndex());
    else
        showSettings();
}

void MainWindow::onSearchClicked()
{
    if (m_stack->currentIndex() == TabSearch)
    {
        m_navBar->setSearchActive(false);
        m_stack->setCurrentIndex(m_navBar->currentIndex());
    }
    else
    {
        showWindow();
        m_navBar->setSearchActive(true);
        m_stack->setCurrentIndex(TabSearch);
    }
}

void MainWindow::onServiceReady()
{
    qDebug() << "[startup] poe-info-service connected";
    const bool perfMode = PerfProbe::instance().enabled();

    m_chatPage->setShowGuildTags(m_config.showGuildTags);
    m_dmPage->setShowGuildTags(m_config.showGuildTags);

    m_chatPage->setPoeInfoClient(m_poeInfoClient);
    m_dmPage->setPoeInfoClient(m_poeInfoClient);
    m_logPage->setPoeInfoClient(m_poeInfoClient);
    m_sessionViewPage->setPoeInfoClient(m_poeInfoClient);

    // Schedule initial preloads for the current page, and background-create
    // the settings page so its landing screen is instant on first click.
    if (!m_timingMode && !perfMode)
    {
        schedulePreloads(m_stack->currentIndex());
        QTimer::singleShot(800, this, &MainWindow::ensureSettingsPage);
    }
}

void MainWindow::onConfigChanged()
{
    log("Settings saved.");
    m_chatPage->setShowGuildTags(m_config.showGuildTags);
    m_dmPage->setShowGuildTags(m_config.showGuildTags);
    m_ruleEngine->setRules(m_config.liveAlertRules);

    if (m_overlay)
    {
        m_overlay->setLayoutGrid(m_config.overlayColumns, m_config.overlayRows);
        m_overlay->setHideoutVisible(m_config.overlayShowHideout);
        m_overlay->setGuildVisible(m_config.overlayShowGuild);
        m_overlay->setMenagerieVisible(m_config.overlayShowMenagerie);
        m_overlay->setMonasteryVisible(m_config.overlayShowMonastery);
        m_overlay->setHeistVisible(m_config.overlayShowHeist);
        m_overlay->setSanctumVisible(m_config.overlayShowSanctum);
        m_overlay->setLadderVisible(m_config.overlayShowLadder);
        m_overlay->setDelveVisible(m_config.overlayShowDelve);
        m_overlay->setKingsmarchVisible(m_config.overlayShowKingsmarch);
        m_overlay->setTimePlayedVisible(m_config.overlayShowTimePlayed);
        m_overlay->setCharacterAgeVisible(m_config.overlayShowCharacterAge);
        m_overlay->setPassivesVisible(m_config.overlayShowPassives);
        m_overlay->setDeathsVisible(m_config.overlayShowDeaths);
        m_overlay->setMonstersRemainingVisible(m_config.overlayShowMonstersRemaining);
        m_overlay->setAtlasPassivesVisible(m_config.overlayShowAtlasPassives);
        m_overlay->setKillsVisible(m_config.overlayShowKills);
        m_overlay->setResetXPVisible(m_config.overlayShowResetXP);
        m_overlay->setReloadItemFilterVisible(m_config.overlayShowReloadItemFilter);
        m_overlay->setL2PVisible(m_config.overlayShowL2P);
    }
}

void MainWindow::onPollTimer()
{
    ScopedBudget budget("MainWindow::onPollTimer", 100);
    QElapsedTimer pollTimer;
    pollTimer.start();

    const QStringList exeNames = m_config.executableNames.isEmpty()
                                     ? AppConfig::knownExes()
                                     : m_config.executableNames;

    const QList<WindowState> states = m_tracker->poll(exeNames);
    const qint64 pollMs = pollTimer.elapsed();
    if (pollMs > 200)
        qDebug() << "[poll] tracker::poll took" << pollMs << "ms";

    QSet<quint32> newPids;
    for (const auto &s : states)
        newPids.insert(s.pid);

    const bool anyRunning = !states.isEmpty();

    if (m_firstPoll || newPids != m_runningPids)
    {
        m_firstPoll = false;
        m_runningPids = newPids;
        m_runningInstallDirs.clear();
        for (const auto &s : states)
            if (!s.installDir.isEmpty())
                m_runningInstallDirs << s.installDir;
        refreshStatusBar();
        m_sessionViewPage->setRunningGames(states);
    }

    // Overlay — track the first detected window's rect.
    const QRect firstRect = anyRunning ? states[0].rect : QRect{};
    if (!firstRect.isNull())
    {
        m_lastGameRect = firstRect;
    }
    else if (m_lastGameRect.isNull())
    {
        m_lastGameRect = QRect(0, 0, 1280, 720);
    }
    if (m_overlay)
    {
        m_overlay->updateGameRect(m_lastGameRect);
        m_overlay->setGameVisible(anyRunning && m_config.useGameOverlay);
        m_overlay->setGameHwnd(anyRunning ? states[0].hwnd : 0);
    }

    // Auto-detect install dirs for all running instances.
    if (m_config.autoDetectInstallDir)
    {
        for (const auto &s : states)
        {
            if (!s.installDir.isEmpty() && !m_config.installDirs.contains(s.installDir))
            {
                m_config.installDirs << s.installDir;
                m_config.save();
                log(QStringLiteral("Install directory auto-detected: %1").arg(s.installDir));
            }
        }
    }

    // Close sessions for installs where the game is no longer running.
    // poe-info-service owns ingestion and the sessions table; this just tells
    // it which installs are currently running.
    if (m_poeInfoClient && m_poeInfoClient->isConnected() && !m_orphanCloseInFlight)
    {
        m_orphanCloseInFlight = true;
        QJsonArray paths;
        for (const QString &dir : m_runningInstallDirs)
            paths.append(dir);
        m_poeInfoClient->request(QStringLiteral("sessions.closeOrphans"),
                                 QJsonObject{{QStringLiteral("running_install_paths"), paths}},
                                 [this](QJsonObject payload, QString error)
                                 {
                                     m_orphanCloseInFlight = false;
                                     if (!error.isEmpty())
                                         return;
                                     if (payload[QStringLiteral("closed")].toInt() > 0)
                                     {
                                         m_logPage->markDirty();
                                         m_sessionViewPage->markDirty();
                                     }
                                 });
    }
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason)
{
    if (reason == QSystemTrayIcon::Trigger)
        showWindow();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_config.minimizeToTray && m_tray->isVisible())
    {
        hide();
        event->ignore();
    }
    else
    {
        qApp->quit();
    }
}

void MainWindow::log(const QString &message, const NotificationStyle &style)
{
    m_sessionViewPage->addNotification(message, style);
}

void MainWindow::log(const QString &title, const QString &tag,
                     const QString &message, const NotificationStyle &style)
{
    m_sessionViewPage->addNotification(title, tag, message, style);
}

void MainWindow::onTaskUpdated(int id)
{
    const QList<TaskRecord> &all = m_taskManager->tasks();

    int active = 0, totalPct = 0, running = 0;
    QString activeLabel;
    for (const auto &r : all)
    {
        if (r.status != TaskStatus::Pending && r.status != TaskStatus::Running)
            continue;
        ++active;
        if (r.status == TaskStatus::Running)
        {
            totalPct += r.percent;
            ++running;
            if (activeLabel.isEmpty())
                activeLabel = r.name;
        }
    }

    if (active > 0)
    {
        const int pct = running > 0 ? totalPct / running : 0;
        const QString content = active == 1
                                    ? QStringLiteral("%1% · %2").arg(pct).arg(activeLabel)
                                    : QStringLiteral("%1 tasks · %2% · %3").arg(active).arg(pct).arg(activeLabel);
        setStatusContent(content);
        return;
    }

    // All tasks done — show completion for the one that just finished.
    for (const auto &r : all)
    {
        if (r.id != id)
            continue;
        const QString t = QTime::currentTime().toString("HH:mm");
        if (r.status == TaskStatus::Finished || r.status == TaskStatus::Monitoring)
        {
            setStatusContent(QStringLiteral("%1 · %2").arg(t, r.name));
        }
        else if (r.status == TaskStatus::Failed)
        {
            setStatusContent(QStringLiteral("%1 · Failed").arg(t));
        }
        else if (r.status == TaskStatus::Cancelled)
        {
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
    if (m_lastStatusContent.isEmpty())
    {
        m_statusLabel->setText(!m_runningPids.isEmpty() ? "Waiting for new game info" : "Waiting for game launch");
    }
    else
    {
        m_statusLabel->setText(m_lastStatusContent);
    }
}
