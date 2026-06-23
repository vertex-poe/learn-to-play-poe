#include "ScrollJumpButton.h"
#include "Theme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPen>

ScrollJumpButton::ScrollJumpButton(QWidget *parent)
    : QPushButton(parent)
{
    setFixedSize(54, 54);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Scroll to bottom");
}

void ScrollJumpButton::setSkipMode(bool skip)
{
    if (m_skipMode == skip) return;
    m_skipMode = skip;
    setToolTip(skip ? "Skip to bottom" : "Scroll to bottom");
    update();
}

void ScrollJumpButton::enterEvent(QEnterEvent *e)
{
    QPushButton::enterEvent(e);
    update();
}

void ScrollJumpButton::leaveEvent(QEvent *e)
{
    QPushButton::leaveEvent(e);
    update();
}

void ScrollJumpButton::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QColor bg = underMouse() ? Theme::bgButtonHover : Theme::bgButton;
    p.setPen(QPen(Theme::borderNormal, 1));
    p.setBrush(bg);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 12, 12);

    const int pad = 12;
    const int iconSize = width() - pad * 2;
    const QString iconPath = m_skipMode ? QStringLiteral(":/icons/chevron-bar-down.svg")
                                        : QStringLiteral(":/icons/arrow-down.svg");
    const QPixmap pix = Theme::renderSvgIcon(iconPath, Theme::textPrimary, {iconSize, iconSize}, devicePixelRatioF());
    p.drawPixmap(QRect(pad, pad, iconSize, iconSize), pix, QRect(0, 0, pix.width(), pix.height()));
}
