#include "ScrollJumpButton.h"
#include "Theme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QPen>
#include <QSvgRenderer>

ScrollJumpButton::ScrollJumpButton(QWidget *parent)
    : QPushButton(parent)
{
    setFixedSize(54, 54);
    setCursor(Qt::PointingHandCursor);
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
    const qreal dpr = devicePixelRatioF();
    QPixmap pix(qRound(iconSize * dpr), qRound(iconSize * dpr));
    pix.setDevicePixelRatio(dpr);
    pix.fill(Qt::transparent);
    { QPainter gp(&pix); QSvgRenderer(QStringLiteral(":/icons/arrow-down.svg")).render(&gp); }
    { QPainter cp(&pix);
      cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
      cp.fillRect(pix.rect(), Theme::textPrimary); }
    p.drawPixmap(pad, pad, pix);
}
