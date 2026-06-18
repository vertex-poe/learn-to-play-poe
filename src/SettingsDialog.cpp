#include "SettingsDialog.h"
#include "ListEditor.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QIcon>
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

    m_installDirs = new ListEditor("Path to install directory", this);
    m_installDirs->setItems(config.installDirs);
    m_installDirs->setEnabled(!config.autoDetectInstallDir);
    form->addRow("Install directories:", m_installDirs);

    m_exeNames = new ListEditor("Executable name (e.g. PathOfExile_x64Steam.exe)", this);
    m_exeNames->setItems(config.executableNames);
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

    connect(m_autoDetect,  &QCheckBox::toggled,    this, &SettingsDialog::onAutoDetectToggled);
    connect(m_autoDetect,  &QCheckBox::toggled,    this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs, &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_exeNames,    &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_startMinimized, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_enableOverlay,  &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled, this, [this](bool) { saveAndEmit(); });
}

void SettingsDialog::onAutoDetectToggled(bool checked)
{
    m_installDirs->setEnabled(!checked);
}

void SettingsDialog::saveAndEmit()
{
    m_config.autoDetectInstallDir = m_autoDetect->isChecked();
    m_config.installDirs          = m_installDirs->items();
    m_config.executableNames      = m_exeNames->items();
    m_config.useGameOverlay       = m_enableOverlay->isChecked();
    m_config.startMinimized       = m_startMinimized->isChecked();
    m_config.minimizeToTray       = m_minimizeToTray->isChecked();
    m_config.save();
    emit configChanged();
}
