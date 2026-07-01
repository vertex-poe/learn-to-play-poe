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


    connect(this, &SettingsPage::configChanged, this, [this]() {
        qDebug() << "configChanged lambda start";
        const bool debug = m_config.debugMode;
        m_debugCategoryBtn->setVisible(debug);
        if (m_accountsUaLabel) m_accountsUaLabel->setVisible(debug);
        if (m_accountsUaDisplay) {
            m_accountsUaDisplay->setVisible(debug);
            const QString displayUa = m_config.debugLegacyUserAgent == QLatin1String("Auto (Chromium)")
                                      ? (m_nativeChromiumUA.isEmpty() ? QStringLiteral("Auto (Chromium)") : autoChromiumUA())
                                      : m_config.effectiveUserAgent();
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

    auto *checkboxesWidget = new QWidget(overlayContent);
    auto *checkboxesLayout = new QHBoxLayout(checkboxesWidget);
    checkboxesLayout->setContentsMargins(0, 10, 0, 0);

    auto *teleportWidget = new QWidget(checkboxesWidget);
    auto *teleportForm = new QFormLayout(teleportWidget);
    teleportForm->setContentsMargins(0, 0, 0, 0);

    auto *infoWidget = new QWidget(checkboxesWidget);
    auto *infoForm = new QFormLayout(infoWidget);
    infoForm->setContentsMargins(0, 0, 0, 0);

    checkboxesLayout->addWidget(teleportWidget);
    checkboxesLayout->addSpacing(40);
    checkboxesLayout->addWidget(infoWidget);
    checkboxesLayout->addStretch();

    overlayForm->addRow(checkboxesWidget);

    // Helper: builds an icon+label widget paired with a checkbox and connects it.
    const auto addIconRow = [&](QFormLayout *form, QCheckBox *&member,
                                const char *icon, const QString &label,
                                const QString &tooltip, bool checked) {
        member = new QCheckBox(overlayContent);
        member->setChecked(checked);
        member->setToolTip(tooltip);
        auto *w = new QWidget(overlayContent);
        w->setToolTip(tooltip);
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(4);
        auto *iconLbl = new QLabel(w);
        iconLbl->setPixmap(QIcon(QLatin1String(icon)).pixmap(18, 18));
        l->addWidget(iconLbl);
        l->addWidget(new QLabel(label + u':', w));
        l->addStretch();
        form->addRow(w, member);
        connect(member, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    };

    teleportForm->addRow(new QLabel("<b>Teleport Shortcuts</b>", overlayContent));

    m_overlayL2P = new QCheckBox(overlayContent);
    m_overlayL2P->setChecked(m_config.overlayShowL2P);
    teleportForm->addRow(
        new QLabel("<span style=\"color: #c8a84b; font-family: 'Palatino Linotype', 'Book Antiqua', 'Palatino', serif; font-size: 14px; font-weight: bold; font-style: italic; letter-spacing: 2px;\">l2p</span> App Focus:", overlayContent),
        m_overlayL2P);

    addIconRow(teleportForm, m_overlayHideout,    ":/icons/fleur-de-lis.svg",        "Hideout",    "/hideout",    m_config.overlayShowHideout);
    addIconRow(teleportForm, m_overlayGuild,      ":/icons/fleur-de-lis-shield.svg", "Guild",      "/guild",      m_config.overlayShowGuild);
    addIconRow(teleportForm, m_overlayMenagerie,  ":/icons/cattle-skull.svg",        "Menagerie",  "/menagerie",  m_config.overlayShowMenagerie);
    addIconRow(teleportForm, m_overlayMonastery,  ":/icons/branch.svg",              "Monastery",  "/monastery",  m_config.overlayShowMonastery);
    addIconRow(teleportForm, m_overlayHeist,      ":/icons/safe2-fill.svg",          "Heist",      "/heist",      m_config.overlayShowHeist);
    addIconRow(teleportForm, m_overlaySanctum,    ":/icons/door-open-fill.svg",      "Sanctum",    "/sanctum",    m_config.overlayShowSanctum);
    addIconRow(teleportForm, m_overlayDelve,      ":/icons/minecart-loaded.svg",     "Delve",      "/delve",      m_config.overlayShowDelve);
    addIconRow(teleportForm, m_overlayKingsmarch, ":/icons/shop.svg",                "Kingsmarch", "/kingsmarch", m_config.overlayShowKingsmarch);

    infoForm->addRow(new QLabel("<b>Informational</b>", overlayContent));

    addIconRow(infoForm, m_overlayLadder,           ":/icons/trophy-fill.svg",        "Top 10 Ladder",      "/ladder",           m_config.overlayShowLadder);
    addIconRow(infoForm, m_overlayTimePlayed,        ":/icons/stopwatch-fill.svg",     "Time Played",        "/played",           m_config.overlayShowTimePlayed);
    addIconRow(infoForm, m_overlayCharacterAge,      ":/icons/stopwatch-fill.svg",     "Character Age",      "/age",              m_config.overlayShowCharacterAge);
    addIconRow(infoForm, m_overlayPassives,          ":/icons/tree-fill.svg",          "Passives",           "/passives",         m_config.overlayShowPassives);
    addIconRow(infoForm, m_overlayDeaths,            ":/icons/person-fill.svg",        "Deaths",             "/deaths",           m_config.overlayShowDeaths);
    addIconRow(infoForm, m_overlayMonstersRemaining, ":/icons/bug-fill.svg",           "Monsters Remaining", "/remaining",        m_config.overlayShowMonstersRemaining);
    addIconRow(infoForm, m_overlayAtlasPassives,     ":/icons/map-fill.svg",           "Atlas Passives",     "/atlaspassives",    m_config.overlayShowAtlasPassives);
    addIconRow(infoForm, m_overlayKills,             ":/icons/bullseye.svg",           "Kills",              "/kills",            m_config.overlayShowKills);
    addIconRow(infoForm, m_overlayResetXP,           ":/icons/box-arrow-in-right.svg", "Reset XP",           "/reset_xp",         m_config.overlayShowResetXP);
    addIconRow(infoForm, m_overlayReloadItemFilter,  ":/icons/indent.svg",             "Reload Item Filter", "/reloaditemfilter", m_config.overlayShowReloadItemFilter);

    parentLayout->addWidget(overlayContent);
    connect(m_enableOverlay,  &QCheckBox::toggled,             this, [this](bool) { saveAndEmit(); });
    connect(m_overlayColumns, &QComboBox::currentIndexChanged, this, [this](int)  { saveAndEmit(); });
    connect(m_overlayRows,    &QComboBox::currentIndexChanged, this, [this](int)  { saveAndEmit(); });
    connect(m_overlayL2P,     &QCheckBox::toggled,             this, [this](bool) { saveAndEmit(); });
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
        "Available under <a href=\"https://github.com/vertex-poe/learn-to-play-poe/blob/main/LICENSE\""
        " style=\"color: #787060;\">AGPL-3.0</a>"
        " — contact us for <a href=\"https://github.com/vertex-poe/learn-to-play-poe/blob/main/LICENSE-ALTERNATE\""
        " style=\"color: #787060;\">alternate licensing</a>.<br>"
        "See <a href=\"https://github.com/vertex-poe/learn-to-play-poe/blob/main/NOTICE\""
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
    if (m_config.debugLegacyUserAgent != QLatin1String("Auto (Chromium)")) {
        m_accountsUaDisplay->setText(m_config.effectiveUserAgent());
    } else {
        // Use the cached native UA if already known; otherwise show placeholder.
        // Do NOT call QWebEngineProfile::defaultProfile() here — on first call it
        // blocks the main thread while the WebEngine renderer process starts, which
        // delays the repaint and makes the Accounts page appear frozen.
        // The field is updated by refreshAutoUADisplay() (called from Debug page
        // interactions) or by the configChanged handler once WebEngine is ready.
        m_accountsUaDisplay->setText(m_nativeChromiumUA.isEmpty()
                                      ? QStringLiteral("Auto (Chromium)")
                                      : autoChromiumUA());
    }
}


void SettingsPage::preloadSubPages(QObject* requestor)
{
    static const struct { int index; } kSubPages[] = {{1},{2},{3},{4},{5},{6},{7},{8}};
    for (const auto &p : kSubPages) {
        if (m_pageLoaded[p.index]) continue;
        const int idx = p.index;
        DeferredTaskQueue::instance().enqueue(
            QString("settings_page_%1").arg(idx),
            DeferredTaskQueue::Low,
            [this, idx]() {
                if (m_pageLoaded[idx]) return;
                QWidget *w = m_stack->widget(idx);
                switch (idx) {
                    case 1: buildGamePage(w);     break;
                    case 2: buildOverlayPage(w);  break;
                    case 3: buildWindowPage(w);   break;
                    case 4: buildChatPage(w);     break;
                    case 5: buildAboutPage(w);    break;
                    case 6: buildAlertsPage(w);   break;
                    case 7: buildDebugPage(w);    break;
                    case 8: buildAccountsPage(w); break;
                    default: break;
                }
                m_pageLoaded[idx] = true;
            },
            requestor);
    }
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

    // Defer enqueueing to the next event-loop tick so the "Loading" page's paint event
    // is processed naturally before the builder runs, without pumping a nested event
    // loop (which would risk re-entrancy into loadPageAsync). The m_targetPageIndex
    // guard skips the enqueue if the user navigated away before this tick fires.
    QTimer::singleShot(0, this, [this, pageIndex] {
        if (m_targetPageIndex != pageIndex)
            return;
        DeferredTaskQueue::instance().enqueue(
            QString("settings_page_%1").arg(pageIndex),
            DeferredTaskQueue::Immediate,
            [this, pageIndex]() {
                if (!m_pageLoaded[pageIndex]) {
                    QWidget *pageWidget = m_stack->widget(pageIndex);
                    switch (pageIndex) {
                        case 1: buildGamePage(pageWidget);     break;
                        case 2: buildOverlayPage(pageWidget);  break;
                        case 3: buildWindowPage(pageWidget);   break;
                        case 4: buildChatPage(pageWidget);     break;
                        case 5: buildAboutPage(pageWidget);    break;
                        case 6: buildAlertsPage(pageWidget);   break;
                        case 7: buildDebugPage(pageWidget);    break;
                        case 8: buildAccountsPage(pageWidget); break;
                        default: break;
                    }
                    m_pageLoaded[pageIndex] = true;
                }
                if (pageIndex == 6)
                    alertsRebuildList();
                if (m_targetPageIndex == pageIndex) {
                    m_stack->setCurrentIndex(pageIndex);
                    m_targetPageIndex = 0;
                }
            });
    });
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
