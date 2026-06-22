#pragma once

#include "AppConfig.h"
#include "NotificationWidget.h"

#include <QMainWindow>
#include <QPointer>
#include <QRect>
#include <QSystemTrayIcon>

class QLabel;
class QStackedWidget;

class QMenu;
class ChatPage;
class CurrentPage;
class DmPage;
class NavBar;
class PastPage;
class QTimer;
class Database;
class GameOverlay;
class LiveEventRuleEngine;
class LogIngestWorker;
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

    bool startMinimized() const { return m_config.startMinimized; }

    void log(const QString &message, const NotificationStyle &style = {});
    void log(const QString &title, const QString &tag,
             const QString &message, const NotificationStyle &style = {});


protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showSettings();
    void onConfigChanged();
    void onPollTimer();
    void onTaskUpdated(int id);
    void onTabChanged(int index);
    void onGearClicked();

private:
    void showWindow();
    void setupTray();
    void scheduleLogIngestion();
    void maybeIngestClientLog(const QString &installDir, bool liveMode = false);
    void startLiveIngest(const QString &installDir);
    void stopLiveIngest();
    void setStatusContent(const QString &content);
    void refreshStatusBar();

    AppConfig  m_config;
    Database  *m_db{};

    CurrentPage        *m_currentPage{};
    TaskManager        *m_taskManager{};
    TaskPanel          *m_taskPanel{};
    ChatPage           *m_chatPage{};
    DmPage             *m_dmPage{};
    PastPage           *m_pastPage{};
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
    bool             m_gameFound{false};
    QRect            m_lastGameRect;
    QString          m_lastGameExeName;
    quint32          m_lastGamePid{};

    QPointer<LogIngestWorker> m_liveWorker;
    LiveEventRuleEngine      *m_ruleEngine{};
};
