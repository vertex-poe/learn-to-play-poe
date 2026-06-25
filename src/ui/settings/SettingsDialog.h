#pragma once

#include "core/AppConfig.h"

#include <QDialog>

class QCheckBox;
class ListEditor;
class QLabel;
class QPushButton;
class QStackedWidget;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(AppConfig &config, QWidget *parent = nullptr);

signals:
    void configChanged();

private:
    void navigateTo(int pageIndex, const QString &title);
    void navigateBack();
    void saveAndEmit();

    AppConfig      &m_config;
    QStackedWidget *m_stack{};
    QPushButton    *m_backBtn{};
    QLabel         *m_titleLabel{};

    // Game page
    QCheckBox  *m_autoDetect{};
    ListEditor *m_installDirs{};
    ListEditor *m_exeNames{};
    QCheckBox  *m_enableOverlay{};

    // Window page
    QCheckBox  *m_startMinimized{};
    QCheckBox  *m_minimizeToTray{};

    // Chat page
    QCheckBox  *m_showGuildTags{};
};
