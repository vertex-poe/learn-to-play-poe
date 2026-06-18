#pragma once

#include "AppConfig.h"

#include <QDialog>

class QCheckBox;
class ListEditor;

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

    QCheckBox  *m_autoDetect{};
    ListEditor *m_installDirs{};
    ListEditor *m_exeNames{};
    QCheckBox  *m_enableOverlay{};
    QCheckBox  *m_startMinimized{};
    QCheckBox  *m_minimizeToTray{};
    QCheckBox  *m_autoUpdate{};
    QCheckBox  *m_autoStartOnBoot{};
};
