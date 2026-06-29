#include "ui/overlay/GameOverlay.h"
#include "ui/Theme.h"

#ifdef _WIN32
#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "platform/OverlayKeepalive.h"
#endif

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QScreen>
#include <QBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QThread>

namespace {

class InfoPanel : public QWidget
{
public:
    explicit InfoPanel(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text)
    {
        setContentsMargins(12, 6, 9, 6);
        QFont f = font();
        f.setPointSizeF(Theme::fontLg);
        f.setItalic(true);
        f.setBold(true);
        f.setStyleHint(QFont::Serif);
        f.setFamilies({"Palatino Linotype", "Book Antiqua", "Palatino", "serif"});
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
        setFont(f);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setOnClick(std::function<void()> cb)
    {
        m_onClick = std::move(cb);
        setCursor(Qt::PointingHandCursor);
    }

    QSize sizeHint() const override
    {
        const QFontMetrics fm(font());
        const QRect br = fm.boundingRect(m_text);
        const QMargins m = contentsMargins();
        return {br.width() + m.left() + m.right(),
                br.height() + m.top() + m.bottom()};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QColor border = Theme::accent;
        border.setAlpha(115);  // 0.45 * 255

        p.setPen(QPen(border, 1));
        p.setBrush(QColor(15, 10, 2, 150));
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        p.setPen(Theme::accent);
        p.setFont(font());
        p.drawText(contentsRect(), Qt::AlignCenter, m_text);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_onClick) {
            m_onClick();
        }
    }

private:
    QString m_text;
    std::function<void()> m_onClick;
};

class ClickableIcon : public QWidget
{
public:
    explicit ClickableIcon(const QString &svgPath, const QString &command, const QColor &bgColor, const QColor &borderColor, QWidget *parent = nullptr)
        : QWidget(parent), m_icon(svgPath), m_command(command), m_bgColor(bgColor), m_borderColor(borderColor)
    {
        setFixedSize(36, 36);
        setCursor(Qt::PointingHandCursor);
    }

    void setGameHwnd(quint64 hwnd) { m_gameHwnd = hwnd; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        p.setPen(QPen(m_borderColor, 1));
        p.setBrush(m_bgColor);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);

        if (!m_icon.isNull()) {
            QRect iconRect((width() - 28) / 2, (height() - 28) / 2, 28, 28);
            m_icon.paint(&p, iconRect, Qt::AlignCenter);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
#ifdef _WIN32
            if (m_gameHwnd != 0) {
                HWND hwnd = reinterpret_cast<HWND>(m_gameHwnd);
                if (GetForegroundWindow() != hwnd) {
                    SetForegroundWindow(hwnd);
                    QThread::msleep(50); // allow window to activate
                }
            }

            auto sendKey = [](WORD vk) {
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wVk = vk;
                SendInput(1, &input, sizeof(INPUT));
                input.ki.dwFlags = KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(INPUT));
            };

            // Enter
            sendKey(VK_RETURN);
            QThread::msleep(20);

            // command
            for (QChar c : m_command) {
                INPUT input = {0};
                input.type = INPUT_KEYBOARD;
                input.ki.wScan = c.unicode();
                input.ki.dwFlags = KEYEVENTF_UNICODE;
                SendInput(1, &input, sizeof(INPUT));
                input.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
                SendInput(1, &input, sizeof(INPUT));
            }

            QThread::msleep(20);
            // Enter
            sendKey(VK_RETURN);
#endif
        }
        QWidget::mousePressEvent(event);
    }

private:
    QIcon m_icon;
    QString m_command;
    QColor m_bgColor;
    QColor m_borderColor;
    quint64 m_gameHwnd{0};
};

} // namespace

GameOverlay::GameOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);

    m_panelContainer = new QWidget(this);
    auto *layout = new QGridLayout(m_panelContainer);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    m_infoPanel = new InfoPanel(QStringLiteral("l2p"), m_panelContainer);
    static_cast<InfoPanel*>(m_infoPanel)->setOnClick([this]() { emit showMainWindowRequested(); });

    m_hideoutIcon = new ClickableIcon(QStringLiteral(":/icons/fleur-de-lis.svg"), QStringLiteral("/hideout"), QColor(30, 45, 65, 150), QColor("#64aad7"), m_panelContainer);

    m_guildIcon = new ClickableIcon(QStringLiteral(":/icons/fleur-de-lis-shield.svg"), QStringLiteral("/guild"), QColor(15, 10, 2, 150), QColor("#64aad7"), m_panelContainer);

    m_menagerieIcon = new ClickableIcon(QStringLiteral(":/icons/cattle-skull.svg"), QStringLiteral("/menagerie"), QColor(65, 30, 30, 150), QColor("#cd4b6e"), m_panelContainer);

    m_monasteryIcon = new ClickableIcon(QStringLiteral(":/icons/branch.svg"), QStringLiteral("/monastery"), QColor(70, 30, 50, 150), QColor("#e67eb4"), m_panelContainer);

    m_heistIcon = new ClickableIcon(QStringLiteral(":/icons/safe2-fill.svg"), QStringLiteral("/heist"), QColor(50, 40, 0, 150), QColor("#c8983a"), m_panelContainer);

    m_sanctumIcon = new ClickableIcon(QStringLiteral(":/icons/door-open-fill.svg"), QStringLiteral("/sanctum"), QColor(30, 40, 60, 150), QColor("#aa8728"), m_panelContainer);

    m_ladderIcon = new ClickableIcon(QStringLiteral(":/icons/trophy-fill.svg"), QStringLiteral("/ladder"), QColor(60, 50, 0, 150), QColor("#e6b800"), m_panelContainer);

    m_delveIcon = new ClickableIcon(QStringLiteral(":/icons/minecart-loaded.svg"), QStringLiteral("/delve"), QColor(10, 50, 70, 150), QColor("#4dd2ff"), m_panelContainer);

    m_kingsmarchIcon = new ClickableIcon(QStringLiteral(":/icons/shop.svg"), QStringLiteral("/kingsmarch"), QColor(30, 40, 50, 150), QColor("#919eac"), m_panelContainer);
    m_timeplayedIcon = new ClickableIcon(QStringLiteral(":/icons/stopwatch-fill.svg"), QStringLiteral("/played"), QColor(40, 30, 50, 150), QColor("#7864a0"), m_panelContainer);
    m_characterageIcon = new ClickableIcon(QStringLiteral(":/icons/stopwatch-fill.svg"), QStringLiteral("/age"), QColor(40, 30, 50, 150), QColor("#7864a0"), m_panelContainer);
    m_passivesIcon = new ClickableIcon(QStringLiteral(":/icons/tree-fill.svg"), QStringLiteral("/passives"), QColor(50, 20, 30, 150), QColor("#d26e9b"), m_panelContainer);
    m_deathsIcon = new ClickableIcon(QStringLiteral(":/icons/person-fill.svg"), QStringLiteral("/deaths"), QColor(50, 40, 30, 150), QColor("#a0825f"), m_panelContainer);
    m_monstersremainingIcon = new ClickableIcon(QStringLiteral(":/icons/bug-fill.svg"), QStringLiteral("/remaining"), QColor(60, 20, 30, 150), QColor("#cd4b6e"), m_panelContainer);
    m_atlaspassivesIcon = new ClickableIcon(QStringLiteral(":/icons/map-fill.svg"), QStringLiteral("/atlaspassives"), QColor(40, 30, 60, 150), QColor("#9b6ed2"), m_panelContainer);
    m_killsIcon = new ClickableIcon(QStringLiteral(":/icons/bullseye.svg"), QStringLiteral("/kills"), QColor(50, 40, 10, 150), QColor("#aa8728"), m_panelContainer);
    m_resetxpIcon = new ClickableIcon(QStringLiteral(":/icons/box-arrow-in-right.svg"), QStringLiteral("/reset_xp"), QColor(20, 40, 60, 150), QColor("#64aad7"), m_panelContainer);
    m_reloaditemfilterIcon = new ClickableIcon(QStringLiteral(":/icons/indent.svg"), QStringLiteral("/reloaditemfilter"), QColor(30, 30, 40, 150), QColor("#6e6e82"), m_panelContainer);
    m_panelContainer->adjustSize();

#ifdef _WIN32
    // Calling winId() forces native HWND creation so we can read/set exstyles now.
    const auto hwnd = reinterpret_cast<HWND>(winId());
    const LONG ex   = GetWindowLong(hwnd, GWL_EXSTYLE);
    // Remove WS_EX_TRANSPARENT so mouse clicks can reach the overlay, but keep WS_EX_NOACTIVATE
    // to avoid stealing focus from the game.
    SetWindowLong(hwnd, GWL_EXSTYLE, (ex | WS_EX_LAYERED | WS_EX_NOACTIVATE) & ~WS_EX_TRANSPARENT);
    m_keepalive = new OverlayKeepalive(hwnd);
#endif
}

GameOverlay::~GameOverlay()
{
#ifdef _WIN32
    delete m_keepalive;
#endif
}

void GameOverlay::updateGameRect(const QRect &physicalRect)
{
#ifdef _WIN32
    // Win32 GetWindowRect returns physical px; Qt setGeometry wants logical px.
    const QScreen *scr = QGuiApplication::primaryScreen();
    const qreal    dpr = scr ? scr->devicePixelRatio() : 1.0;
    setGeometry(QRect(
        qRound(physicalRect.x()      / dpr),
        qRound(physicalRect.y()      / dpr),
        qRound(physicalRect.width()  / dpr),
        qRound(physicalRect.height() / dpr)
    ));
#else
    setGeometry(physicalRect);
#endif
}

void GameOverlay::setGameVisible(bool found)
{
    setVisible(found);
}

void GameOverlay::setLayoutGrid(int columns, int rows)
{
    m_gridColumns = columns;
    m_gridRows = rows;
    repositionPanels();
}

void GameOverlay::rebuildGridLayout()
{
    if (!m_panelContainer) return;
    auto *layout = qobject_cast<QGridLayout*>(m_panelContainer->layout());
    if (!layout) return;

    // Remove all items
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr) {
        delete item;
    }

    QList<QWidget*> visibleWidgets;
    if (m_infoPanel && !m_infoPanel->isHidden()) visibleWidgets.append(m_infoPanel);
    if (m_hideoutIcon && !m_hideoutIcon->isHidden()) visibleWidgets.append(m_hideoutIcon);
    if (m_guildIcon && !m_guildIcon->isHidden()) visibleWidgets.append(m_guildIcon);
    if (m_menagerieIcon && !m_menagerieIcon->isHidden()) visibleWidgets.append(m_menagerieIcon);
    if (m_monasteryIcon && !m_monasteryIcon->isHidden()) visibleWidgets.append(m_monasteryIcon);
    if (m_heistIcon && !m_heistIcon->isHidden()) visibleWidgets.append(m_heistIcon);
    if (m_sanctumIcon && !m_sanctumIcon->isHidden()) visibleWidgets.append(m_sanctumIcon);
    if (m_ladderIcon && !m_ladderIcon->isHidden()) visibleWidgets.append(m_ladderIcon);
    if (m_delveIcon && !m_delveIcon->isHidden()) visibleWidgets.append(m_delveIcon);
    if (m_kingsmarchIcon && !m_kingsmarchIcon->isHidden()) visibleWidgets.append(m_kingsmarchIcon);
    if (m_timeplayedIcon && !m_timeplayedIcon->isHidden()) visibleWidgets.append(m_timeplayedIcon);
    if (m_characterageIcon && !m_characterageIcon->isHidden()) visibleWidgets.append(m_characterageIcon);
    if (m_passivesIcon && !m_passivesIcon->isHidden()) visibleWidgets.append(m_passivesIcon);
    if (m_deathsIcon && !m_deathsIcon->isHidden()) visibleWidgets.append(m_deathsIcon);
    if (m_monstersremainingIcon && !m_monstersremainingIcon->isHidden()) visibleWidgets.append(m_monstersremainingIcon);
    if (m_atlaspassivesIcon && !m_atlaspassivesIcon->isHidden()) visibleWidgets.append(m_atlaspassivesIcon);
    if (m_killsIcon && !m_killsIcon->isHidden()) visibleWidgets.append(m_killsIcon);
    if (m_resetxpIcon && !m_resetxpIcon->isHidden()) visibleWidgets.append(m_resetxpIcon);
    if (m_reloaditemfilterIcon && !m_reloaditemfilterIcon->isHidden()) visibleWidgets.append(m_reloaditemfilterIcon);

    for (int i = 0; i < visibleWidgets.size(); ++i) {
        int row = 0, col = 0;
        if (m_gridColumns > 0) {
            col = i % m_gridColumns;
            row = i / m_gridColumns;
        } else if (m_gridRows > 0) {
            row = i % m_gridRows;
            col = i / m_gridRows;
        } else {
            col = 0;
            row = i;
        }
        layout->addWidget(visibleWidgets[i], row, col, Qt::AlignHCenter);
    }
}


void GameOverlay::setHideoutVisible(bool visible)
{
    m_hideoutIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setGuildVisible(bool visible)
{
    m_guildIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setMenagerieVisible(bool visible)
{
    m_menagerieIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setMonasteryVisible(bool visible)
{
    m_monasteryIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setHeistVisible(bool visible)
{
    m_heistIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setSanctumVisible(bool visible)
{
    m_sanctumIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setLadderVisible(bool visible)
{
    m_ladderIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setDelveVisible(bool visible)
{
    m_delveIcon->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setKingsmarchVisible(bool visible)
{
    m_kingsmarchIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setTimePlayedVisible(bool visible)
{
    m_timeplayedIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setCharacterAgeVisible(bool visible)
{
    m_characterageIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setPassivesVisible(bool visible)
{
    m_passivesIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setDeathsVisible(bool visible)
{
    m_deathsIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setMonstersRemainingVisible(bool visible)
{
    m_monstersremainingIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setAtlasPassivesVisible(bool visible)
{
    m_atlaspassivesIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setKillsVisible(bool visible)
{
    m_killsIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setResetXPVisible(bool visible)
{
    m_resetxpIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setReloadItemFilterVisible(bool visible)
{
    m_reloaditemfilterIcon->setVisible(visible);
    repositionPanels();
}
void GameOverlay::setL2PVisible(bool visible)
{
    m_infoPanel->setVisible(visible);
    repositionPanels();
}

void GameOverlay::setGameHwnd(quint64 hwnd)
{
    if (m_hideoutIcon)
        static_cast<ClickableIcon*>(m_hideoutIcon)->setGameHwnd(hwnd);
    if (m_guildIcon)
        static_cast<ClickableIcon*>(m_guildIcon)->setGameHwnd(hwnd);
    if (m_menagerieIcon)
        static_cast<ClickableIcon*>(m_menagerieIcon)->setGameHwnd(hwnd);
    if (m_monasteryIcon)
        static_cast<ClickableIcon*>(m_monasteryIcon)->setGameHwnd(hwnd);
    if (m_heistIcon)
        static_cast<ClickableIcon*>(m_heistIcon)->setGameHwnd(hwnd);
    if (m_sanctumIcon)
        static_cast<ClickableIcon*>(m_sanctumIcon)->setGameHwnd(hwnd);
    if (m_ladderIcon)
        static_cast<ClickableIcon*>(m_ladderIcon)->setGameHwnd(hwnd);
    if (m_delveIcon)
        static_cast<ClickableIcon*>(m_delveIcon)->setGameHwnd(hwnd);
    if (m_kingsmarchIcon) static_cast<ClickableIcon*>(m_kingsmarchIcon)->setGameHwnd(hwnd);
    if (m_timeplayedIcon) static_cast<ClickableIcon*>(m_timeplayedIcon)->setGameHwnd(hwnd);
    if (m_characterageIcon) static_cast<ClickableIcon*>(m_characterageIcon)->setGameHwnd(hwnd);
    if (m_passivesIcon) static_cast<ClickableIcon*>(m_passivesIcon)->setGameHwnd(hwnd);
    if (m_deathsIcon) static_cast<ClickableIcon*>(m_deathsIcon)->setGameHwnd(hwnd);
    if (m_monstersremainingIcon) static_cast<ClickableIcon*>(m_monstersremainingIcon)->setGameHwnd(hwnd);
    if (m_atlaspassivesIcon) static_cast<ClickableIcon*>(m_atlaspassivesIcon)->setGameHwnd(hwnd);
    if (m_killsIcon) static_cast<ClickableIcon*>(m_killsIcon)->setGameHwnd(hwnd);
    if (m_resetxpIcon) static_cast<ClickableIcon*>(m_resetxpIcon)->setGameHwnd(hwnd);
    if (m_reloaditemfilterIcon) static_cast<ClickableIcon*>(m_reloaditemfilterIcon)->setGameHwnd(hwnd);
}

void GameOverlay::paintEvent(QPaintEvent *)
{
    // Clear the entire surface to transparent so the game shows through.
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Clear);
    painter.fillRect(rect(), Qt::transparent);
}

void GameOverlay::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    repositionPanels();
}

void GameOverlay::repositionPanels()
{
    if (!m_panelContainer)
        return;
    rebuildGridLayout();
    m_panelContainer->adjustSize();
    const int margin = 10;
    m_panelContainer->move(margin, margin);
    updateMask();
}

void GameOverlay::updateMask()
{
    // Only the panel regions intercept mouse events; everything else is click-through.
    QRegion mask;
    if (m_panelContainer)
        mask |= QRegion(m_panelContainer->geometry());
    setMask(mask);
}
