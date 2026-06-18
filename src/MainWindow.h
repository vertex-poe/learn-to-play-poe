#pragma once

#include "AppConfig.h"

#include <QMainWindow>
#include <QSystemTrayIcon>

class QPlainTextEdit;
class QMenu;
class SettingsDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    bool startMinimized() const { return m_config.startMinimized; }

    void log(const QString &message);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void showSettings();
    void onConfigChanged();

private:
    void showWindow();
    void setupTray();

    AppConfig m_config;

    QPlainTextEdit  *m_log{};
    QSystemTrayIcon *m_tray{};
    QMenu           *m_trayMenu{};
    SettingsDialog  *m_settingsDialog{};
};
