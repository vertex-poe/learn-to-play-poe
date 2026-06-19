#pragma once

#include "AppConfig.h"
#include "NotificationsPanel.h"

#include <QMainWindow>
#include <QRect>
#include <QSystemTrayIcon>

class QMenu;
class QTimer;
class Database;
class GameOverlay;
class SettingsDialog;
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

private:
    void showWindow();
    void setupTray();
    void setupMenuBar();
    void scheduleLogIngestion();
    void maybeIngestClientLog(const QString &installDir);

    AppConfig  m_config;
    Database  *m_db{};

    NotificationsPanel *m_log{};
    TaskManager        *m_taskManager{};
    TaskPanel          *m_taskPanel{};
    QSystemTrayIcon    *m_tray{};
    QMenu              *m_trayMenu{};
    SettingsDialog     *m_settingsDialog{};

    WindowTracker   *m_tracker{};
    QTimer          *m_pollTimer{};
    GameOverlay     *m_overlay{};
    bool             m_firstPoll{true};
    bool             m_gameFound{false};
    QRect            m_lastGameRect;
    QString          m_lastGameExeName;
};
