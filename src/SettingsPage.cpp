#include "SettingsPage.h"
#include "AppConfig.h"
#include "Theme.h"
#include "ListEditor.h"
#include "PoeAccountStore.h"
#include "PoeLoginWindow.h"

#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
#include <QMessageBox>
#include <QComboBox>
#include <QCoreApplication>
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
            this, [this] { navigateTo(6, "Alerts"); });

    addDivider();

    connect(makeItemBtn("Accounts"), &QPushButton::clicked,
            this, [this] { navigateTo(8, "Accounts"); });
    connect(makeItemBtn("Game"),    &QPushButton::clicked,
            this, [this] { navigateTo(1, "Game"); });
    connect(makeItemBtn("Overlay"), &QPushButton::clicked,
            this, [this] { navigateTo(2, "Overlay"); });
    connect(makeItemBtn("Window"),  &QPushButton::clicked,
            this, [this] { navigateTo(3, "Window"); });
    connect(makeItemBtn("Chat"),    &QPushButton::clicked,
            this, [this] { navigateTo(4, "Chat"); });

    addDivider();

    connect(makeItemBtn("Debug"), &QPushButton::clicked,
            this, [this] { navigateTo(7, "Debug"); });

    connect(makeItemBtn("About"),  &QPushButton::clicked,
            this, [this] { navigateTo(5, "About"); });

    connect(makeItemBtn("Exit App", false), &QPushButton::clicked, this, [this]() {
        const auto reply = QMessageBox::question(this, "Exit", "Really exit?",
                                                 QMessageBox::Yes | QMessageBox::Cancel,
                                                 QMessageBox::Yes);
        if (reply == QMessageBox::Yes)
            qApp->quit();
    });

    categoryLayout->addStretch(1);

    // ---- Page 1: Game -------------------------------------------------
    auto *gameScroll = new QScrollArea;
    gameScroll->setWidgetResizable(true);
    gameScroll->setFrameShape(QFrame::NoFrame);

    auto *gameContent = new QWidget;
    auto *gameForm    = new QFormLayout(gameContent);
    gameForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_autoDetect = new QCheckBox(gameContent);
    m_autoDetect->setChecked(config.autoDetectInstallDir);
    gameForm->addRow("Auto-detect install directories:", m_autoDetect);

    m_installDirs = new ListEditor({}, gameContent);
    m_installDirs->setBrowseForDirectory(true);
    m_installDirs->setItems(config.installDirs);
    gameForm->addRow("Install directories:", m_installDirs);

    m_exeNames = new ListEditor("Executable name (e.g. PathOfExile_x64Steam.exe)", gameContent);
    m_exeNames->setBuiltinItems(AppConfig::knownExes());
    m_exeNames->setItems(config.executableNames);
    m_exeNames->setInputFileBrowser(true);
    gameForm->addRow("Executable names:", m_exeNames);

    gameScroll->setWidget(gameContent);
    m_stack->addWidget(gameScroll); // index 1

    // ---- Page 2: Overlay -----------------------------------------------
    auto *overlayContent = new QWidget;
    auto *overlayForm    = new QFormLayout(overlayContent);
    overlayForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_enableOverlay = new QCheckBox(overlayContent);
    m_enableOverlay->setChecked(config.useGameOverlay);
    overlayForm->addRow("Enable overlay:", m_enableOverlay);

    m_stack->addWidget(overlayContent); // index 2

    // ---- Page 3: Window -----------------------------------------------
    auto *windowContent = new QWidget;
    auto *windowForm    = new QFormLayout(windowContent);
    windowForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_defaultTab = new QComboBox(windowContent);
    m_defaultTab->addItems({"Guide", "Chat", "DMs", "Stash", "Profile", "Current Log", "Past Logs"});
    m_defaultTab->setCurrentIndex(qBound(0, config.defaultTab, 6));
    m_defaultTab->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    windowForm->addRow("Default tab:", m_defaultTab);

    m_startMinimized = new QCheckBox(windowContent);
    m_startMinimized->setChecked(config.startMinimized);
    windowForm->addRow("Start minimized:", m_startMinimized);

    m_minimizeToTray = new QCheckBox(windowContent);
    m_minimizeToTray->setChecked(config.minimizeToTray);
    windowForm->addRow("Minimize to tray on close:", m_minimizeToTray);

    m_stack->addWidget(windowContent); // index 3

    // ---- Page 4: Chat -------------------------------------------------
    auto *chatContent = new QWidget;
    auto *chatForm    = new QFormLayout(chatContent);
    chatForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_showGuildTags = new QCheckBox(chatContent);
    m_showGuildTags->setChecked(config.showGuildTags);
    chatForm->addRow("Display guild tags:", m_showGuildTags);

    m_stack->addWidget(chatContent); // index 4

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
        " style=\"color: #787060;\">other licensing</a>.<br>"
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
    aboutDebugMode->setChecked(config.debugLog);
    auto *aboutDebugRow = new QHBoxLayout;
    aboutDebugRow->addStretch();
    aboutDebugRow->addWidget(aboutDebugMode);
    aboutDebugRow->addStretch();
    aboutLayout->addLayout(aboutDebugRow);
    connect(aboutDebugMode, &QCheckBox::toggled, this, [this, aboutDebugMode](bool) {
        m_debugMode->setChecked(aboutDebugMode->isChecked());
    });
    connect(m_debugMode, &QCheckBox::toggled, this, [aboutDebugMode](bool checked) {
        if (aboutDebugMode->isChecked() != checked)
            aboutDebugMode->setChecked(checked);
    });

    aboutLayout->addStretch(1);

    m_stack->addWidget(aboutContent); // index 5

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

    m_stack->addWidget(alertsContent); // index 6

    // ---- Page 7: Debug ------------------------------------------------
    auto *debugContent = new QWidget;
    auto *debugForm    = new QFormLayout(debugContent);
    debugForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    m_debugMode = new QCheckBox(debugContent);
    m_debugMode->setChecked(config.debugLog);
    debugForm->addRow("Debug logging:", m_debugMode);

    m_userAgent = new QComboBox(debugContent);
    m_userAgent->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_userAgent->addItem("Windows 11");
    {
        auto *uaModel = qobject_cast<QStandardItemModel *>(m_userAgent->model());
        if (uaModel)
            uaModel->item(0)->setEnabled(false);
    }
    for (const auto &entry : kUserAgents)
        m_userAgent->addItem(entry.label);
    m_userAgent->insertSeparator(m_userAgent->count());
    m_userAgent->addItem("Custom");
    {
        int idx = m_userAgent->findText(config.userAgentLabel);
        m_userAgent->setCurrentIndex(idx > 0 ? idx : 2); // default Chrome (win11)
    }

    m_customUserAgent = new QLineEdit(debugContent);
    m_customUserAgent->setPlaceholderText("User-Agent string");
    {
        const bool isCustom = config.userAgentLabel == QLatin1String("Custom");
        m_customUserAgent->setReadOnly(!isCustom);
        m_customUserAgent->setText(isCustom ? config.customUserAgent : config.effectiveUserAgent());
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
        const bool isCustom = config.userAgentLabel == QLatin1String("Custom");
        m_includeToolName->setChecked(!isCustom && config.includeToolName);
        m_includeToolName->setEnabled(!isCustom);
    }
    debugForm->addRow("Include tool name:", m_includeToolName);

    m_stack->addWidget(debugContent); // index 7

    // ---- Page 8: Accounts ---------------------------------------------
    auto *accountsContent = new QWidget;
    auto *accountsForm    = new QFormLayout(accountsContent);
    accountsForm->setContentsMargins(Theme::spacingBase, Theme::spacingBase, Theme::spacingBase, Theme::spacingBase);

    auto *loginBtn = new QPushButton("Login", accountsContent);
    loginBtn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(loginBtn, &QPushButton::clicked, this, [this]() {
        auto *win = new PoeLoginWindow(m_config, this);
        win->setAttribute(Qt::WA_DeleteOnClose);
        connect(win, &PoeLoginWindow::sessionCaptured,
                m_accountStore, &PoeAccountStore::writeSession);
    });

    accountsForm->addRow("PathOfExile.com Account:", loginBtn);
    m_stack->addWidget(accountsContent); // index 8

    // ---- Signal connections -------------------------------------------
    connect(m_autoDetect,     &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs,    &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
    connect(m_exeNames,       &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
    connect(m_enableOverlay,  &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_defaultTab,     &QComboBox::currentIndexChanged, this, [this](int) { saveAndEmit(); });
    connect(m_startMinimized, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_showGuildTags,   &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
    connect(m_debugMode,       &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
    connect(m_userAgent, &QComboBox::currentIndexChanged, this, [this](int) {
        const bool isCustom = m_userAgent->currentText() == QLatin1String("Custom");
        m_customUserAgent->setReadOnly(!isCustom);
        if (isCustom)
            m_customUserAgent->setText(m_config.customUserAgent);
        // Non-custom: let saveAndEmit set the text after updating the config label.
        m_includeToolName->setEnabled(!isCustom);
        m_includeToolName->blockSignals(true);
        m_includeToolName->setChecked(!isCustom && m_config.includeToolName);
        m_includeToolName->blockSignals(false);
        saveAndEmit();
    });
    connect(m_customUserAgent, &QLineEdit::textEdited,         this, [this](const QString &) { saveAndEmit(); });
    connect(m_includeToolName, &QCheckBox::toggled,            this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::navigateTo(int pageIndex, const QString &title)
{
    if (pageIndex == 6)
        alertsRebuildList();
    m_titleLabel->setText(title);
    m_backBtn->setVisible(true);
    m_stack->setCurrentIndex(pageIndex);
}

void SettingsPage::navigateBack()
{
    m_titleLabel->setText("");
    m_backBtn->setVisible(false);
    m_stack->setCurrentIndex(0);
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

void SettingsPage::saveAndEmit()
{
    m_config.autoDetectInstallDir = m_autoDetect->isChecked();
    m_config.installDirs          = m_installDirs->items();

    const QStringList known = AppConfig::knownExes();
    QStringList userExes;
    for (const QString &name : m_exeNames->items()) {
        if (!known.contains(name, Qt::CaseInsensitive))
            userExes << name;
    }
    m_config.executableNames = userExes;
    m_config.useGameOverlay  = m_enableOverlay->isChecked();
    m_config.defaultTab      = m_defaultTab->currentIndex();
    m_config.startMinimized  = m_startMinimized->isChecked();
    m_config.minimizeToTray  = m_minimizeToTray->isChecked();
    m_config.showGuildTags   = m_showGuildTags->isChecked();
    m_config.debugLog        = m_debugMode->isChecked();
    if (m_userAgent->currentIndex() > 0)
        m_config.userAgentLabel = m_userAgent->currentText();
    if (m_userAgent->currentText() == QLatin1String("Custom"))
        m_config.customUserAgent = m_customUserAgent->text();
    if (m_userAgent->currentText() != QLatin1String("Custom"))
        m_config.includeToolName = m_includeToolName->isChecked();
    m_config.save();
    if (m_userAgent->currentText() != QLatin1String("Custom"))
        m_customUserAgent->setText(m_config.effectiveUserAgent());
    emit configChanged();
}
