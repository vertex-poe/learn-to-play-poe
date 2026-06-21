#include "NavBar.h"
#include "Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QSvgRenderer>

NavBar::NavBar(const QStringList &labels, QWidget *parent)
    : QWidget(parent), m_labels(labels)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void NavBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_labels.size())
        return;
    if (index == m_current && !m_gearActive)
        return;
    m_current = index;
    m_gearActive = false;
    update();
    emit currentChanged(index);
}

void NavBar::setGearActive(bool active)
{
    if (m_gearActive == active)
        return;
    m_gearActive = active;
    update();
}

QSize NavBar::sizeHint() const
{
    QFont f = font();
    f.setPointSizeF(Theme::font2xl);
    return {0, QFontMetrics(f).height() + 28};
}

void NavBar::paintEvent(QPaintEvent *)
{
    QPainter p(this);

    const int w = width();
    const int h = height();
    const int n = m_labels.size();
    if (n == 0) return;

    const int tabAreaW = w - k_gearWidth;
    const int colW     = tabAreaW / n;
    const int separatorH = 3;
    const int underlineH = 8;

    p.fillRect(rect(), palette().window());

    // Bottom separator
    p.fillRect(0, h - separatorH, w, separatorH, palette().mid().color());

    QFont f = font();
    f.setPointSizeF(Theme::font2xl);

    for (int i = 0; i < n; ++i) {
        const int x  = i * colW;
        const int cw = (i == n - 1) ? tabAreaW - x : colW;
        const QRect cell(x, 0, cw, h - separatorH);
        const bool active = (i == m_current) && !m_gearActive;

        f.setBold(active);
        p.setFont(f);
        p.setPen(active ? palette().windowText().color()
                        : palette().placeholderText().color());
        p.drawText(cell, Qt::AlignCenter, m_labels[i]);

        if (active) {
            p.fillRect(x, h - separatorH - underlineH, cw, underlineH,
                       palette().highlight().color());
        }
    }

    // Gear icon
    const QColor gearColor = m_gearActive ? palette().windowText().color()
                                          : palette().placeholderText().color();
    const int cellH   = h - separatorH;
    const int iconSize = QFontMetrics(f).height() - 6;
    const int iconX    = tabAreaW + (k_gearWidth - iconSize) / 2;
    const int iconY    = (cellH - iconSize) / 2;

    const qreal dpr = devicePixelRatioF();
    QPixmap gearPix(qRound(iconSize * dpr), qRound(iconSize * dpr));
    gearPix.setDevicePixelRatio(dpr);
    gearPix.fill(Qt::transparent);
    {
        QPainter gp(&gearPix);
        QSvgRenderer(QString(m_gearActive ? ":/icons/gear-fill.svg" : ":/icons/gear.svg")).render(&gp);
    }
    {
        QPainter tp(&gearPix);
        tp.setCompositionMode(QPainter::CompositionMode_SourceIn);
        tp.fillRect(gearPix.rect(), gearColor);
    }
    p.drawPixmap(iconX, iconY, gearPix);

    if (m_gearActive) {
        p.fillRect(tabAreaW, h - separatorH - underlineH, k_gearWidth, underlineH,
                   palette().highlight().color());
    }
}

void NavBar::mousePressEvent(QMouseEvent *event)
{
    const int x = static_cast<int>(event->position().x());
    if (x >= width() - k_gearWidth) {
        emit settingsClicked();
        return;
    }
    const int n = m_labels.size();
    if (n == 0) return;
    const int tabAreaW = width() - k_gearWidth;
    const int col = qBound(0, x * n / tabAreaW, n - 1);
    setCurrentIndex(col);
}
