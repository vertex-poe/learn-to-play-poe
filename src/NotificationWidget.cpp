#include "NotificationWidget.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QVBoxLayout>

namespace {

// Draws its text inside a rounded badge outline using QPainter.
// The border is a semi-transparent white, matching the "monospace key" look.
class BadgeLabel : public QWidget
{
public:
    explicit BadgeLabel(const QString &text, const QColor &textColor,
                        QWidget *parent = nullptr)
        : QWidget(parent), m_text(text), m_textColor(textColor)
    {
        QFont f = font();
        f.setFamily("monospace");
        setFont(f);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setContentsMargins(Theme::spacingXs, 1, Theme::spacingXs, 1);
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
        p.setPen(QPen(QColor(255, 255, 255, 77), 1));  // rgba(255,255,255,0.3)
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);
        p.setPen(m_textColor);
        p.setFont(font());
        p.drawText(contentsRect(), Qt::AlignCenter, m_text);
    }

private:
    QString m_text;
    QColor  m_textColor;
};

// Draws its text inside a pill (fully-rounded rectangle) using QPainter so
// the shape is always correct regardless of Qt's stylesheet border-radius quirks.
class TagLabel : public QLabel
{
public:
    explicit TagLabel(const QString &text, const QColor &textColor, QWidget *parent = nullptr)
        : QLabel(text, parent), m_textColor(textColor)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        setContentsMargins(5, 0, 5, 0); // horizontal inset only, for legibility
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(0, 1);

        const int r = height() / 2;
        const QRect border = rect().adjusted(0, 0, -1, -1);

        QColor borderCol = m_textColor; borderCol.setAlpha(160);
        QColor fillCol   = m_textColor; fillCol.setAlpha(22);
        p.setPen(QPen(borderCol, 1));
        p.setBrush(fillCol);
        p.drawRoundedRect(border, r, r);

        const QFontMetrics fm(font());
        const int baseline = (height() + fm.ascent() - fm.descent()) / 2 - 1;
        p.setPen(m_textColor);
        p.drawText(contentsMargins().left(), baseline, text());
    }

private:
    QColor m_textColor;
};

QWidget *buildSegmentedRow(const QString &text, const QColor &color, QWidget *parent)
{
    auto *row    = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(Theme::spacingXs);

    auto applyLabelStyle = [&](QLabel *lbl) {
        QPalette pal = lbl->palette();
        pal.setColor(QPalette::WindowText, color);
        lbl->setPalette(pal);
    };

    static const QRegularExpression re("\\{([^}]*)\\}");
    int lastEnd = 0;
    auto it = re.globalMatch(text);
    while (it.hasNext()) {
        const auto match = it.next();
        if (match.capturedStart() > lastEnd) {
            auto *lbl = new QLabel(text.mid(lastEnd, match.capturedStart() - lastEnd), row);
            applyLabelStyle(lbl);
            layout->addWidget(lbl);
        }
        auto *badge = new BadgeLabel(match.captured(1), color, row);
        layout->addWidget(badge);
        lastEnd = match.capturedEnd();
    }
    if (lastEnd < text.length()) {
        auto *lbl = new QLabel(text.mid(lastEnd), row);
        applyLabelStyle(lbl);
        layout->addWidget(lbl);
    }
    layout->addStretch();
    return row;
}

} // namespace

void NotificationWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(m_style.border, m_style.borderWidth));
    p.setBrush(m_style.background);
    p.drawRoundedRect(rect().adjusted(0, 0, -1, -1),
                      m_style.borderRadius, m_style.borderRadius);
}

NotificationWidget::NotificationWidget(const QString &title, const QString &tag,
                                       const QString &message, const QString &timestamp,
                                       const NotificationStyle &style, QWidget *parent)
    : QFrame(parent)
    , m_style(style)
{
    setFrameShape(QFrame::NoFrame);

    auto *tsLabel = new QLabel(timestamp, this);
    {
        QPalette pal = tsLabel->palette();
        pal.setColor(QPalette::WindowText, style.timestampColor);
        tsLabel->setPalette(pal);
    }
    tsLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    tsLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(10, Theme::spacingSm, 10, Theme::spacingSm);
    m_outerLayout->setSpacing(Theme::spacingSm);

    auto *topRow = new QHBoxLayout;
    topRow->setSpacing(Theme::spacingSm);

    if (!title.isEmpty()) {
        auto *left       = new QWidget(this);
        auto *leftLayout = new QHBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        leftLayout->setSpacing(6);

        auto *titleLabel = new QLabel(title, left);
        {
            QPalette pal = titleLabel->palette();
            pal.setColor(QPalette::WindowText, style.accentColor);
            titleLabel->setPalette(pal);
            QFont f = titleLabel->font();
            f.setPointSizeF(Theme::fontXl);
            f.setBold(true);
            titleLabel->setFont(f);
        }
        titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        leftLayout->addWidget(titleLabel, 0, Qt::AlignTop);

        if (!tag.isEmpty()) {
            auto *tagLabel = new TagLabel(tag, style.accentColor, left);
            QFont tagFont = tagLabel->font();
            tagFont.setPointSizeF(Theme::fontXs);
            tagLabel->setFont(tagFont);
            leftLayout->addWidget(tagLabel, 0, Qt::AlignTop);
        }

        leftLayout->addStretch();
        topRow->addWidget(left, 1);
    } else {
        topRow->addWidget(buildSegmentedRow(message, style.textColor, this), 1);
    }

    topRow->addWidget(tsLabel, 0);
    m_outerLayout->addLayout(topRow);

    if (!title.isEmpty() && !message.isEmpty()) {
        m_bodyWidget = buildSegmentedRow(message, style.bodyColor, this);
        m_outerLayout->addWidget(m_bodyWidget);
    }
}

void NotificationWidget::setMessage(const QString &text)
{
    if (!m_outerLayout) return;
    if (m_bodyWidget) {
        m_outerLayout->removeWidget(m_bodyWidget);
        delete m_bodyWidget;
        m_bodyWidget = nullptr;
    }
    if (!text.isEmpty()) {
        m_bodyWidget = buildSegmentedRow(text, m_style.bodyColor, this);
        m_outerLayout->addWidget(m_bodyWidget);
    }
    updateGeometry();
}
