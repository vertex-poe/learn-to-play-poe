#include "SettingsDialog.h"
#include "AppConfig.h"
#include "Theme.h"
#include "ListEditor.h"

#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QFormLayout>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

SettingsDialog::SettingsDialog(AppConfig &config, QWidget *parent)
    : QDialog(parent)
    , m_config(config)
{
    setWindowTitle("Settings");
    setWindowIcon(QIcon(":/icons/vertex-icon.png"));
    setMinimumWidth(480);

    // ---- Header -------------------------------------------------------
    m_backBtn = new QPushButton("← Back", this);
    m_backBtn->setFlat(true);
    m_backBtn->setVisible(false);
    connect(m_backBtn, &QPushButton::clicked, this, &SettingsDialog::navigateBack);

    m_titleLabel = new QLabel("Settings", this);
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
    categoryLayout->setContentsMargins(0, 0, 0, 0);
    categoryLayout->setSpacing(0);

    // Page indices: 0=root, 1=Game, 2=Window, 3=Chat, 4=About
    const auto addCategory = [&](const QString &label, int pageIndex) {
        auto *btn = new QPushButton(label + "  ›", categoryPage);
        btn->setFlat(true);
        btn->setMinimumHeight(44);
        btn->setStyleSheet("QPushButton { text-align: left; padding: 4px 12px; }");
        connect(btn, &QPushButton::clicked, this, [this, pageIndex, label] {
            navigateTo(pageIndex, label);
        });
        categoryLayout->addWidget(btn);
        auto *sep = new QFrame(categoryPage);
        sep->setFrameShape(QFrame::HLine);
        sep->setFrameShadow(QFrame::Sunken);
        categoryLayout->addWidget(sep);
    };

    m_stack->addWidget(categoryPage); // index 0

    addCategory("Game",   1);
    addCategory("Window", 2);
    addCategory("Chat",   3);
    addCategory("About",  4);
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

    // ---- Signal connections -------------------------------------------
    connect(m_autoDetect,    &QCheckBox::toggled,       this, [this](bool) { saveAndEmit(); });
    connect(m_installDirs,   &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_exeNames,      &ListEditor::itemsChanged, this, &SettingsDialog::saveAndEmit);
    connect(m_enableOverlay,  &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_startMinimized, &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_minimizeToTray, &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
    connect(m_showGuildTags,  &QCheckBox::toggled,      this, [this](bool) { saveAndEmit(); });
}

void SettingsDialog::navigateTo(int pageIndex, const QString &title)
{
    m_titleLabel->setText(title);
    m_backBtn->setVisible(true);
    m_stack->setCurrentIndex(pageIndex);
}

void SettingsDialog::navigateBack()
{
    m_titleLabel->setText("Settings");
    m_backBtn->setVisible(false);
    m_stack->setCurrentIndex(0);
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
    m_config.useGameOverlay  = m_enableOverlay->isChecked();
    m_config.startMinimized  = m_startMinimized->isChecked();
    m_config.minimizeToTray  = m_minimizeToTray->isChecked();
    m_config.showGuildTags   = m_showGuildTags->isChecked();
    m_config.save();
    emit configChanged();
}
