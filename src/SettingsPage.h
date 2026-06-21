#pragma once

#include "AppConfig.h"

#include <QWidget>

class QCheckBox;
class ListEditor;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
struct LiveEventRule;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(AppConfig &config, QWidget *parent = nullptr);

signals:
    void configChanged();

private:
    void navigateTo(int pageIndex, const QString &title);
    void navigateBack();
    void saveAndEmit();

    // Alerts sub-page
    void alertsRebuildList();
    void alertsAddRule();
    void alertsEditRule();
    void alertsRemoveRule();
    bool alertsEditRuleDialog(LiveEventRule &rule);

    AppConfig      &m_config;
    QStackedWidget *m_stack{};
    QPushButton    *m_backBtn{};
    QLabel         *m_titleLabel{};

    // Game page
    QCheckBox  *m_autoDetect{};
    ListEditor *m_installDirs{};
    ListEditor *m_exeNames{};

    // Overlay page
    QCheckBox  *m_enableOverlay{};

    // Window page
    QCheckBox  *m_startMinimized{};
    QCheckBox  *m_minimizeToTray{};

    // Chat page
    QCheckBox  *m_showGuildTags{};

    // Alerts page
    QListWidget *m_alertsList{};
};
