#pragma once

#include "AppConfig.h"

#include <QMainWindow>
#include <QRect>
#include <QSystemTrayIcon>

class QPlainTextEdit;
class QMenu;
class QTimer;
class GameOverlay;
class SettingsDialog;
class WindowTracker;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool startMinimized() const { return m_config.startMinimized; }

    void log(const QString &message);

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

    AppConfig m_config;

    QPlainTextEdit  *m_log{};
    QSystemTrayIcon *m_tray{};
    QMenu           *m_trayMenu{};
    SettingsDialog  *m_settingsDialog{};

    WindowTracker   *m_tracker{};
    QTimer          *m_pollTimer{};
    GameOverlay     *m_overlay{};
    bool             m_firstPoll{true};
    bool             m_gameFound{false};
    QRect            m_lastGameRect;
    QString          m_detectedInstallDir;
};
