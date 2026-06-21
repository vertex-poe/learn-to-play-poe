#include "SettingsPage.h"
#include "AppConfig.h"
#include "Theme.h"
#include "ListEditor.h"

#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
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

} // namespace

SettingsPage::SettingsPage(AppConfig &config, QWidget *parent)
    : QWidget(parent)
    , m_config(config)
{
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
    headerBox->setContentsMargins(4, 4, 4, 4);
    headerBox->setSpacing(8);
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
    categoryLayout->setContentsMargins(24, 20, 24, 20);
    categoryLayout->setSpacing(12);

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
        l->setContentsMargins(8, 4, 8, 4);
        auto *line = new QFrame(w);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        l->addWidget(line);
        categoryLayout->addWidget(w);
    };

    m_stack->addWidget(categoryPage); // index 0

    connect(makeItemBtn("Exit App", false), &QPushButton::clicked,
            qApp, &QCoreApplication::quit);
    connect(makeItemBtn("Alerts"), &QPushButton::clicked,
            this, [this] { navigateTo(5, "Alerts"); });

    addDivider();

    connect(makeItemBtn("Game"),   &QPushButton::clicked,
            this, [this] { navigateTo(1, "Game"); });
    connect(makeItemBtn("Window"), &QPushButton::clicked,
            this, [this] { navigateTo(2, "Window"); });
    connect(makeItemBtn("Chat"),   &QPushButton::clicked,
            this, [this] { navigateTo(3, "Chat"); });

    addDivider();

    connect(makeItemBtn("About"),  &QPushButton::clicked,
            this, [this] { navigateTo(4, "About"); });

    categoryLayout->addStretch(1);

    // ---- Page 1: Game -------------------------------------------------
    auto *gameScroll = new QScrollArea;
    gameScroll->setWidgetResizable(true);
    gameScroll->setFrameShape(QFrame::NoFrame);

    auto *gameContent = new QWidget;
    auto *gameForm    = new QFormLayout(gameContent);
    gameForm->setContentsMargins(12, 12, 12, 12);

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

    m_enableOverlay = new QCheckBox(gameContent);
    m_enableOverlay->setChecked(config.useGameOverlay);
    gameForm->addRow("Enable overlay:", m_enableOverlay);

    gameScroll->setWidget(gameContent);
    m_stack->addWidget(gameScroll); // index 1

    // ---- Page 2: Window -----------------------------------------------
    auto *windowContent = new QWidget;
    auto *windowForm    = new QFormLayout(windowContent);
    windowForm->setContentsMargins(12, 12, 12, 12);

    m_startMinimized = new QCheckBox(windowContent);
    m_startMinimized->setChecked(config.startMinimized);
    windowForm->addRow("Start minimized:", m_startMinimized);

    m_minimizeToTray = new QCheckBox(windowContent);
    m_minimizeToTray->setChecked(config.minimizeToTray);
    windowForm->addRow("Minimize to tray on close:", m_minimizeToTray);

    m_stack->addWidget(windowContent); // index 2

    // ---- Page 3: Chat -------------------------------------------------
    auto *chatContent = new QWidget;
    auto *chatForm    = new QFormLayout(chatContent);
    chatForm->setContentsMargins(12, 12, 12, 12);

    m_showGuildTags = new QCheckBox(chatContent);
    m_showGuildTags->setChecked(config.showGuildTags);
    chatForm->addRow("Display guild tags:", m_showGuildTags);

    m_stack->addWidget(chatContent); // index 3

    // ---- Page 4: About ------------------------------------------------
    auto *aboutContent = new QWidget;
    auto *aboutLayout  = new QVBoxLayout(aboutContent);
    aboutLayout->setContentsMargins(12, 16, 12, 12);
    aboutLayout->setSpacing(4);

    auto *appNameLabel = new QLabel(QCoreApplication::applicationName(), aboutContent);
    QFont appNameFont  = appNameLabel->font();
    appNameFont.setPointSizeF(Theme::fontLg);
    appNameFont.setBold(true);
    appNameLabel->setFont(appNameFont);

    auto *versionLabel = new QLabel(
        QStringLiteral("Version %1").arg(QCoreApplication::applicationVersion()), aboutContent);

    auto *qtLabel = new QLabel(
        QStringLiteral("Built with Qt %1").arg(QT_VERSION_STR), aboutContent);
    qtLabel->setForegroundRole(QPalette::PlaceholderText);

    auto *aboutSep = new QFrame(aboutContent);
    aboutSep->setFrameShape(QFrame::HLine);
    aboutSep->setFrameShadow(QFrame::Sunken);

    auto *configTitle = new QLabel("Config file", aboutContent);
    QFont configTitleFont = configTitle->font();
    configTitleFont.setBold(true);
    configTitle->setFont(configTitleFont);

    const QString configPath = AppConfig::configPath();
    auto *configPathLabel    = new QLabel(configPath, aboutContent);
    configPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    configPathLabel->setWordWrap(true);
    configPathLabel->setForegroundRole(QPalette::PlaceholderText);

    auto *copyBtn = new QPushButton(
        QIcon::fromTheme("edit-copy", QIcon(":/icons/copy.png")), "Copy path", aboutContent);
    copyBtn->setFlat(true);
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QPushButton::clicked, this, [configPath] {
        QGuiApplication::clipboard()->setText(configPath);
    });

    aboutLayout->addWidget(appNameLabel);
    aboutLayout->addWidget(versionLabel);
    aboutLayout->addSpacing(4);
    aboutLayout->addWidget(qtLabel);
    aboutLayout->addSpacing(12);
    aboutLayout->addWidget(aboutSep);
    aboutLayout->addSpacing(8);
    aboutLayout->addWidget(configTitle);
    aboutLayout->addWidget(configPathLabel);
    aboutLayout->addSpacing(4);
    aboutLayout->addWidget(copyBtn, 0, Qt::AlignLeft);
    aboutLayout->addStretch(1);

    m_stack->addWidget(aboutContent); // index 4

    // ---- Page 5: Alerts -----------------------------------------------
    auto *alertsContent = new QWidget;
    auto *alertsLayout  = new QVBoxLayout(alertsContent);
    alertsLayout->setContentsMargins(12, 12, 12, 12);
    alertsLayout->setSpacing(8);

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

    m_stack->addWidget(alertsContent); // index 5

    // ---- Signal connections -------------------------------------------
    connect(m_autoDetect,     &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs,    &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
    connect(m_exeNames,       &ListEditor::itemsChanged, this, &SettingsPage::saveAndEmit);
    connect(m_enableOverlay,  &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_startMinimized, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_showGuildTags,  &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
}

void SettingsPage::navigateTo(int pageIndex, const QString &title)
{
    if (pageIndex == 5)
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
    m_config.startMinimized  = m_startMinimized->isChecked();
    m_config.minimizeToTray  = m_minimizeToTray->isChecked();
    m_config.showGuildTags   = m_showGuildTags->isChecked();
    m_config.save();
    emit configChanged();
}
