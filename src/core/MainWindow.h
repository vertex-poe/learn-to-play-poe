#pragma once

#include "core/AppConfig.h"
#include "ui/widgets/NotificationWidget.h"

#include <QMainWindow>
#include <QRect>
#include <QSet>
#include <QSystemTrayIcon>

class QLabel;
class QStackedWidget;

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

    WindowTracker   *m_tracker{};
    QTimer          *m_pollTimer{};
    GameOverlay     *m_overlay{};
    bool             m_firstPoll{true};
    QSet<quint32>    m_runningPids;
    QStringList      m_runningInstallDirs;
    QRect            m_lastGameRect;

    LiveEventRuleEngine      *m_ruleEngine{};
    bool                      m_orphanCloseInFlight{false};
    QObject                  *m_lastPreloadRequestor{};
};
