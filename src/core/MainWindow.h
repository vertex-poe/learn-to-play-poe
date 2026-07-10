#pragma once

#include "core/AppConfig.h"
#include "ui/widgets/NotificationWidget.h"

#include <QMainWindow>
#include <QRect>
#include <QSet>
#include <QSystemTrayIcon>

class QLabel;
class QStackedWidget;
class QJsonObject;

class QMenu;
class PoeInfoClient;
class ServiceManager;
class ChatPage;
class SessionViewPage;
class DmPage;
class NavBar;
class LogPage;
class QTimer;
class GameOverlay;
class LiveEventRuleEngine;
class SettingsPage;
class TaskManager;
class TaskPanel;
class WindowTracker;
class ProgressTrackerWorker;
class InstallDirNotice;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool startMinimized() const { return m_config.startMinimized && !m_timingMode; }

    void log(const QString &message, const NotificationStyle &style = {});
    void log(const QString &title, const QString &tag,
             const QString &message, const NotificationStyle &style = {});

    // Publish NavBar hitbox coordinates and config info for the perf test.
    // Call from main() right after show(), before exec().
    void publishPerfHitboxes();


protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onServiceReady();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showSettings();
    void onConfigChanged();
    void onPollTimer();
    void onTaskUpdated(int id);
    void onTabChanged(int index);
    void onGearClicked();
    void onSearchClicked();

private:
    enum Tab {
        TabGuide    = 0,
        TabChats,
        TabStash,
        TabProfile,
        TabLog,
        TabSettings,
        TabSearch,
        TabCurrent,
        TabDms,
    };

    void showWindow();
    void setupTray();
    void schedulePreloads(int stackIndex);
    void ensureSettingsPage();
    void setStatusContent(const QString &content);
    void refreshStatusBar();
    void requestIngestStatus();
    void applyStatusPayload(const QJsonObject &payload);
    void startProgressTracker();
    void stopProgressTracker();
    void refreshInstallDirsFromService();
    void applyServiceConfig(const QJsonObject &settings);

    AppConfig     m_config;
    bool          m_timingMode{false};
    ServiceManager *m_serviceManager{};
    PoeInfoClient  *m_poeInfoClient{};

    SessionViewPage    *m_sessionViewPage{};
    TaskManager        *m_taskManager{};
    TaskPanel          *m_taskPanel{};
    ChatPage           *m_chatPage{};
    DmPage             *m_dmPage{};
    LogPage           *m_logPage{};
    NavBar             *m_navBar{};
    QStackedWidget     *m_stack{};
    QSystemTrayIcon    *m_tray{};
    QMenu              *m_trayMenu{};
    SettingsPage       *m_settingsPage{};
    QLabel             *m_statusLabel{};
    QString             m_lastStatusContent;
    QString             m_ingestStatusMessage; // empty = no override; else shown in status bar

    // Ingest progress tracker (TaskPanel): shown only once a Client.txt
    // backlog replay has been running for over a second, so a quick catch-up
    // never flashes a task row — the status bar text above covers that case.
    bool                    m_ingesting{false};            // current known phase == "ingesting"
    bool                    m_ingestGraceScheduled{false};  // one pending "still ingesting after 1s?" check per episode
    int                     m_lastIngestPercent{-1};
    QString                 m_lastIngestMessage;
    int                     m_progressTrackerTaskId{-1};
    ProgressTrackerWorker  *m_progressTrackerWorker{}; // non-owning once submitted to m_taskManager

    WindowTracker   *m_tracker{};
    QTimer          *m_pollTimer{};
    GameOverlay     *m_overlay{};
    bool             m_firstPoll{true};
    QSet<quint32>    m_runningPids;
    QStringList      m_runningInstallDirs;
    QRect            m_lastGameRect;

    // Cached from poe-info-service's own config (config.list/"config" topic)
    // — no longer stored in m_config/l2p-poe.toml, see Settings > Game.
    // executableNames still drives this app's own WindowTracker::poll() for
    // overlay/running-games display; defaults to AppConfig::knownExes()
    // until the first fetch completes.
    QStringList        m_executableNames{AppConfig::knownExes()};
    InstallDirNotice   *m_installDirNotice{};

    LiveEventRuleEngine      *m_ruleEngine{};
    bool                      m_orphanCloseInFlight{false};
    QObject                  *m_lastPreloadRequestor{};
};
