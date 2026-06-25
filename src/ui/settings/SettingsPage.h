#pragma once

#include "core/AppConfig.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class ListEditor;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class PoeAccountStore;
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
    QPushButton    *m_debugCategoryBtn{};

    // Game page
    QCheckBox  *m_autoDetect{};
    ListEditor *m_installDirs{};
    ListEditor *m_exeNames{};

    // Overlay page
    QCheckBox  *m_enableOverlay{};

    // Window page
    QComboBox  *m_defaultTab{};
    QCheckBox  *m_startMinimized{};
    QCheckBox  *m_minimizeToTray{};

    // Chat page
    QCheckBox  *m_showGuildTags{};

    // Debug page
    QCheckBox  *m_debugLog{};
    QComboBox  *m_userAgent{};
    QLineEdit  *m_customUserAgent{};
    QCheckBox  *m_includeToolName{};
    QCheckBox  *m_includeQtToken{};

    // Alerts page
    QListWidget *m_alertsList{};

    // Accounts page
    PoeAccountStore *m_accountStore{};
    QPushButton     *m_accountsActionBtn{};
    bool             m_hasSession{false};
    QLabel          *m_accountsUaLabel{};
    QLineEdit       *m_accountsUaDisplay{};
    QPushButton     *m_accountsUaCopyBtn{};

    void updateAccountButton();

    // Native Chromium UA (fetched once, async)
    QString m_nativeChromiumUA;
    QString autoChromiumUA() const;
    void    refreshAutoUADisplay();
};
