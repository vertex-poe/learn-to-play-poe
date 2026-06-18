#pragma once

#include "AppConfig.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(AppConfig &config, QWidget *parent = nullptr);

signals:
    void configChanged();

private:
    void onAutoDetectToggled(bool checked);
    void saveAndEmit();

    AppConfig &m_config;

    QCheckBox *m_autoDetect{};
    QLineEdit *m_installDir{};
    QLineEdit *m_winExe{};
    QLineEdit *m_linuxExe{};
    QCheckBox *m_startMinimized{};
    QCheckBox *m_minimizeToTray{};
    QCheckBox *m_autoStartOnBoot{};
};
