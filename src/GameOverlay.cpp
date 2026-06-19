#include "GameOverlay.h"
#include "Theme.h"

#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QPen>
#include <QResizeEvent>
#include <QScreen>

namespace {

class InfoPanel : public QWidget
{
public:
    explicit InfoPanel(const QString &text, QWidget *parent = nullptr)
        : QWidget(parent), m_text(text)
    {
        setContentsMargins(12, 6, 9, 6);
        QFont f = font();
        f.setPointSize(13);
        f.setItalic(true);
        f.setBold(true);
        f.setStyleHint(QFont::Serif);
        f.setFamilies({"Palatino Linotype", "Book Antiqua", "Palatino", "serif"});
        f.setLetterSpacing(QFont::AbsoluteSpacing, 3.0);
        setFont(f);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
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
        p.setBrush(QColor(15, 10, 2, 210));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

        p.setPen(Theme::accent);
        p.setFont(font());
        p.drawText(contentsRect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
};

} // namespace

GameOverlay::GameOverlay(QWidget *parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool)
{
    setAttribute(Qt::WA_TranslucentBackground);

    m_infoPanel = new InfoPanel(QStringLiteral("l2p"), this);
    m_infoPanel->adjustSize();
}

void GameOverlay::updateGameRect(const QRect &physicalRect)
{
#ifdef Q_OS_WIN
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
    if (!m_infoPanel)
        return;
    m_infoPanel->adjustSize();
    const int margin = 10;
    m_infoPanel->move(margin, margin);
    updateMask();
}

void GameOverlay::updateMask()
{
    // Only the panel regions intercept mouse events; everything else is click-through.
    QRegion mask;
    if (m_infoPanel)
        mask |= QRegion(m_infoPanel->geometry());
    setMask(mask);
}
