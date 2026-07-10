#pragma once

#include "core/AppConfig.h"

#include <QJsonObject>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class ListEditor;
class QLabel;
class QListWidget;
class QPushButton;
class QStackedWidget;
class PoeAccountStore;
class PoeInfoClient;
struct LiveEventRule;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(AppConfig &config, PoeInfoClient *poeInfoClient, QWidget *parent = nullptr);

    // Background-build all sub-pages at low priority so they're instant when clicked.
    void preloadSubPages(QObject* requestor);

    // Navigates directly to the Game category page, e.g. from the
    // no-install-dirs-configured notice's "Add install directory..." button.
    void showGamePage();

    // Provide a way to build specific sub-pages when clicked
    void buildGamePage(QWidget *parent);
    void buildOverlayPage(QWidget *parent);
    void buildWindowPage(QWidget *parent);
    void buildChatPage(QWidget *parent);
    void buildAboutPage(QWidget *parent);
    void buildAlertsPage(QWidget *parent);
    void buildDebugPage(QWidget *parent);
    void buildAccountsPage(QWidget *parent);

signals:
    void configChanged();

protected:
    void hideEvent(QHideEvent *event) override;

private:
    void navigateTo(int pageIndex, const QString &title);
    void loadPageAsync(int pageIndex, const QString &title);
    void navigateBack();
    void saveAndEmit();

    // Game page: poe-info-service proxy (see buildGamePage).
    void refreshGameSettings();
    void applyGameSettings(const QJsonObject &settings);

    // Alerts sub-page
    void alertsRebuildList();
    void alertsAddRule();
    void alertsEditRule();
    void alertsRemoveRule();
    bool alertsEditRuleDialog(LiveEventRule &rule);

    AppConfig      &m_config;
    PoeInfoClient  *m_poeInfoClient{};
    QStackedWidget *m_stack{};
    QPushButton    *m_backBtn{};
    QLabel         *m_titleLabel{};
    QPushButton    *m_debugCategoryBtn{};

    int     m_targetPageIndex = 0;
    QWidget *m_loadingPage{};
    bool    m_pageLoaded[9]{}; // track which index has been built

    // Game page
    QCheckBox  *m_autoDetect{};
    ListEditor *m_installDirs{};
    ListEditor *m_exeNames{};

    // Overlay page
    QCheckBox  *m_enableOverlay{};
    QComboBox  *m_overlayColumns{};
    QComboBox  *m_overlayRows{};
    QCheckBox  *m_overlayHideout{};
    QCheckBox  *m_overlayGuild{};
    QCheckBox  *m_overlayMenagerie{};
    QCheckBox  *m_overlayMonastery{};
    QCheckBox  *m_overlayHeist{};
    QCheckBox  *m_overlaySanctum{};
    QCheckBox  *m_overlayLadder{};
    QCheckBox  *m_overlayDelve{};
    QCheckBox  *m_overlayKingsmarch{};
    QCheckBox  *m_overlayTimePlayed{};
    QCheckBox  *m_overlayCharacterAge{};
    QCheckBox  *m_overlayPassives{};
    QCheckBox  *m_overlayDeaths{};
    QCheckBox  *m_overlayMonstersRemaining{};
    QCheckBox  *m_overlayAtlasPassives{};
    QCheckBox  *m_overlayKills{};
    QCheckBox  *m_overlayResetXP{};
    QCheckBox  *m_overlayReloadItemFilter{};
    QCheckBox  *m_overlayL2P{};

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

    // Debug page: PoE Info Service > Client (where to connect)
    QLineEdit  *m_infoServiceHost{};
    QSpinBox   *m_infoServicePort{};

    // Debug page: PoE Info Service > Server (live setting on the service itself)
    QCheckBox  *m_infoServiceDebugLogging{};
    void refreshInfoServiceDebugLogging();

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
