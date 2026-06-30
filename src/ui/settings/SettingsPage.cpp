#include "core/DeferredTaskQueue.h"
#include "ui/settings/SettingsPage.h"
#include "core/AppConfig.h"
#include "util/Docs.h"
#include "ui/Theme.h"
#include "ui/widgets/ListEditor.h"
#include "util/PoeAccountStore.h"
#include "ui/settings/PoeLoginWindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QRegularExpression>
#include <QTimer>
#include <QWebEngineProfile>
#include <QGuiApplication>
#include <QMessageBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QApplication>
#include <QEventLoop>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QSvgRenderer>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Alert rule helpers (event/action presets)
// ---------------------------------------------------------------------------
namespace {

struct EventPreset {
    QString     label;
    QString     eventType;
    QVariantMap dataFilter;
    QString     hint;
};

struct ActionPreset {
    QString label;
    QString actionType;
};

const QVector<EventPreset> &eventPresets()
{
    static const QVector<EventPreset> v = {
        {"(any event)",              "",                {},                                                       "{type}, {timestamp}"},
        {"Whisper from player",      "whisper",         {{"direction", "from"}},                                  "{player}: {message}"},
        {"Whisper to player",        "whisper",         {{"direction", "to"}},                                    "{player}: {message}"},
        {"Area entered",             "area_entered",    {},                                                       "{area_name} (level {area_level})"},
        {"Level up",                 "level_up",        {},                                                       "{character} ({char_class}) is now level {level}"},
        {"Character death",          "character_death", {},                                                       "{character} has been slain"},
        {"Achievement unlocked",     "achievement",     {},                                                       "{name}"},
        {"Hideout discovered",       "hideout_discovered", {},                                                    "{name}"},
        {"Global chat (#)",          "chat",            {{"channel", "#"}},                                       "{player}: {message}"},
        {"Trade chat ($)",           "chat",            {{"channel", "$"}},                                       "{player}: {message}"},
        {"Party chat (%)",           "chat",            {{"channel", "%"}},                                       "{player}: {message}"},
        {"Guild chat (&)",           "chat",            {{"channel", "&"}},                                       "{player}: {message}"},
        {"Monsters cleared",         "quest_event",     {{"event_type", "monsters_cleared"}},                     ""},
        {"Passive skill point",      "quest_event",     {{"event_type", "passive_skill_point_received"}},         ""},
        {"Kitava resist penalty",    "quest_event",     {{"event_type", "kitava_resistance_penalty"}},            ""},
        {"Labyrinth craft options",  "quest_event",     {{"event_type", "labyrinth_craft_options_received"}},     ""},
        {"AFK on",                   "afk_on",          {},                                                       ""},
        {"AFK off",                  "afk_off",         {},                                                       "Duration: {duration_secs}s"},
        {"Patch required",           "general_event",   {{"event_type", "patch_required"}},                      ""},
        {"Session started",          "session_start",   {},                                                       ""},
    };
    return v;
}

const QVector<ActionPreset> &actionPresets()
{
    static const QVector<ActionPreset> v = {
        {"Show notification", "notify"},
    };
    return v;
}

int findEventPresetIndex(const LiveEventRule &rule)
{
    const auto &presets = eventPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].eventType == rule.eventType && presets[i].dataFilter == rule.dataFilter)
            return i;
    }
    return 0;
}

int findActionPresetIndex(const LiveEventRule &rule)
{
    const auto &presets = actionPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].actionType == rule.actionType)
            return i;
    }
    return 0;
}

QString ruleDescription(const LiveEventRule &rule)
{
    const QString action = rule.actionType == QLatin1String("notify")
        ? QStringLiteral("Show notification") : rule.actionType;
    const QString msg    = rule.actionParams.value("message").toString();
    const QString detail = msg.isEmpty() ? QString() : QStringLiteral(": \"%1\"").arg(msg);
    return QStringLiteral("When: %1  →  %2%3").arg(rule.label, action, detail);
}

struct UaEntry { const char *label; const char *ua; };
static const UaEntry kUserAgents[] = {
    {"Brave (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36"},
    {"Chrome (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36"},
    {"Edge (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36 Edg/149.0.0.0"},
    {"Firefox (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:152.0) Gecko/20100101 Firefox/152.0"},
};

} // namespace

SettingsPage::SettingsPage(AppConfig &config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
    m_accountStore = new PoeAccountStore(this);

    // ---- Header -------------------------------------------------------
    m_backBtn = new QPushButton("← Back", this);
    m_backBtn->setFlat(true);
    m_backBtn->setVisible(false);
    connect(m_backBtn, &QPushButton::clicked, this, &SettingsPage::navigateBack);

    m_titleLabel = new QLabel(this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);

    auto *header = new QWidget(this);
    auto *headerBox = new QHBoxLayout(header);
    headerBox->setContentsMargins(Theme::spacingXs, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
    headerBox->setSpacing(Theme::spacingSm);
    headerBox->addWidget(m_backBtn);
    headerBox->addWidget(m_titleLabel, 1);

    auto *headerSep = new QFrame(this);
    headerSep->setFrameShape(QFrame::HLine);
    headerSep->setFrameShadow(QFrame::Sunken);

    // ---- Stacked widget -----------------------------------------------
    m_stack = new QStackedWidget(this);

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);
    outerLayout->addWidget(header);
    outerLayout->addWidget(headerSep);
    outerLayout->addWidget(m_stack, 1);

    // ---- Page 0: Category list ----------------------------------------
    auto *categoryPage   = new QWidget;
    auto *categoryLayout = new QVBoxLayout(categoryPage);
    categoryLayout->setContentsMargins(Theme::spacing2xl, Theme::spacingXl, Theme::spacing2xl, Theme::spacingXl);
    categoryLayout->setSpacing(Theme::spacingBase);

    const auto makeItemBtn = [&](const QString &label, bool arrow = true) {
        auto *btn = new QPushButton(arrow ? label + "  ›" : label, categoryPage);
        QFont btnFont = btn->font();
        btnFont.setPointSizeF(Theme::fontXl);
        btn->setFont(btnFont);
        btn->setMinimumHeight(56);
        btn->setStyleSheet(
            "QPushButton { background-color: palette(button); border: none;"
            "              border-radius: 8px; padding: 8px 16px; }"
            "QPushButton:hover    { background-color: palette(light); }"
            "QPushButton:pressed  { background-color: palette(mid); }"
        );
        categoryLayout->addWidget(btn);
        return btn;
    };

    const auto addDivider = [&]() {
        auto *w = new QWidget(categoryPage);
        auto *l = new QVBoxLayout(w);
        l->setContentsMargins(Theme::spacingSm, Theme::spacingXs, Theme::spacingSm, Theme::spacingXs);
        auto *line = new QFrame(w);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        l->addWidget(line);
        categoryLayout->addWidget(w);
    };

    m_stack->addWidget(categoryPage); // index 0

    connect(makeItemBtn("Alerts"), &QPushButton::clicked,
            this, [this] { loadPageAsync(6, "Alerts"); });

    addDivider();

    connect(makeItemBtn("Accounts"), &QPushButton::clicked,
            this, [this] { loadPageAsync(8, "Accounts"); });
    connect(makeItemBtn("Game"),    &QPushButton::clicked,
            this, [this] { loadPageAsync(1, "Game"); });
    connect(makeItemBtn("Overlay"), &QPushButton::clicked,
            this, [this] { loadPageAsync(2, "Overlay"); });
    connect(makeItemBtn("Window"),  &QPushButton::clicked,
            this, [this] { loadPageAsync(3, "Window"); });
    connect(makeItemBtn("Chat"),    &QPushButton::clicked,
            this, [this] { loadPageAsync(4, "Chat"); });

    addDivider();

    m_debugCategoryBtn = makeItemBtn("Debug");
    m_debugCategoryBtn->setVisible(config.debugMode);
    connect(m_debugCategoryBtn, &QPushButton::clicked,
            this, [this] { loadPageAsync(7, "Debug"); });

    connect(makeItemBtn("About"),  &QPushButton::clicked,
            this, [this] { loadPageAsync(5, "About"); });

    connect(makeItemBtn("Exit App", false), &QPushButton::clicked, this, [this]() {
        const auto reply = QMessageBox::question(this, "Exit", "Really exit?",
                                                 QMessageBox::Yes | QMessageBox::Cancel,
                                                 QMessageBox::Yes);
        if (reply == QMessageBox::Yes)
            qApp->quit();
    });

    categoryLayout->addStretch(1);

    for (int i = 1; i <= 8; ++i) {
        m_stack->addWidget(new QWidget(this));
    }
    m_loadingPage = new QWidget(this);
    auto *loadingLayout = new QVBoxLayout(m_loadingPage);
    auto *loadingLabel = new QLabel("Loading...", m_loadingPage);
    loadingLabel->setAlignment(Qt::AlignCenter);
    QFont font = loadingLabel->font();
    font.setPointSize(16);
    loadingLabel->setFont(font);
    loadingLayout->addWidget(loadingLabel);
    m_stack->addWidget(m_loadingPage); // index 9

    auto enqueuePreload = [this](int pageIndex, const std::function<void(QWidget*)> &builder) {
        DeferredTaskQueue::instance().enqueue(QString("settings_page_%1").arg(pageIndex), DeferredTaskQueue::Low, [this, pageIndex, builder]() {
            if (!m_pageLoaded[pageIndex]) {
                QWidget *pageWidget = m_stack->widget(pageIndex);
                builder(pageWidget);
                m_pageLoaded[pageIndex] = true;
            }
            if (pageIndex == 6)
                alertsRebuildList();
            
            if (m_targetPageIndex == pageIndex) {
                m_stack->setCurrentIndex(pageIndex);
                m_targetPageIndex = 0;
            }
        });
    };

    enqueuePreload(6, [this](QWidget* w) { buildAlertsPage(w); });
    enqueuePreload(8, [this](QWidget* w) { buildAccountsPage(w); });
    enqueuePreload(1, [this](QWidget* w) { buildGamePage(w); });
    enqueuePreload(2, [this](QWidget* w) { buildOverlayPage(w); });
    enqueuePreload(3, [this](QWidget* w) { buildWindowPage(w); });
    enqueuePreload(4, [this](QWidget* w) { buildChatPage(w); });
    enqueuePreload(7, [this](QWidget* w) { buildDebugPage(w); });
    enqueuePreload(5, [this](QWidget* w) { buildAboutPage(w); });

    // Fetch the native Chromium UA once without blocking the constructor.
    // Removed the problematic QTimer::singleShot that crashes QtWebEngine on startup.
    // m_nativeChromiumUA will be fetched lazily in autoChromiumUA().

    connect(this, &SettingsPage::configChanged, this, [this]() {
        qDebug() << "configChanged lambda start";
        const bool debug = m_config.debugMode;
        m_debugCategoryBtn->setVisible(debug);
        if (m_accountsUaLabel) m_accountsUaLabel->setVisible(debug);
        if (m_accountsUaDisplay) {
            m_accountsUaDisplay->setVisible(debug);
            const QString displayUa = m_config.debugLegacyUserAgent == QLatin1String("Auto (Chromium)")
                                      ? autoChromiumUA() : m_config.effectiveUserAgent();
            m_accountsUaDisplay->setText(displayUa);
        }
        if (m_accountsUaCopyBtn) m_accountsUaCopyBtn->setVisible(debug);
    });
}

void SettingsPage::buildGamePage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);

    // ---- Page 1: Game -------------------------------------------------
    auto *gameScroll = new QScrollArea;
    gameScroll->setWidgetResizable(true);
    gameScroll->setFrameShape(QFrame::NoFrame);

    auto *gameContent = new QWidget;
    auto *gameForm    = new QFormLayout(gameContent);
    gameForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_autoDetect = new QCheckBox(gameContent);
    m_autoDetect->setChecked(m_config.autoDetectInstallDir);
    gameForm->addRow("Auto-detect install directories:", m_autoDetect);

    m_installDirs = new ListEditor({}, gameContent);
    m_installDirs->setBrowseForDirectory(true);
    m_installDirs->setItems(m_config.installDirs);
    gameForm->addRow("Install directories:", m_installDirs);

    m_exeNames = new ListEditor("Executable name (e.g. PathOfExile_x64Steam.exe)", gameContent);
    m_exeNames->setBuiltinItems(AppConfig::knownExes());
    m_exeNames->setItems(m_config.executableNames);
    m_exeNames->setInputFileBrowser(true);
    gameForm->addRow("Executable names:", m_exeNames);

    gameScroll->setWidget(gameContent);
    parentLayout->addWidget(gameScroll);
    connect(m_autoDetect,     &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs,    &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
    connect(m_exeNames,       &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
}

void SettingsPage::buildOverlayPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 2: Overlay -----------------------------------------------
    auto *overlayContent = new QWidget;
    auto *overlayForm    = new QFormLayout(overlayContent);
    overlayForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_enableOverlay = new QCheckBox(overlayContent);
    m_enableOverlay->setChecked(m_config.useGameOverlay);
    overlayForm->addRow("Enable overlay:", m_enableOverlay);

    auto *layoutContainer = new QWidget(overlayContent);
    auto *layoutHBox = new QHBoxLayout(layoutContainer);
    layoutHBox->setContentsMargins(0, 0, 0, 0);

    m_overlayColumns = new QComboBox(layoutContainer);
    m_overlayColumns->addItem("Auto");
    for (int i = 1; i <= 20; ++i) {
        m_overlayColumns->addItem(QString::number(i));
    }
    m_overlayColumns->setCurrentIndex(m_config.overlayColumns);

    m_overlayRows = new QComboBox(layoutContainer);
    m_overlayRows->addItem("Auto");
    for (int i = 1; i <= 20; ++i) {
        m_overlayRows->addItem(QString::number(i));
    }
    m_overlayRows->setCurrentIndex(m_config.overlayRows);

    connect(m_overlayColumns, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 0) {
            QSignalBlocker blocker(m_overlayRows);
            m_overlayRows->setCurrentIndex(1);
        } else if (index > 0 && m_overlayRows->currentIndex() > 0) {
            QSignalBlocker blocker(m_overlayRows);
            m_overlayRows->setCurrentIndex(0);
        }
    });
    connect(m_overlayRows, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index == 0) {
            QSignalBlocker blocker(m_overlayColumns);
            m_overlayColumns->setCurrentIndex(1);
        } else if (index > 0 && m_overlayColumns->currentIndex() > 0) {
            QSignalBlocker blocker(m_overlayColumns);
            m_overlayColumns->setCurrentIndex(0);
        }
    });
    
    auto *columnsLabel = new QLabel("Columns:", layoutContainer);
    columnsLabel->setToolTip("Vertical");
    layoutHBox->addWidget(columnsLabel);
    layoutHBox->addWidget(m_overlayColumns);
    layoutHBox->addSpacing(10);
    auto *rowsLabel = new QLabel("Rows:", layoutContainer);
    rowsLabel->setToolTip("Horizontal");
    layoutHBox->addWidget(rowsLabel);
    layoutHBox->addWidget(m_overlayRows);
    layoutHBox->addStretch();
    
    overlayForm->addRow("Icon layout:", layoutContainer);

    auto *teleportHeader = new QLabel("<b>Teleport Shortcuts</b>", overlayContent);
    overlayForm->addRow(teleportHeader);

    m_overlayL2P = new QCheckBox(overlayContent);
    m_overlayL2P->setChecked(m_config.overlayShowL2P);
    auto *l2pLabel = new QLabel("<span style=\"color: #c8a84b; font-family: 'Palatino Linotype', 'Book Antiqua', 'Palatino', serif; font-size: 14px; font-weight: bold; font-style: italic; letter-spacing: 2px;\">l2p</span> App Focus:", overlayContent);
    overlayForm->addRow(l2pLabel, m_overlayL2P);

    m_overlayHideout = new QCheckBox(overlayContent);
    m_overlayHideout->setChecked(m_config.overlayShowHideout);
    m_overlayHideout->setToolTip("/hideout");
    auto *hideoutLabel = new QLabel("<img src=':/icons/fleur-de-lis.svg' width='18' height='18' style='vertical-align: middle;'> Hideout:", overlayContent);
    hideoutLabel->setToolTip("/hideout");
    overlayForm->addRow(hideoutLabel, m_overlayHideout);

    const auto addPlaceholder = [&](const QString &label, const QString &cmd, bool checked) {
        auto *l = new QLabel(label + ":", overlayContent);
        if (!cmd.isEmpty()) {
            l->setToolTip(cmd);
        }
        auto *cb = new QCheckBox(overlayContent);
        cb->setChecked(checked);
        cb->setEnabled(false);
        if (!cmd.isEmpty()) {
            cb->setToolTip(cmd);
        }
        overlayForm->addRow(l, cb);
    };

    m_overlayGuild = new QCheckBox(overlayContent);
    m_overlayGuild->setChecked(m_config.overlayShowGuild);
    m_overlayGuild->setToolTip("/guild");
    
    auto *guildLabelWidget = new QWidget(overlayContent);
    guildLabelWidget->setToolTip("/guild");
    auto *guildLabelLayout = new QHBoxLayout(guildLabelWidget);
    guildLabelLayout->setContentsMargins(0, 0, 0, 0);
    guildLabelLayout->setSpacing(4);
    
    auto *guildIconLabel = new QLabel(guildLabelWidget);
    guildIconLabel->setPixmap(QIcon(":/icons/fleur-de-lis-shield.svg").pixmap(18, 18));
    
    auto *guildTextLabel = new QLabel("Guild:", guildLabelWidget);
    guildLabelLayout->addWidget(guildIconLabel);
    guildLabelLayout->addWidget(guildTextLabel);
    guildLabelLayout->addStretch();
    
    overlayForm->addRow(guildLabelWidget, m_overlayGuild);

    m_overlayMenagerie = new QCheckBox(overlayContent);
    m_overlayMenagerie->setChecked(m_config.overlayShowMenagerie);
    m_overlayMenagerie->setToolTip("/menagerie");
    
    auto *menagerieLabelWidget = new QWidget(overlayContent);
    menagerieLabelWidget->setToolTip("/menagerie");
    auto *menagerieLabelLayout = new QHBoxLayout(menagerieLabelWidget);
    menagerieLabelLayout->setContentsMargins(0, 0, 0, 0);
    menagerieLabelLayout->setSpacing(4);
    
    auto *menagerieIconLabel = new QLabel(menagerieLabelWidget);
    menagerieIconLabel->setPixmap(QIcon(":/icons/cattle-skull.svg").pixmap(18, 18));
    
    auto *menagerieTextLabel = new QLabel("Menagerie:", menagerieLabelWidget);
    menagerieLabelLayout->addWidget(menagerieIconLabel);
    menagerieLabelLayout->addWidget(menagerieTextLabel);
    menagerieLabelLayout->addStretch();
    
    overlayForm->addRow(menagerieLabelWidget, m_overlayMenagerie);

    m_overlayMonastery = new QCheckBox(overlayContent);
    m_overlayMonastery->setChecked(m_config.overlayShowMonastery);
    m_overlayMonastery->setToolTip("/monastery");
    
    auto *monasteryLabelWidget = new QWidget(overlayContent);
    monasteryLabelWidget->setToolTip("/monastery");
    auto *monasteryLabelLayout = new QHBoxLayout(monasteryLabelWidget);
    monasteryLabelLayout->setContentsMargins(0, 0, 0, 0);
    monasteryLabelLayout->setSpacing(4);
    
    auto *monasteryIconLabel = new QLabel(monasteryLabelWidget);
    monasteryIconLabel->setPixmap(QIcon(":/icons/branch.svg").pixmap(18, 18));
    
    auto *monasteryTextLabel = new QLabel("Monastery:", monasteryLabelWidget);
    monasteryLabelLayout->addWidget(monasteryIconLabel);
    monasteryLabelLayout->addWidget(monasteryTextLabel);
    monasteryLabelLayout->addStretch();
    
    overlayForm->addRow(monasteryLabelWidget, m_overlayMonastery);

    m_overlayHeist = new QCheckBox(overlayContent);
    m_overlayHeist->setChecked(m_config.overlayShowHeist);
    m_overlayHeist->setToolTip("/heist");
    auto *heistLabelWidget = new QWidget(overlayContent);
    heistLabelWidget->setToolTip("/heist");
    auto *heistLabelLayout = new QHBoxLayout(heistLabelWidget);
    heistLabelLayout->setContentsMargins(0, 0, 0, 0);
    heistLabelLayout->setSpacing(4);
    auto *heistIconLabel = new QLabel(heistLabelWidget);
    heistIconLabel->setPixmap(QIcon(":/icons/safe2-fill.svg").pixmap(18, 18));
    auto *heistTextLabel = new QLabel("Heist:", heistLabelWidget);
    heistLabelLayout->addWidget(heistIconLabel);
    heistLabelLayout->addWidget(heistTextLabel);
    heistLabelLayout->addStretch();
    overlayForm->addRow(heistLabelWidget, m_overlayHeist);

    m_overlaySanctum = new QCheckBox(overlayContent);
    m_overlaySanctum->setChecked(m_config.overlayShowSanctum);
    m_overlaySanctum->setToolTip("/sanctum");
    auto *sanctumLabelWidget = new QWidget(overlayContent);
    sanctumLabelWidget->setToolTip("/sanctum");
    auto *sanctumLabelLayout = new QHBoxLayout(sanctumLabelWidget);
    sanctumLabelLayout->setContentsMargins(0, 0, 0, 0);
    sanctumLabelLayout->setSpacing(4);
    auto *sanctumIconLabel = new QLabel(sanctumLabelWidget);
    sanctumIconLabel->setPixmap(QIcon(":/icons/door-open-fill.svg").pixmap(18, 18));
    auto *sanctumTextLabel = new QLabel("Sanctum:", sanctumLabelWidget);
    sanctumLabelLayout->addWidget(sanctumIconLabel);
    sanctumLabelLayout->addWidget(sanctumTextLabel);
    sanctumLabelLayout->addStretch();
    overlayForm->addRow(sanctumLabelWidget, m_overlaySanctum);

    m_overlayDelve = new QCheckBox(overlayContent);
    m_overlayDelve->setChecked(m_config.overlayShowDelve);
    m_overlayDelve->setToolTip("/delve");
    auto *delveLabelWidget = new QWidget(overlayContent);
    delveLabelWidget->setToolTip("/delve");
    auto *delveLabelLayout = new QHBoxLayout(delveLabelWidget);
    delveLabelLayout->setContentsMargins(0, 0, 0, 0);
    delveLabelLayout->setSpacing(4);
    auto *delveIconLabel = new QLabel(delveLabelWidget);
    delveIconLabel->setPixmap(QIcon(":/icons/minecart-loaded.svg").pixmap(18, 18));
    auto *delveTextLabel = new QLabel("Delve:", delveLabelWidget);
    delveLabelLayout->addWidget(delveIconLabel);
    delveLabelLayout->addWidget(delveTextLabel);
    delveLabelLayout->addStretch();
    overlayForm->addRow(delveLabelWidget, m_overlayDelve);
    m_overlayKingsmarch = new QCheckBox(overlayContent);
    m_overlayKingsmarch->setChecked(m_config.overlayShowKingsmarch);
    m_overlayKingsmarch->setToolTip("/kingsmarch");
    auto *kingsmarchLabelWidget = new QWidget(overlayContent);
    kingsmarchLabelWidget->setToolTip("/kingsmarch");
    auto *kingsmarchLabelLayout = new QHBoxLayout(kingsmarchLabelWidget);
    kingsmarchLabelLayout->setContentsMargins(0, 0, 0, 0);
    kingsmarchLabelLayout->setSpacing(4);
    auto *kingsmarchIconLabel = new QLabel(kingsmarchLabelWidget);
    kingsmarchIconLabel->setPixmap(QIcon(":/icons/shop.svg").pixmap(18, 18));
    auto *kingsmarchTextLabel = new QLabel("Kingsmarch:", kingsmarchLabelWidget);
    kingsmarchLabelLayout->addWidget(kingsmarchIconLabel);
    kingsmarchLabelLayout->addWidget(kingsmarchTextLabel);
    kingsmarchLabelLayout->addStretch();
    overlayForm->addRow(kingsmarchLabelWidget, m_overlayKingsmarch);


    auto *infoHeader = new QLabel("<br><b>Informational</b>", overlayContent);
    overlayForm->addRow(infoHeader);

    m_overlayLadder = new QCheckBox(overlayContent);
    m_overlayLadder->setChecked(m_config.overlayShowLadder);
    m_overlayLadder->setToolTip("/ladder");
    auto *ladderLabelWidget = new QWidget(overlayContent);
    ladderLabelWidget->setToolTip("/ladder");
    auto *ladderLabelLayout = new QHBoxLayout(ladderLabelWidget);
    ladderLabelLayout->setContentsMargins(0, 0, 0, 0);
    ladderLabelLayout->setSpacing(4);
    auto *ladderIconLabel = new QLabel(ladderLabelWidget);
    ladderIconLabel->setPixmap(QIcon(":/icons/trophy-fill.svg").pixmap(18, 18));
    auto *ladderTextLabel = new QLabel("Top 10 Ladder:", ladderLabelWidget);
    ladderLabelLayout->addWidget(ladderIconLabel);
    ladderLabelLayout->addWidget(ladderTextLabel);
    ladderLabelLayout->addStretch();
    overlayForm->addRow(ladderLabelWidget, m_overlayLadder);

    m_overlayTimePlayed = new QCheckBox(overlayContent);
    m_overlayTimePlayed->setChecked(m_config.overlayShowTimePlayed);
    m_overlayTimePlayed->setToolTip("/played");
    auto *timeplayedLabelWidget = new QWidget(overlayContent);
    timeplayedLabelWidget->setToolTip("/played");
    auto *timeplayedLabelLayout = new QHBoxLayout(timeplayedLabelWidget);
    timeplayedLabelLayout->setContentsMargins(0, 0, 0, 0);
    timeplayedLabelLayout->setSpacing(4);
    auto *timeplayedIconLabel = new QLabel(timeplayedLabelWidget);
    timeplayedIconLabel->setPixmap(QIcon(":/icons/stopwatch-fill.svg").pixmap(18, 18));
    auto *timeplayedTextLabel = new QLabel("Time Played:", timeplayedLabelWidget);
    timeplayedLabelLayout->addWidget(timeplayedIconLabel);
    timeplayedLabelLayout->addWidget(timeplayedTextLabel);
    timeplayedLabelLayout->addStretch();
    overlayForm->addRow(timeplayedLabelWidget, m_overlayTimePlayed);
    m_overlayCharacterAge = new QCheckBox(overlayContent);
    m_overlayCharacterAge->setChecked(m_config.overlayShowCharacterAge);
    m_overlayCharacterAge->setToolTip("/age");
    auto *characterageLabelWidget = new QWidget(overlayContent);
    characterageLabelWidget->setToolTip("/age");
    auto *characterageLabelLayout = new QHBoxLayout(characterageLabelWidget);
    characterageLabelLayout->setContentsMargins(0, 0, 0, 0);
    characterageLabelLayout->setSpacing(4);
    auto *characterageIconLabel = new QLabel(characterageLabelWidget);
    characterageIconLabel->setPixmap(QIcon(":/icons/stopwatch-fill.svg").pixmap(18, 18));
    auto *characterageTextLabel = new QLabel("Character Age:", characterageLabelWidget);
    characterageLabelLayout->addWidget(characterageIconLabel);
    characterageLabelLayout->addWidget(characterageTextLabel);
    characterageLabelLayout->addStretch();
    overlayForm->addRow(characterageLabelWidget, m_overlayCharacterAge);
    m_overlayPassives = new QCheckBox(overlayContent);
    m_overlayPassives->setChecked(m_config.overlayShowPassives);
    m_overlayPassives->setToolTip("/passives");
    auto *passivesLabelWidget = new QWidget(overlayContent);
    passivesLabelWidget->setToolTip("/passives");
    auto *passivesLabelLayout = new QHBoxLayout(passivesLabelWidget);
    passivesLabelLayout->setContentsMargins(0, 0, 0, 0);
    passivesLabelLayout->setSpacing(4);
    auto *passivesIconLabel = new QLabel(passivesLabelWidget);
    passivesIconLabel->setPixmap(QIcon(":/icons/tree-fill.svg").pixmap(18, 18));
    auto *passivesTextLabel = new QLabel("Passives:", passivesLabelWidget);
    passivesLabelLayout->addWidget(passivesIconLabel);
    passivesLabelLayout->addWidget(passivesTextLabel);
    passivesLabelLayout->addStretch();
    overlayForm->addRow(passivesLabelWidget, m_overlayPassives);
    m_overlayDeaths = new QCheckBox(overlayContent);
    m_overlayDeaths->setChecked(m_config.overlayShowDeaths);
    m_overlayDeaths->setToolTip("/deaths");
    auto *deathsLabelWidget = new QWidget(overlayContent);
    deathsLabelWidget->setToolTip("/deaths");
    auto *deathsLabelLayout = new QHBoxLayout(deathsLabelWidget);
    deathsLabelLayout->setContentsMargins(0, 0, 0, 0);
    deathsLabelLayout->setSpacing(4);
    auto *deathsIconLabel = new QLabel(deathsLabelWidget);
    deathsIconLabel->setPixmap(QIcon(":/icons/person-fill.svg").pixmap(18, 18));
    auto *deathsTextLabel = new QLabel("Deaths:", deathsLabelWidget);
    deathsLabelLayout->addWidget(deathsIconLabel);
    deathsLabelLayout->addWidget(deathsTextLabel);
    deathsLabelLayout->addStretch();
    overlayForm->addRow(deathsLabelWidget, m_overlayDeaths);
    m_overlayMonstersRemaining = new QCheckBox(overlayContent);
    m_overlayMonstersRemaining->setChecked(m_config.overlayShowMonstersRemaining);
    m_overlayMonstersRemaining->setToolTip("/remaining");
    auto *monstersremainingLabelWidget = new QWidget(overlayContent);
    monstersremainingLabelWidget->setToolTip("/remaining");
    auto *monstersremainingLabelLayout = new QHBoxLayout(monstersremainingLabelWidget);
    monstersremainingLabelLayout->setContentsMargins(0, 0, 0, 0);
    monstersremainingLabelLayout->setSpacing(4);
    auto *monstersremainingIconLabel = new QLabel(monstersremainingLabelWidget);
    monstersremainingIconLabel->setPixmap(QIcon(":/icons/bug-fill.svg").pixmap(18, 18));
    auto *monstersremainingTextLabel = new QLabel("Monsters Remaining:", monstersremainingLabelWidget);
    monstersremainingLabelLayout->addWidget(monstersremainingIconLabel);
    monstersremainingLabelLayout->addWidget(monstersremainingTextLabel);
    monstersremainingLabelLayout->addStretch();
    overlayForm->addRow(monstersremainingLabelWidget, m_overlayMonstersRemaining);
    m_overlayAtlasPassives = new QCheckBox(overlayContent);
    m_overlayAtlasPassives->setChecked(m_config.overlayShowAtlasPassives);
    m_overlayAtlasPassives->setToolTip("/atlaspassives");
    auto *atlaspassivesLabelWidget = new QWidget(overlayContent);
    atlaspassivesLabelWidget->setToolTip("/atlaspassives");
    auto *atlaspassivesLabelLayout = new QHBoxLayout(atlaspassivesLabelWidget);
    atlaspassivesLabelLayout->setContentsMargins(0, 0, 0, 0);
    atlaspassivesLabelLayout->setSpacing(4);
    auto *atlaspassivesIconLabel = new QLabel(atlaspassivesLabelWidget);
    atlaspassivesIconLabel->setPixmap(QIcon(":/icons/map-fill.svg").pixmap(18, 18));
    auto *atlaspassivesTextLabel = new QLabel("Atlas Passives:", atlaspassivesLabelWidget);
    atlaspassivesLabelLayout->addWidget(atlaspassivesIconLabel);
    atlaspassivesLabelLayout->addWidget(atlaspassivesTextLabel);
    atlaspassivesLabelLayout->addStretch();
    overlayForm->addRow(atlaspassivesLabelWidget, m_overlayAtlasPassives);
    m_overlayKills = new QCheckBox(overlayContent);
    m_overlayKills->setChecked(m_config.overlayShowKills);
    m_overlayKills->setToolTip("/kills");
    auto *killsLabelWidget = new QWidget(overlayContent);
    killsLabelWidget->setToolTip("/kills");
    auto *killsLabelLayout = new QHBoxLayout(killsLabelWidget);
    killsLabelLayout->setContentsMargins(0, 0, 0, 0);
    killsLabelLayout->setSpacing(4);
    auto *killsIconLabel = new QLabel(killsLabelWidget);
    killsIconLabel->setPixmap(QIcon(":/icons/bullseye.svg").pixmap(18, 18));
    auto *killsTextLabel = new QLabel("Kills:", killsLabelWidget);
    killsLabelLayout->addWidget(killsIconLabel);
    killsLabelLayout->addWidget(killsTextLabel);
    killsLabelLayout->addStretch();
    overlayForm->addRow(killsLabelWidget, m_overlayKills);
    m_overlayResetXP = new QCheckBox(overlayContent);
    m_overlayResetXP->setChecked(m_config.overlayShowResetXP);
    m_overlayResetXP->setToolTip("/reset_xp");
    auto *resetxpLabelWidget = new QWidget(overlayContent);
    resetxpLabelWidget->setToolTip("/reset_xp");
    auto *resetxpLabelLayout = new QHBoxLayout(resetxpLabelWidget);
    resetxpLabelLayout->setContentsMargins(0, 0, 0, 0);
    resetxpLabelLayout->setSpacing(4);
    auto *resetxpIconLabel = new QLabel(resetxpLabelWidget);
    resetxpIconLabel->setPixmap(QIcon(":/icons/box-arrow-in-right.svg").pixmap(18, 18));
    auto *resetxpTextLabel = new QLabel("Reset XP:", resetxpLabelWidget);
    resetxpLabelLayout->addWidget(resetxpIconLabel);
    resetxpLabelLayout->addWidget(resetxpTextLabel);
    resetxpLabelLayout->addStretch();
    overlayForm->addRow(resetxpLabelWidget, m_overlayResetXP);
    m_overlayReloadItemFilter = new QCheckBox(overlayContent);
    m_overlayReloadItemFilter->setChecked(m_config.overlayShowReloadItemFilter);
    m_overlayReloadItemFilter->setToolTip("/reloaditemfilter");
    auto *reloaditemfilterLabelWidget = new QWidget(overlayContent);
    reloaditemfilterLabelWidget->setToolTip("/reloaditemfilter");
    auto *reloaditemfilterLabelLayout = new QHBoxLayout(reloaditemfilterLabelWidget);
    reloaditemfilterLabelLayout->setContentsMargins(0, 0, 0, 0);
    reloaditemfilterLabelLayout->setSpacing(4);
    auto *reloaditemfilterIconLabel = new QLabel(reloaditemfilterLabelWidget);
    reloaditemfilterIconLabel->setPixmap(QIcon(":/icons/indent.svg").pixmap(18, 18));
    auto *reloaditemfilterTextLabel = new QLabel("Reload Item Filter:", reloaditemfilterLabelWidget);
    reloaditemfilterLabelLayout->addWidget(reloaditemfilterIconLabel);
    reloaditemfilterLabelLayout->addWidget(reloaditemfilterTextLabel);
    reloaditemfilterLabelLayout->addStretch();
    overlayForm->addRow(reloaditemfilterLabelWidget, m_overlayReloadItemFilter);

    parentLayout->addWidget(overlayContent);
    connect(m_enableOverlay,  &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_overlayColumns, &QComboBox::currentIndexChanged, this, [this](int) { saveAndEmit(); });
    connect(m_overlayRows,    &QComboBox::currentIndexChanged, this, [this](int) { saveAndEmit(); });
    connect(m_overlayHideout, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_overlayGuild,   &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_overlayMenagerie,&QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlayMonastery,&QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlayHeist,    &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlaySanctum,  &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlayLadder,   &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlayDelve,    &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_overlayKingsmarch, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayTimePlayed, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayCharacterAge, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayPassives, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayDeaths, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayMonstersRemaining, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayAtlasPassives, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayKills, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayResetXP, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayReloadItemFilter, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_overlayL2P,     &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::buildWindowPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 3: Window -----------------------------------------------
    auto *windowContent = new QWidget;
    auto *windowForm    = new QFormLayout(windowContent);
    windowForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_defaultTab = new QComboBox(windowContent);
    m_defaultTab->addItems({"Guide", "Chat", "DMs", "Stash", "Profile", "Current Log", "Past Logs"});
    m_defaultTab->setCurrentIndex(qBound(0, m_config.defaultTab, 6));
    m_defaultTab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    windowForm->addRow("Default tab:", m_defaultTab);

    m_startMinimized = new QCheckBox(windowContent);
    m_startMinimized->setChecked(m_config.startMinimized);
    windowForm->addRow("Start minimized:", m_startMinimized);

    m_minimizeToTray = new QCheckBox(windowContent);
    m_minimizeToTray->setChecked(m_config.minimizeToTray);
    windowForm->addRow("Minimize to tray on close:", m_minimizeToTray);

    parentLayout->addWidget(windowContent);
    connect(m_defaultTab,     &QComboBox::currentIndexChanged, this, [this](int) { saveAndEmit(); });
    connect(m_startMinimized, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::buildChatPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 4: Chat -------------------------------------------------
    auto *chatContent = new QWidget;
    auto *chatForm    = new QFormLayout(chatContent);
    chatForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_showGuildTags = new QCheckBox(chatContent);
    m_showGuildTags->setChecked(m_config.showGuildTags);
    chatForm->addRow("Display guild tags:", m_showGuildTags);

    parentLayout->addWidget(chatContent);
    connect(m_showGuildTags,   &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::buildAboutPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 5: About ------------------------------------------------
    auto *aboutContent = new QWidget;
    auto *aboutLayout  = new QVBoxLayout(aboutContent);
    aboutLayout->setContentsMargins(Theme::spacingBase, Theme::spacingLg, Theme::spacingBase, Theme::spacingLg);
    aboutLayout->setSpacing(Theme::spacingSm);

    // Short centered HR: stretch 3:2:3 gives the line 25% of the width
    const auto makeSep = [&]() -> QWidget * {
        auto *w = new QWidget(aboutContent);
        auto *h = new QHBoxLayout(w);
        h->setContentsMargins(0, Theme::spacingSm, 0, Theme::spacingSm);
        auto *line = new QFrame(w);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        h->addStretch(3);
        h->addWidget(line, 2);
        h->addStretch(3);
        return w;
    };

    auto *appTitleLabel = new QLabel("Learn to Play", aboutContent);
    {
        QFont f = appTitleLabel->font();
        f.setPointSizeF(Theme::font4xl);
        f.setBold(true);
        appTitleLabel->setFont(f);
        appTitleLabel->setAlignment(Qt::AlignCenter);
    }

    auto *gameLabel = new QLabel("Path of Exile 1", aboutContent);
    {
        QFont f = gameLabel->font();
        f.setPointSizeF(Theme::font3xl);
        gameLabel->setFont(f);
        gameLabel->setAlignment(Qt::AlignCenter);
    }

    auto *versionLabel = new QLabel(
        QStringLiteral("Version %1").arg(QCoreApplication::applicationVersion()), aboutContent);
    versionLabel->setAlignment(Qt::AlignCenter);

    aboutLayout->addWidget(appTitleLabel);
    aboutLayout->addWidget(gameLabel);
    aboutLayout->addWidget(versionLabel);
    aboutLayout->addWidget(makeSep());

    auto *presentedByLabel = new QLabel("Presented by:", aboutContent);
    presentedByLabel->setAlignment(Qt::AlignCenter);

    auto *vertexRow = new QWidget(aboutContent);
    {
        const int iconPx = qRound(Theme::fontXl * 1.5);
        QPixmap iconPix(iconPx, iconPx);
        iconPix.fill(Qt::transparent);
        { QPainter gp(&iconPix); QSvgRenderer(QStringLiteral(":/brand/vertex-icon.svg")).render(&gp); }
        auto *iconLabel = new QLabel(vertexRow);
        iconLabel->setPixmap(iconPix);

        auto *vertexLabel = new QLabel("Vertex Industries", vertexRow);
        {
            QFont f = vertexLabel->font();
            f.setPointSizeF(Theme::fontXl);
            vertexLabel->setFont(f);
        }

        auto *h = new QHBoxLayout(vertexRow);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(Theme::spacingSm);
        h->addStretch();
        h->addWidget(iconLabel);
        h->addWidget(vertexLabel);
        h->addStretch();
    }

    auto *communityLabel = new QLabel("and the community", aboutContent);
    {
        QFont f = communityLabel->font();
        f.setPointSizeF(Theme::fontSm);
        communityLabel->setFont(f);
    }
    communityLabel->setAlignment(Qt::AlignCenter);

    aboutLayout->addWidget(presentedByLabel);
    aboutLayout->addWidget(vertexRow);
    aboutLayout->addWidget(communityLabel);
    aboutLayout->addWidget(makeSep());

    auto *copyrightLabel = new QLabel(
        "© 2026 Vertex Industries. All rights reserved.<br>"
        "Available under <a href=\"https://github.com/vertex-poe1/learn-to-play-poe1/blob/main/LICENSE\""
        " style=\"color: #787060;\">AGPL-3.0</a>"
        " — contact us for <a href=\"https://github.com/vertex-poe1/learn-to-play-poe1/blob/main/LICENSE-ALTERNATE\""
        " style=\"color: #787060;\">alternate licensing</a>.<br>"
        "See <a href=\"https://github.com/vertex-poe1/learn-to-play-poe1/blob/main/NOTICE\""
        " style=\"color: #787060;\">NOTICE</a>"
        " for third-party attributions.",
        aboutContent);
    copyrightLabel->setTextFormat(Qt::RichText);
    copyrightLabel->setOpenExternalLinks(true);
    copyrightLabel->setAlignment(Qt::AlignCenter);
    copyrightLabel->setForegroundRole(QPalette::PlaceholderText);

    aboutLayout->addWidget(copyrightLabel);
    aboutLayout->addWidget(makeSep());

    auto *aboutDebugMode = new QCheckBox("Enable debug mode", aboutContent);
    {
        QFont f = aboutDebugMode->font();
        f.setPointSizeF(Theme::fontSm);
        aboutDebugMode->setFont(f);
    }
    aboutDebugMode->setStyleSheet(
        QStringLiteral("QCheckBox::indicator { width: %1px; height: %1px; }").arg(Theme::checkboxSm));
    aboutDebugMode->setChecked(m_config.debugMode);
    auto *aboutDebugRow = new QHBoxLayout;
    aboutDebugRow->addStretch();
    aboutDebugRow->addWidget(aboutDebugMode);
    aboutDebugRow->addStretch();
    aboutLayout->addLayout(aboutDebugRow);
    connect(aboutDebugMode, &QCheckBox::toggled, this, [this, aboutDebugMode](bool) {
        m_config.debugMode = aboutDebugMode->isChecked();
        saveAndEmit();
    });

    aboutLayout->addStretch(1);

    parentLayout->addWidget(aboutContent);
}

void SettingsPage::buildAlertsPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 6: Alerts -----------------------------------------------
    auto *alertsContent = new QWidget;
    auto *alertsLayout  = new QVBoxLayout(alertsContent);
    alertsLayout->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);
    alertsLayout->setSpacing(Theme::spacingSm);

    alertsLayout->addWidget(new QLabel("When a game event fires, take an action:", alertsContent));

    m_alertsList = new QListWidget(alertsContent);
    alertsLayout->addWidget(m_alertsList, 1);

    auto *alertsBtnAdd    = new QPushButton("Add",    alertsContent);
    auto *alertsBtnEdit   = new QPushButton("Edit",   alertsContent);
    auto *alertsBtnRemove = new QPushButton("Remove", alertsContent);

    auto *alertsBtnRow = new QHBoxLayout;
    alertsBtnRow->addStretch();
    alertsBtnRow->addWidget(alertsBtnAdd);
    alertsBtnRow->addWidget(alertsBtnEdit);
    alertsBtnRow->addWidget(alertsBtnRemove);
    alertsLayout->addLayout(alertsBtnRow);

    connect(alertsBtnAdd,    &QPushButton::clicked, this, &SettingsPage::alertsAddRule);
    connect(alertsBtnEdit,   &QPushButton::clicked, this, &SettingsPage::alertsEditRule);
    connect(alertsBtnRemove, &QPushButton::clicked, this, &SettingsPage::alertsRemoveRule);
    connect(m_alertsList, &QListWidget::itemDoubleClicked, this, &SettingsPage::alertsEditRule);

    parentLayout->addWidget(alertsContent);
}

void SettingsPage::buildDebugPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 7: Debug ------------------------------------------------
    auto *debugContent = new QWidget;
    auto *debugForm    = new QFormLayout(debugContent);
    debugForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_debugLog = new QCheckBox(debugContent);
    m_debugLog->setChecked(m_config.debugLog);
    debugForm->addRow("Debug logging:", m_debugLog);

    m_userAgent = new QComboBox(debugContent);
    m_userAgent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_userAgent->addItem("Auto (Chromium)");
    m_userAgent->insertSeparator(m_userAgent->count());
    m_userAgent->addItem("Windows 11");
    {
        auto *uaModel = qobject_cast<QStandardItemModel *>(m_userAgent->model());
        if (uaModel)
            uaModel->item(m_userAgent->count() - 1)->setEnabled(false);
    }
    for (const auto &entry : kUserAgents)
        m_userAgent->addItem(entry.label);
    m_userAgent->insertSeparator(m_userAgent->count());
    m_userAgent->addItem("Custom");
    {
        int idx = m_userAgent->findText(m_config.debugLegacyUserAgent);
        m_userAgent->setCurrentIndex(idx >= 0 ? idx : 0); // default Auto (Chromium)
    }

    m_customUserAgent = new QLineEdit(debugContent);
    {
        const bool isCustom = m_config.debugLegacyUserAgent == QLatin1String("Custom");
        const bool isAuto   = m_config.debugLegacyUserAgent == QLatin1String("Auto (Chromium)")
                              || m_config.debugLegacyUserAgent.isEmpty();
        m_customUserAgent->setReadOnly(!isCustom);
        if (isAuto) {
            m_customUserAgent->setPlaceholderText("Native Chromium UA");
        } else {
            m_customUserAgent->setPlaceholderText("User-Agent string");
            m_customUserAgent->setText(isCustom ? m_config.debugLegacyUserAgentCustom
                                                : m_config.effectiveUserAgent());
        }
    }

    const int iconPx = QFontMetrics(font()).height();
    auto *copyBtn = new QPushButton(debugContent);
    copyBtn->setFlat(true);
    copyBtn->setFixedSize(iconPx + 8, iconPx + 8);
    copyBtn->setIcon(QIcon(Theme::renderSvgIcon(
        QStringLiteral(":/icons/clipboard-fill.svg"),
        palette().buttonText().color(),
        {iconPx, iconPx}, devicePixelRatioF())));
    copyBtn->setIconSize({iconPx, iconPx});
    copyBtn->setToolTip("Copy to clipboard");
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QGuiApplication::clipboard()->setText(m_customUserAgent->text());
    });

    {
        auto *sectionLabel = new QLabel("Official Legacy POE API", debugContent);
        QFont f = sectionLabel->font();
        f.setBold(true);
        sectionLabel->setFont(f);

        const int px = QFontMetrics(font()).height();
        auto *infoBtn = new QPushButton(debugContent);
        infoBtn->setFlat(true);
        infoBtn->setFixedSize(px + 8, px + 8);
        infoBtn->setIcon(QIcon(Theme::renderSvgIcon(
            QStringLiteral(":/icons/info-circle.svg"),
            palette().buttonText().color(),
            {px, px}, devicePixelRatioF())));
        infoBtn->setIconSize({px, px});
        infoBtn->setToolTip("Encrypted Session Token");
        connect(infoBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(
                QUrl(docSource("", "rationales/legacy-api").url));
        });

        auto *sectionRow = new QHBoxLayout;
        sectionRow->setContentsMargins(0, 0, 0, 0);
        sectionRow->addWidget(sectionLabel);
        sectionRow->addWidget(infoBtn);
        sectionRow->addStretch();
        debugForm->addRow(sectionRow);
    }

    auto *uaComboRow = new QHBoxLayout;
    uaComboRow->setContentsMargins(0, 0, 0, 0);
    uaComboRow->addWidget(m_userAgent);
    uaComboRow->addStretch();
    debugForm->addRow("User agent:", uaComboRow);

    auto *uaFieldRow = new QHBoxLayout;
    uaFieldRow->setContentsMargins(0, 0, 0, 0);
    uaFieldRow->addWidget(m_customUserAgent, 1);
    uaFieldRow->addWidget(copyBtn);
    debugForm->addRow(new QLabel(debugContent), uaFieldRow);

    m_includeToolName = new QCheckBox(debugContent);
    {
        const bool isCustom = m_config.debugLegacyUserAgent == QLatin1String("Custom");
        m_includeToolName->setChecked(!isCustom && m_config.debugLegacyUserAgentApp);
        m_includeToolName->setEnabled(!isCustom);
    }
    debugForm->addRow("Include tool name:", m_includeToolName);

    m_includeQtToken = new QCheckBox(debugContent);
    {
        const bool isCustom = m_config.debugLegacyUserAgent == QLatin1String("Custom");
        m_includeQtToken->setEnabled(!isCustom);
        m_includeQtToken->setChecked(!isCustom && m_config.debugUserAgentQt);
    }
    debugForm->addRow("Include QtWebEngine token:", m_includeQtToken);

    parentLayout->addWidget(debugContent);
    connect(m_debugLog,        &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
    connect(m_userAgent, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool isCustom = m_userAgent->currentText() == QLatin1String("Custom");
        const bool isAuto   = m_userAgent->currentText() == QLatin1String("Auto (Chromium)");
        m_customUserAgent->setReadOnly(!isCustom);
        if (isAuto) {
            refreshAutoUADisplay();
        } else {
            m_customUserAgent->setPlaceholderText("User-Agent string");
            if (isCustom)
                m_customUserAgent->setText(m_config.debugLegacyUserAgentCustom);
        }
        m_includeToolName->setEnabled(!isCustom);
        m_includeToolName->blockSignals(true);
        m_includeToolName->setChecked(!isCustom && m_config.debugLegacyUserAgentApp);
        m_includeToolName->blockSignals(false);
        m_includeQtToken->setEnabled(!isCustom);
        m_includeQtToken->blockSignals(true);
        m_includeQtToken->setChecked(!isCustom && m_config.debugUserAgentQt);
        m_includeQtToken->blockSignals(false);
        saveAndEmit();
    });
    connect(m_customUserAgent, &QLineEdit::textEdited,         this, [this](const QString &) { saveAndEmit(); });
    connect(m_includeToolName, &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
    connect(m_includeQtToken,    &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::buildAccountsPage(QWidget *parent)
{
    auto *parentLayout = new QVBoxLayout(parent);
    parentLayout->setContentsMargins(0, 0, 0, 0);


    // ---- Page 8: Accounts ---------------------------------------------
    auto *accountsContent = new QWidget;
    auto *accountsForm    = new QFormLayout(accountsContent);
    accountsForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_accountsActionBtn = new QPushButton(accountsContent);
    m_accountsActionBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_accountsActionBtn->setText("Checking...");
    m_accountsActionBtn->setEnabled(false);
    connect(m_accountsActionBtn, &QPushButton::clicked, this, [this]() {
        if (m_hasSession) {
            auto *win = new PoeLoginWindow(m_config, this, PoeLoginWindow::Mode::Browse);
            connect(win, &QObject::destroyed, this, [this]() {
                m_hasSession = false;
                m_accountStore->deleteSession();
                updateAccountButton();
            });
            win->show();
        } else {
            auto *win = new PoeLoginWindow(m_config, this);
            connect(win, &PoeLoginWindow::sessionCaptured,
                    m_accountStore, &PoeAccountStore::writeSession);
        }
    });

    {
        auto *sectionLabel = new QLabel("Official Legacy POE API", accountsContent);
        QFont f = sectionLabel->font();
        f.setBold(true);
        sectionLabel->setFont(f);
        accountsForm->addRow(sectionLabel);
    }

    {
        const int px = QFontMetrics(font()).height();
        auto *infoBtn = new QPushButton(accountsContent);
        infoBtn->setFlat(true);
        infoBtn->setFixedSize(px + 8, px + 8);
        infoBtn->setIcon(QIcon(Theme::renderSvgIcon(
            QStringLiteral(":/icons/info-circle.svg"),
            palette().buttonText().color(),
            {px, px}, devicePixelRatioF())));
        infoBtn->setIconSize({px, px});
        infoBtn->setToolTip("Encrypted Session Token");
        connect(infoBtn, &QPushButton::clicked, this, []() {
            QDesktopServices::openUrl(
                QUrl(docSource("", "rationales/legacy-api").url));
        });

        auto *accountsActionRow = new QHBoxLayout;
        accountsActionRow->setContentsMargins(0, 0, 0, 0);
        accountsActionRow->addWidget(m_accountsActionBtn);
        accountsActionRow->addWidget(infoBtn);
        accountsActionRow->addStretch();
        accountsForm->addRow("PathOfExile.com Account:", accountsActionRow);
    }

    m_accountsUaLabel = new QLabel("User agent:", accountsContent);
    m_accountsUaDisplay = new QLineEdit(accountsContent);
    m_accountsUaDisplay->setReadOnly(true);
    m_accountsUaDisplay->setPlaceholderText("Native Chromium UA");
    // Populated by the deferred native UA fetch; empty shows placeholder until then.

    {
        const int px = QFontMetrics(font()).height();
        m_accountsUaCopyBtn = new QPushButton(accountsContent);
        m_accountsUaCopyBtn->setFlat(true);
        m_accountsUaCopyBtn->setFixedSize(px + 8, px + 8);
        m_accountsUaCopyBtn->setIcon(QIcon(Theme::renderSvgIcon(
            QStringLiteral(":/icons/clipboard-fill.svg"),
            palette().buttonText().color(),
            {px, px}, devicePixelRatioF())));
        m_accountsUaCopyBtn->setIconSize({px, px});
        m_accountsUaCopyBtn->setToolTip("Copy to clipboard");
        connect(m_accountsUaCopyBtn, &QPushButton::clicked, this, [this]() {
            QGuiApplication::clipboard()->setText(m_accountsUaDisplay->text());
        });
    }

    auto *accountsUaRow = new QHBoxLayout;
    accountsUaRow->setContentsMargins(0, 0, 0, 0);
    accountsUaRow->addWidget(m_accountsUaDisplay, 1);
    accountsUaRow->addWidget(m_accountsUaCopyBtn);
    accountsForm->addRow(m_accountsUaLabel, accountsUaRow);
    m_accountsUaLabel->setVisible(m_config.debugMode);
    m_accountsUaDisplay->setVisible(m_config.debugMode);
    m_accountsUaCopyBtn->setVisible(m_config.debugMode);

    parentLayout->addWidget(accountsContent);
    connect(m_accountStore, &PoeAccountStore::sessionRead, this,
            [this](const QString &poesessid) {
        m_hasSession = !poesessid.isEmpty();
        updateAccountButton();
    });
    connect(m_accountStore, &PoeAccountStore::sessionWritten, this,
            [this](bool ok) {
        if (ok) { m_hasSession = true; updateAccountButton(); }
    });
    m_accountStore->readSession();
    m_accountsUaLabel->setVisible(m_config.debugMode);
    m_accountsUaDisplay->setVisible(m_config.debugMode);
    m_accountsUaCopyBtn->setVisible(m_config.debugMode);
    const QString displayUa = m_config.debugLegacyUserAgent == QLatin1String("Auto (Chromium)")
                              ? (m_nativeChromiumUA.isEmpty() ? "Auto (Chromium)" : autoChromiumUA())
                              : m_config.effectiveUserAgent();
    m_accountsUaDisplay->setText(displayUa);
}


    void SettingsPage::navigateTo(int pageIndex, const QString &title)
{
    if (pageIndex == 6)
        alertsRebuildList();
    m_titleLabel->setText(title);
    m_backBtn->setVisible(true);
    m_stack->setCurrentIndex(pageIndex);
}


void SettingsPage::loadPageAsync(int pageIndex, const QString &title)
{
    if (m_stack->currentIndex() == 9 && m_targetPageIndex != pageIndex) {
        // We are currently loading a DIFFERENT page. Deprioritize it.
        DeferredTaskQueue::instance().setPriority(QString("settings_page_%1").arg(m_targetPageIndex), DeferredTaskQueue::Low);
    }

    m_targetPageIndex = pageIndex;
    m_titleLabel->setText(title);
    m_backBtn->setVisible(true);

    if (m_pageLoaded[pageIndex]) {
        if (pageIndex == 6)
            alertsRebuildList();
        m_stack->setCurrentIndex(pageIndex);
        return;
    }

    m_stack->setCurrentIndex(9); // Loading page

    qApp->processEvents(QEventLoop::ExcludeUserInputEvents); // Force UI update before blocking

    // Promote the pre-queued task to Immediate priority so it runs right away and blocks UI
    // with the WaitCursor automatically managed by DeferredTaskQueue.
    DeferredTaskQueue::instance().setPriority(QString("settings_page_%1").arg(pageIndex), DeferredTaskQueue::Immediate);
}

void SettingsPage::navigateBack()
{
    if (m_stack->currentIndex() == 9) {
        // We are going back while loading. Deprioritize the current loading task.
        DeferredTaskQueue::instance().setPriority(QString("settings_page_%1").arg(m_targetPageIndex), DeferredTaskQueue::Low);
    }
    m_targetPageIndex = 0;
    m_titleLabel->setText("");
    m_backBtn->setVisible(false);
    m_stack->setCurrentIndex(0);
}

void SettingsPage::hideEvent(QHideEvent *event)
{
    if (m_stack->currentIndex() == 9) {
        DeferredTaskQueue::instance().setPriority(QString("settings_page_%1").arg(m_targetPageIndex), DeferredTaskQueue::Low);
    }
    QWidget::hideEvent(event);
}

void SettingsPage::alertsRebuildList()
{
    m_alertsList->clear();
    for (const auto &rule : m_config.liveAlertRules) {
        auto *item = new QListWidgetItem(ruleDescription(rule), m_alertsList);
        item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
}

void SettingsPage::alertsAddRule()
{
    LiveEventRule rule;
    rule.id      = QString::number(QDateTime::currentMSecsSinceEpoch());
    rule.enabled = true;
    const auto &ep     = eventPresets().first();
    rule.label         = ep.label;
    rule.eventType     = ep.eventType;
    rule.dataFilter    = ep.dataFilter;
    rule.actionType    = actionPresets().first().actionType;
    rule.actionParams["message"] = ep.hint;

    if (alertsEditRuleDialog(rule)) {
        m_config.liveAlertRules.append(rule);
        alertsRebuildList();
        m_alertsList->setCurrentRow(m_config.liveAlertRules.size() - 1);
        m_config.save();
        emit configChanged();
    }
}

void SettingsPage::alertsEditRule()
{
    const int row = m_alertsList->currentRow();
    if (row < 0 || row >= m_config.liveAlertRules.size()) return;

    LiveEventRule rule = m_config.liveAlertRules[row];
    if (alertsEditRuleDialog(rule)) {
        rule.enabled = m_alertsList->item(row)->checkState() == Qt::Checked;
        m_config.liveAlertRules[row] = rule;
        alertsRebuildList();
        m_alertsList->setCurrentRow(row);
        m_config.save();
        emit configChanged();
    }
}

void SettingsPage::alertsRemoveRule()
{
    const int row = m_alertsList->currentRow();
    if (row < 0 || row >= m_config.liveAlertRules.size()) return;
    m_config.liveAlertRules.removeAt(row);
    alertsRebuildList();
    m_config.save();
    emit configChanged();
}

bool SettingsPage::alertsEditRuleDialog(LiveEventRule &rule)
{
    QDialog dlg(this);
    dlg.setWindowTitle("Edit Alert Rule");
    dlg.setMinimumWidth(440);

    auto *eventCombo  = new QComboBox(&dlg);
    auto *actionCombo = new QComboBox(&dlg);
    auto *titleEdit   = new QLineEdit(rule.actionParams.value("title").toString(), &dlg);
    auto *msgEdit     = new QLineEdit(rule.actionParams.value("message").toString(), &dlg);
    auto *hintLabel   = new QLabel(&dlg);
    hintLabel->setWordWrap(true);

    const auto &ePresets = eventPresets();
    for (const auto &p : ePresets)
        eventCombo->addItem(p.label);
    eventCombo->setCurrentIndex(findEventPresetIndex(rule));

    for (const auto &p : actionPresets())
        actionCombo->addItem(p.label);
    actionCombo->setCurrentIndex(findActionPresetIndex(rule));

    const auto updateHint = [&](int idx) {
        if (idx < 0 || idx >= ePresets.size()) return;
        const QString &hint = ePresets[idx].hint;
        hintLabel->setText(hint.isEmpty() ? QString()
                                          : QStringLiteral("Available: %1").arg(hint));
    };
    updateHint(eventCombo->currentIndex());
    connect(eventCombo, &QComboBox::currentIndexChanged, &dlg, [&](int idx) { updateHint(idx); });

    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto *form = new QFormLayout;
    form->addRow("When:",    eventCombo);
    form->addRow("Do:",      actionCombo);
    form->addRow("Title:",   titleEdit);
    form->addRow("Message:", msgEdit);
    form->addRow(hintLabel);

    auto *vbox = new QVBoxLayout(&dlg);
    vbox->addLayout(form);
    vbox->addWidget(bbox);

    if (dlg.exec() != QDialog::Accepted)
        return false;

    const int ei = eventCombo->currentIndex();
    const int ai = actionCombo->currentIndex();
    if (ei >= 0 && ei < ePresets.size()) {
        rule.label      = ePresets[ei].label;
        rule.eventType  = ePresets[ei].eventType;
        rule.dataFilter = ePresets[ei].dataFilter;
    }
    if (ai >= 0 && ai < actionPresets().size())
        rule.actionType = actionPresets()[ai].actionType;

    rule.actionParams["title"]   = titleEdit->text();
    rule.actionParams["message"] = msgEdit->text();
    return true;
}

QString SettingsPage::autoChromiumUA() const
{
    if (m_nativeChromiumUA.isEmpty()) {
        const_cast<SettingsPage*>(this)->m_nativeChromiumUA = QWebEngineProfile::defaultProfile()->httpUserAgent();
    }
    
    if (m_nativeChromiumUA.isEmpty())
        return {};
    static const QRegularExpression kQtToken(QStringLiteral(R"(QtWebEngine/[\d.]+ )"));
    QString ua = m_nativeChromiumUA;
    if (!m_config.debugUserAgentQt)
        ua.remove(kQtToken);
    ua = ua.trimmed();
    if (m_config.debugLegacyUserAgentApp) {
        const QString token = QCoreApplication::applicationName().remove(u' ')
                              + u'/'
                              + QCoreApplication::applicationVersion();
        ua += u' ' + token;
    }
    return ua;
}

void SettingsPage::refreshAutoUADisplay()
{
    const QString ua = autoChromiumUA();

    if (m_userAgent->currentText() == QLatin1String("Auto (Chromium)")) {
        if (ua.isEmpty()) {
            m_customUserAgent->setPlaceholderText("Native Chromium UA");
            m_customUserAgent->clear();
        } else {
            m_customUserAgent->setPlaceholderText({});
            m_customUserAgent->setText(ua);
        }
    }

    // Accounts page always shows the resolved auto UA (read-only, debug-only).
    m_accountsUaDisplay->setText(ua);
}

void SettingsPage::updateAccountButton()
{
    m_accountsActionBtn->setText(m_hasSession ? "Logout" : "Login");
    m_accountsActionBtn->setEnabled(true);
}

void SettingsPage::saveAndEmit()
{
    if (m_autoDetect) m_config.autoDetectInstallDir = m_autoDetect->isChecked();
    if (m_installDirs) m_config.installDirs = m_installDirs->items();

    if (m_exeNames) {
        const QStringList known = AppConfig::knownExes();
        QStringList userExes;
        for (const QString &name : m_exeNames->items()) {
            if (!known.contains(name, Qt::CaseInsensitive))
                userExes << name;
        }
        m_config.executableNames = userExes;
    }
    
    if (m_enableOverlay) m_config.useGameOverlay  = m_enableOverlay->isChecked();
    if (m_overlayColumns) m_config.overlayColumns = m_overlayColumns->currentIndex();
    if (m_overlayRows) m_config.overlayRows       = m_overlayRows->currentIndex();
    if (m_overlayHideout) m_config.overlayShowHideout    = m_overlayHideout->isChecked();
    if (m_overlayGuild) m_config.overlayShowGuild      = m_overlayGuild->isChecked();
    if (m_overlayMenagerie) m_config.overlayShowMenagerie  = m_overlayMenagerie->isChecked();
    if (m_overlayMonastery) m_config.overlayShowMonastery  = m_overlayMonastery->isChecked();
    if (m_overlayHeist) m_config.overlayShowHeist      = m_overlayHeist->isChecked();
    if (m_overlaySanctum) m_config.overlayShowSanctum    = m_overlaySanctum->isChecked();
    if (m_overlayLadder) m_config.overlayShowLadder     = m_overlayLadder->isChecked();
    if (m_overlayDelve) m_config.overlayShowDelve      = m_overlayDelve->isChecked();
    if (m_overlayKingsmarch) m_config.overlayShowKingsmarch = m_overlayKingsmarch->isChecked();
    if (m_overlayTimePlayed) m_config.overlayShowTimePlayed = m_overlayTimePlayed->isChecked();
    if (m_overlayCharacterAge) m_config.overlayShowCharacterAge = m_overlayCharacterAge->isChecked();
    if (m_overlayPassives) m_config.overlayShowPassives = m_overlayPassives->isChecked();
    if (m_overlayDeaths) m_config.overlayShowDeaths = m_overlayDeaths->isChecked();
    if (m_overlayMonstersRemaining) m_config.overlayShowMonstersRemaining = m_overlayMonstersRemaining->isChecked();
    if (m_overlayAtlasPassives) m_config.overlayShowAtlasPassives = m_overlayAtlasPassives->isChecked();
    if (m_overlayKills) m_config.overlayShowKills = m_overlayKills->isChecked();
    if (m_overlayResetXP) m_config.overlayShowResetXP = m_overlayResetXP->isChecked();
    if (m_overlayReloadItemFilter) m_config.overlayShowReloadItemFilter = m_overlayReloadItemFilter->isChecked();
    if (m_overlayL2P) m_config.overlayShowL2P        = m_overlayL2P->isChecked();
    
    if (m_defaultTab) m_config.defaultTab      = m_defaultTab->currentIndex();
    if (m_startMinimized) m_config.startMinimized  = m_startMinimized->isChecked();
    if (m_minimizeToTray) m_config.minimizeToTray  = m_minimizeToTray->isChecked();
    
    if (m_showGuildTags) m_config.showGuildTags   = m_showGuildTags->isChecked();
    
    if (m_debugLog) m_config.debugLog       = m_debugLog->isChecked();
    if (m_userAgent) {
        m_config.debugLegacyUserAgent = m_userAgent->currentText();
        if (m_userAgent->currentText() == QLatin1String("Custom") && m_customUserAgent)
            m_config.debugLegacyUserAgentCustom = m_customUserAgent->text();
        if (m_userAgent->currentText() != QLatin1String("Custom")) {
            if (m_includeToolName) m_config.debugLegacyUserAgentApp = m_includeToolName->isChecked();
            if (m_includeQtToken) m_config.debugUserAgentQt = m_includeQtToken->isChecked();
        }
    }
    
    m_config.save();

    if (m_userAgent) {
        if (m_userAgent->currentText() == QLatin1String("Auto (Chromium)"))
            refreshAutoUADisplay();
        else if (m_userAgent->currentText() != QLatin1String("Custom") && m_customUserAgent)
            m_customUserAgent->setText(m_config.effectiveUserAgent());
    }
    emit configChanged();
}
