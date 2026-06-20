#include "LiveAlertsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Event presets — each maps a display name to an (eventType, dataFilter) pair
// and a placeholder hint for the message template field.
// ---------------------------------------------------------------------------
struct EventPreset {
    QString     label;
    QString     eventType;
    QVariantMap dataFilter;
    QString     hint;
};

static const QVector<EventPreset>& eventPresets()
{
    static const QVector<EventPreset> presets = {
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
    return presets;
}

// ---------------------------------------------------------------------------
// Action presets
// ---------------------------------------------------------------------------
struct ActionPreset {
    QString label;
    QString actionType;
};

static const QVector<ActionPreset>& actionPresets()
{
    static const QVector<ActionPreset> presets = {
        {"Show notification", "notify"},
    };
    return presets;
}

// ---------------------------------------------------------------------------
// Helper: find event preset index matching a rule's eventType + dataFilter
// ---------------------------------------------------------------------------
static int findEventPresetIndex(const LiveEventRule& rule)
{
    const auto& presets = eventPresets();
    for (int i = 0; i < presets.size(); ++i) {
        const auto& p = presets[i];
        if (p.eventType != rule.eventType) continue;
        if (p.dataFilter != rule.dataFilter) continue;
        return i;
    }
    return 0;
}

static int findActionPresetIndex(const LiveEventRule& rule)
{
    const auto& presets = actionPresets();
    for (int i = 0; i < presets.size(); ++i) {
        if (presets[i].actionType == rule.actionType)
            return i;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// LiveAlertsDialog
// ---------------------------------------------------------------------------
LiveAlertsDialog::LiveAlertsDialog(const QVector<LiveEventRule>& rules, QWidget* parent)
    : QDialog(parent)
    , m_rules(rules)
{
    setWindowTitle("Live Alert Rules");
    setMinimumSize(560, 360);

    m_list = new QListWidget(this);

    auto* btnAdd    = new QPushButton("Add", this);
    auto* btnEdit   = new QPushButton("Edit", this);
    auto* btnRemove = new QPushButton("Remove", this);

    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(btnAdd);
    btnRow->addWidget(btnEdit);
    btnRow->addWidget(btnRemove);

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    auto* vbox = new QVBoxLayout(this);
    vbox->addWidget(new QLabel("Alert rules — when a game event fires, take an action:", this));
    vbox->addWidget(m_list, 1);
    vbox->addLayout(btnRow);
    vbox->addWidget(bbox);

    connect(btnAdd,    &QPushButton::clicked, this, &LiveAlertsDialog::addRule);
    connect(btnEdit,   &QPushButton::clicked, this, &LiveAlertsDialog::editRule);
    connect(btnRemove, &QPushButton::clicked, this, &LiveAlertsDialog::removeRule);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &LiveAlertsDialog::editRule);
    connect(bbox, &QDialogButtonBox::accepted, this, [this]() {
        // Sync checkbox states back to m_rules before accepting.
        for (int i = 0; i < m_list->count() && i < m_rules.size(); ++i)
            m_rules[i].enabled = (m_list->item(i)->checkState() == Qt::Checked);
        accept();
    });
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    rebuildList();
}

void LiveAlertsDialog::rebuildList()
{
    m_list->clear();
    for (const auto& rule : m_rules) {
        auto* item = new QListWidgetItem(ruleDescription(rule), m_list);
        item->setCheckState(rule.enabled ? Qt::Checked : Qt::Unchecked);
    }
}

QString LiveAlertsDialog::ruleDescription(const LiveEventRule& rule) const
{
    const QString action = rule.actionType == QLatin1String("notify")
        ? QStringLiteral("Show notification")
        : rule.actionType;
    const QString msg = rule.actionParams.value("message").toString();
    const QString detail = msg.isEmpty() ? QString() : QStringLiteral(": \"%1\"").arg(msg);
    return QStringLiteral("When: %1  →  %2%3").arg(rule.label, action, detail);
}

void LiveAlertsDialog::addRule()
{
    LiveEventRule rule;
    rule.id      = QString::number(QDateTime::currentMSecsSinceEpoch());
    rule.enabled = true;
    // Set defaults from first preset
    const auto& ep = eventPresets().first();
    rule.label      = ep.label;
    rule.eventType  = ep.eventType;
    rule.dataFilter = ep.dataFilter;
    rule.actionType = actionPresets().first().actionType;
    rule.actionParams["message"] = ep.hint;

    if (editRuleDialog(rule)) {
        m_rules.append(rule);
        rebuildList();
        m_list->setCurrentRow(m_rules.size() - 1);
    }
}

void LiveAlertsDialog::editRule()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_rules.size()) return;

    LiveEventRule rule = m_rules[row];
    if (editRuleDialog(rule)) {
        // Preserve enabled state from checkbox
        rule.enabled = m_list->item(row)->checkState() == Qt::Checked;
        m_rules[row] = rule;
        rebuildList();
        m_list->setCurrentRow(row);
    }
}

void LiveAlertsDialog::removeRule()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_rules.size()) return;
    m_rules.removeAt(row);
    rebuildList();
}

// ---------------------------------------------------------------------------
// Inline "Edit Rule" dialog
// ---------------------------------------------------------------------------
bool LiveAlertsDialog::editRuleDialog(LiveEventRule& rule)
{
    QDialog dlg(this);
    dlg.setWindowTitle("Edit Alert Rule");
    dlg.setMinimumWidth(440);

    auto* eventCombo  = new QComboBox(&dlg);
    auto* actionCombo = new QComboBox(&dlg);
    auto* titleEdit   = new QLineEdit(rule.actionParams.value("title").toString(), &dlg);
    auto* msgEdit     = new QLineEdit(rule.actionParams.value("message").toString(), &dlg);
    auto* hintLabel   = new QLabel(&dlg);
    hintLabel->setWordWrap(true);

    const auto& ePresets = eventPresets();
    for (const auto& p : ePresets)
        eventCombo->addItem(p.label);
    eventCombo->setCurrentIndex(findEventPresetIndex(rule));

    for (const auto& p : actionPresets())
        actionCombo->addItem(p.label);
    actionCombo->setCurrentIndex(findActionPresetIndex(rule));

    auto updateHint = [&](int idx) {
        if (idx < 0 || idx >= ePresets.size()) return;
        const QString& hint = ePresets[idx].hint;
        hintLabel->setText(hint.isEmpty()
            ? QString()
            : QStringLiteral("Available: %1").arg(hint));
    };
    updateHint(eventCombo->currentIndex());
    QObject::connect(eventCombo, &QComboBox::currentIndexChanged, &dlg, [&](int idx) {
        updateHint(idx);
    });

    auto* bbox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    QObject::connect(bbox, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(bbox, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    auto* form = new QFormLayout;
    form->addRow("When:", eventCombo);
    form->addRow("Do:", actionCombo);
    form->addRow("Title:", titleEdit);
    form->addRow("Message:", msgEdit);
    form->addRow(hintLabel);

    auto* vbox = new QVBoxLayout(&dlg);
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
