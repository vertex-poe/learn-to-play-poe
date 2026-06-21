#include "SettingsDialog.h"
#include "AppConfig.h"
#include "ListEditor.h"

#include <QCheckBox>
#include <QClipboard>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(AppConfig &config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle("Settings");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    setMinimumWidth(480);

    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    layout->addLayout(form);

    m_autoDetect = new QCheckBox(this);
    m_autoDetect->setChecked(config.autoDetectInstallDir);
    form->addRow("Auto-detect install directories:", m_autoDetect);

    m_installDirs = new ListEditor({}, this);
    m_installDirs->setBrowseForDirectory(true);
    m_installDirs->setItems(config.installDirs);
    form->addRow("Install directories:", m_installDirs);

    m_exeNames = new ListEditor("Executable name (e.g. PathOfExile_x64Steam.exe)", this);
    m_exeNames->setBuiltinItems(AppConfig::knownExes());
    m_exeNames->setItems(config.executableNames);
    m_exeNames->setInputFileBrowser(true);
    form->addRow("Executable names:", m_exeNames);

    m_enableOverlay = new QCheckBox(this);
    m_enableOverlay->setChecked(config.useGameOverlay);
    form->addRow("Enable overlay:", m_enableOverlay);

    m_startMinimized = new QCheckBox(this);
    m_startMinimized->setChecked(config.startMinimized);
    form->addRow("Start minimized:", m_startMinimized);

    m_minimizeToTray = new QCheckBox(this);
    m_minimizeToTray->setChecked(config.minimizeToTray);
    form->addRow("Minimize to tray on close:", m_minimizeToTray);

    m_autoUpdate = new QCheckBox("(coming soon)", this);
    m_autoUpdate->setChecked(config.autoUpdate);
    m_autoUpdate->setEnabled(false);
    form->addRow("Auto-update application:", m_autoUpdate);

    m_autoStartOnBoot = new QCheckBox("(coming soon)", this);
    m_autoStartOnBoot->setChecked(config.autoStartOnBoot);
    m_autoStartOnBoot->setEnabled(false);
    form->addRow("Auto start on boot:", m_autoStartOnBoot);

    m_showGuildTags = new QCheckBox(this);
    m_showGuildTags->setChecked(config.showGuildTags);
    form->addRow("Display guild tags:", m_showGuildTags);

    const QString configPath = AppConfig::configPath();
    auto *pathLabel = new QLabel(configPath, this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto *copyBtn = new QPushButton(QIcon::fromTheme("edit-copy", QIcon(":/icons/copy.png")), "", this);
    copyBtn->setToolTip("Copy path to clipboard");
    copyBtn->setFlat(true);
    copyBtn->setFixedSize(22, 22);
    copyBtn->setCursor(Qt::PointingHandCursor);
    connect(copyBtn, &QPushButton::clicked, this, [configPath] {
        QGuiApplication::clipboard()->setText(configPath);
    });
    auto *pathRow = new QHBoxLayout;
    pathRow->setContentsMargins(0, 0, 0, 0);
    pathRow->setSpacing(4);
    pathRow->addWidget(pathLabel, 1);
    pathRow->addWidget(copyBtn);
    form->addRow("Config file:", pathRow);

    connect(m_autoDetect,  &QCheckBox::toggled,    this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs, &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_exeNames,    &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_startMinimized, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_enableOverlay,  &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_showGuildTags,  &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
}

void SettingsDialog::saveAndEmit()
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
    m_config.useGameOverlay       = m_enableOverlay->isChecked();
    m_config.startMinimized       = m_startMinimized->isChecked();
    m_config.minimizeToTray       = m_minimizeToTray->isChecked();
    m_config.showGuildTags        = m_showGuildTags->isChecked();
    m_config.save();
    emit configChanged();
}
