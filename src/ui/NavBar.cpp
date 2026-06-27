#include "ui/NavBar.h"
#include "ui/Theme.h"
#include "core/PerfProbe.h"

#include <QMouseEvent>
#include <QPainter>
#include <QRect>

NavBar::NavBar(const QStringList &labels, QWidget *parent)
    : QWidget(parent), m_labels(labels)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void NavBar::setCurrentIndex(int index)
{
    if (index < 0 || index >= m_labels.size())
        return;
    if (index == m_current && !m_gearActive && !m_searchActive)
        return;
    m_current = index;
    m_gearActive = false;
    m_searchActive = false;
    update();
    emit currentChanged(index);
}

void NavBar::setGearActive(bool active)
{
    if (m_gearActive == active)
        return;
    m_gearActive = active;
    if (active)
        m_searchActive = false;
    update();
}

void NavBar::setSearchActive(bool active)
{
    if (m_searchActive == active)
        return;
    m_searchActive = active;
    if (active)
        m_gearActive = false;
    update();
}

QRect NavBar::tabRect(int i) const
{
    const int n = m_labels.size();
    if (i < 0 || i >= n) return {};
    const int w        = width();
    const int tabAreaX = k_listWidth;
    const int tabAreaW = w - k_listWidth - k_gearWidth;
    const int colW     = tabAreaW / n;
    const int x        = tabAreaX + i * colW;
    const int cw       = (i == n - 1) ? (tabAreaX + tabAreaW - x) : colW;
    return QRect(x, 0, cw, height());
}

QSize NavBar::sizeHint() const
{
    QFont f = font();
    f.setPointSizeF(Theme::font2xl);
    return {0, QFontMetrics(f).height() + 28};
}

void NavBar::paintEvent(QPaintEvent *)
{
    PerfProbe::instance().onNavBarFirstPaint();

    QPainter p(this);

    const int w = width();
    const int h = height();
    const int n = m_labels.size();
    if (n == 0) return;

    const int tabAreaX = k_listWidth;
    const int tabAreaW = w - k_listWidth - k_gearWidth;
    const int colW     = tabAreaW / n;
    const int separatorH = 3;
    const int underlineH = 8;
    const int cellH      = h - separatorH;

    p.fillRect(rect(), palette().window());

    // Bottom separator
    p.fillRect(0, h - separatorH, w, separatorH, palette().mid().color());

    QFont f = font();
    f.setPointSizeF(Theme::font2xl);

    for (int i = 0; i < n; ++i) {
        const int x  = tabAreaX + i * colW;
        const int cw = (i == n - 1) ? tabAreaX + tabAreaW - x : colW;
        const QRect cell(x, 0, cw, cellH);
        const bool active = (i == m_current) && !m_gearActive && !m_searchActive;

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

    const qreal dpr      = devicePixelRatioF();
    const int   iconSize = QFontMetrics(f).height() - 6;
    const int   iconY    = (cellH - iconSize) / 2;

    // List (hamburger) icon — far left
    {
        const QColor col = m_searchActive ? palette().windowText().color()
                                        : palette().placeholderText().color();
        const int iconX = (k_listWidth - iconSize) / 2;
        const QPixmap pix = Theme::renderSvgIcon(
            QStringLiteral(":/icons/search.svg"), col, {iconSize, iconSize}, dpr);
        p.drawPixmap(QRect(iconX, iconY, iconSize, iconSize), pix,
                     QRect(0, 0, pix.width(), pix.height()));
        if (m_searchActive)
            p.fillRect(0, h - separatorH - underlineH, k_listWidth, underlineH,
                       palette().highlight().color());
    }

    // Gear icon — far right
    {
        const QColor col = m_gearActive ? palette().windowText().color()
                                        : palette().placeholderText().color();
        const int iconX = w - k_gearWidth + (k_gearWidth - iconSize) / 2;
        const QString svg = m_gearActive ? QStringLiteral(":/icons/gear-fill.svg")
                                         : QStringLiteral(":/icons/gear.svg");
        const QPixmap pix = Theme::renderSvgIcon(svg, col, {iconSize, iconSize}, dpr);
        p.drawPixmap(QRect(iconX, iconY, iconSize, iconSize), pix,
                     QRect(0, 0, pix.width(), pix.height()));
        if (m_gearActive)
            p.fillRect(w - k_gearWidth, h - separatorH - underlineH, k_gearWidth, underlineH,
                       palette().highlight().color());
    }
}

void NavBar::mousePressEvent(QMouseEvent *event)
{
    const int x = static_cast<int>(event->position().x());

    // Compute which nav tab was hit (before routing) so PerfProbe can track it.
    if (PerfProbe::instance().enabled() && x >= k_listWidth && x < width() - k_gearWidth) {
        const int n = m_labels.size();
        if (n > 0) {
            const int tabAreaW = width() - k_listWidth - k_gearWidth;
            const int col = qBound(0, (x - k_listWidth) * n / tabAreaW, n - 1);
            PerfProbe::instance().onNavBarMousePress(col);
        }
    }

    if (x < k_listWidth) {
        emit searchClicked();
        return;
    }
    if (x >= width() - k_gearWidth) {
        emit settingsClicked();
        return;
    }
    const int n = m_labels.size();
    if (n == 0) return;
    const int tabAreaW = width() - k_listWidth - k_gearWidth;
    const int col = qBound(0, (x - k_listWidth) * n / tabAreaW, n - 1);
    if (col == m_current && !m_gearActive && !m_searchActive) {
        emit tabReselected(col);
        return;
    }
    setCurrentIndex(col);
}
