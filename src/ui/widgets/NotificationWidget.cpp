#include "ui/widgets/NotificationWidget.h"
#include "ui/Theme.h"

#include <QDesktopServices>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QRegularExpression>
#include <QUrl>
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

class SourceIconWidget : public QWidget
{
public:
    explicit SourceIconWidget(const QString &svgPath, const QColor &color,
                              int size = 14, QWidget *parent = nullptr)
        : QWidget(parent), m_svgPath(svgPath), m_color(color)
    {
        setFixedSize(size, size);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }

    void setUrl(const QString &url)
    {
        m_url = url;
        setCursor(url.isEmpty() ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }

protected:
    void mousePressEvent(QMouseEvent *e) override
    {
        if (!m_url.isEmpty())
            QDesktopServices::openUrl(QUrl(m_url));
        QWidget::mousePressEvent(e);
    }

    void paintEvent(QPaintEvent *) override
    {
        const QPixmap pix = Theme::renderSvgIcon(m_svgPath, m_color, size(), devicePixelRatioF());
        QPainter p(this);
        p.drawPixmap(0, 0, pix);
    }

private:
    QString m_svgPath;
    QColor  m_color;
    QString m_url;
};

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
    tsLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    m_outerLayout = new QVBoxLayout(this);
    m_outerLayout->setContentsMargins(10, Theme::spacingSm, 10, Theme::spacingSm);
    m_outerLayout->setSpacing(Theme::spacingSm);

    m_topRow = new QHBoxLayout;
    m_topRow->setSpacing(Theme::spacingSm);

    if (!title.isEmpty()) {
        auto *left = new QWidget(this);
        m_leftLayout = new QHBoxLayout(left);
        m_leftLayout->setContentsMargins(0, 0, 0, 0);
        m_leftLayout->setSpacing(6);

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
        titleLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_leftLayout->addWidget(titleLabel, 0, Qt::AlignVCenter);

        if (!tag.isEmpty()) {
            auto *tagLabel = new TagLabel(tag, style.accentColor, left);
            QFont tagFont = tagLabel->font();
            tagFont.setPointSizeF(Theme::fontXs);
            tagLabel->setFont(tagFont);
            m_leftLayout->addWidget(tagLabel, 0, Qt::AlignVCenter);
        }

        m_headerSuffixLabel = new QLabel(left);
        {
            QPalette pal = m_headerSuffixLabel->palette();
            pal.setColor(QPalette::WindowText, style.bodyColor);
            m_headerSuffixLabel->setPalette(pal);
            QFont f = m_headerSuffixLabel->font();
            f.setPointSizeF(Theme::fontBase);
            m_headerSuffixLabel->setFont(f);
        }
        m_headerSuffixLabel->setVisible(false);
        m_leftLayout->addWidget(m_headerSuffixLabel, 0, Qt::AlignVCenter);

        m_leftLayout->addStretch();
        m_topRow->addWidget(left, 1);
    } else {
        m_topRow->addWidget(buildSegmentedRow(message, style.textColor, this), 1);
    }

    m_expandIndicator = new QLabel(QString(QChar(0x25B8)), this); // ▸
    {
        QPalette pal = m_expandIndicator->palette();
        pal.setColor(QPalette::WindowText, style.timestampColor);
        m_expandIndicator->setPalette(pal);
        QFont f = m_expandIndicator->font();
        f.setPointSizeF(Theme::fontXl);
        m_expandIndicator->setFont(f);
    }
    m_expandIndicator->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_expandIndicator->setVisible(false);
    m_topRow->addWidget(m_expandIndicator, 0);

    m_sourceIcon = new SourceIconWidget(QStringLiteral(":/icons/info-circle.svg"), style.timestampColor, 14, this);
    m_sourceIcon->setVisible(false);
    m_topRow->addWidget(m_sourceIcon, 0, Qt::AlignVCenter);

    m_topRow->addWidget(tsLabel, 0);

    m_outerLayout->addLayout(m_topRow);

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

void NotificationWidget::setHeaderSuffix(const QString &text)
{
    if (!m_headerSuffixLabel) return;
    m_headerSuffixLabel->setText(text);
    m_headerSuffixLabel->setVisible(!text.isEmpty());
}

void NotificationWidget::setSource(const DocSource &source)
{
    if (!m_sourceIcon) return;
    m_sourceIcon->setToolTip("Source: " + source.label);
    static_cast<SourceIconWidget *>(m_sourceIcon)->setUrl(source.url);
    m_sourceIcon->setVisible(!source.label.isEmpty());
}

void NotificationWidget::setDetailRows(const QList<QPair<QString, QString>> &rows)
{
    if (rows.isEmpty()) return;

    if (m_separator) {
        m_outerLayout->removeWidget(m_separator);
        delete m_separator;
        m_separator = nullptr;
    }
    if (m_detailWidget) {
        m_outerLayout->removeWidget(m_detailWidget);
        delete m_detailWidget;
        m_detailWidget = nullptr;
    }

    auto *sep = new QWidget(this);
    sep->setFixedHeight(1);
    sep->setAutoFillBackground(true);
    {
        QPalette pal = sep->palette();
        pal.setColor(QPalette::Window, m_style.border);
        sep->setPalette(pal);
    }
    sep->setVisible(false);
    m_outerLayout->addWidget(sep);
    m_separator = sep;

    auto *detail = new QWidget(this);
    auto *grid = new QGridLayout(detail);
    grid->setContentsMargins(0, 2, 0, 2);
    grid->setHorizontalSpacing(Theme::spacingSm);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(1, 1);

    int r = 0;
    for (const auto &[key, value] : rows) {
        if (value.isEmpty()) continue;

        auto *keyLabel = new QLabel(key, detail);
        {
            QFont f = keyLabel->font();
            f.setPointSizeF(Theme::fontSm);
            keyLabel->setFont(f);
            QPalette pal = keyLabel->palette();
            pal.setColor(QPalette::WindowText, m_style.timestampColor);
            keyLabel->setPalette(pal);
        }
        keyLabel->setAlignment(Qt::AlignRight | Qt::AlignTop);

        auto *valLabel = new QLabel(value, detail);
        {
            QFont f = valLabel->font();
            f.setPointSizeF(Theme::fontSm);
            valLabel->setFont(f);
            QPalette pal = valLabel->palette();
            pal.setColor(QPalette::WindowText, m_style.bodyColor);
            valLabel->setPalette(pal);
        }
        valLabel->setWordWrap(true);

        grid->addWidget(keyLabel, r, 0, Qt::AlignTop | Qt::AlignRight);
        grid->addWidget(valLabel, r, 1, Qt::AlignTop);
        ++r;
    }

    if (r == 0) {
        m_outerLayout->removeWidget(sep);
        delete sep;
        m_separator = nullptr;
        delete detail;
        return;
    }

    detail->setVisible(false);
    m_outerLayout->addWidget(detail);
    m_detailWidget = detail;

    if (m_expandIndicator)
        m_expandIndicator->setVisible(true);
    setCursor(Qt::PointingHandCursor);
}

void NotificationWidget::mousePressEvent(QMouseEvent *e)
{
    if (m_detailWidget) {
        m_expanded = !m_expanded;
        if (m_separator)       m_separator->setVisible(m_expanded);
        m_detailWidget->setVisible(m_expanded);
        if (m_expandIndicator)
            m_expandIndicator->setText(m_expanded
                ? QString(QChar(0x25BE))   // ▾
                : QString(QChar(0x25B8))); // ▸
        if (m_outerLayout) m_outerLayout->activate();
        updateGeometry();
        emit expanded(m_expanded);
    }
    QFrame::mousePressEvent(e);
}

void NotificationWidget::setAreaName(const QString &name)
{
    if (!m_leftLayout || name.isEmpty()) return;
    auto *lbl = new QLabel(name, this);
    {
        QPalette pal = lbl->palette();
        pal.setColor(QPalette::WindowText, QColor(215, 215, 215));
        lbl->setPalette(pal);
        QFont f = lbl->font();
        f.setPointSizeF(Theme::fontXl);
        f.setBold(true);
        lbl->setFont(f);
    }
    lbl->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    const int idx = m_leftLayout->indexOf(m_headerSuffixLabel);
    m_leftLayout->insertWidget(idx, lbl, 0, Qt::AlignVCenter);
}

void NotificationWidget::setLeadingIcon(const QString &svgPath, const QColor &color, int size)
{
    if (!m_leftLayout || svgPath.isEmpty()) return;
    auto *icon = new SourceIconWidget(svgPath, color, size, this);
    m_leftLayout->insertWidget(0, icon, 0, Qt::AlignVCenter);
    m_leadingIcon = icon;
}

void NotificationWidget::appendTopRowTag(const QString &tag)
{
    if (!m_topRow || tag.isEmpty()) return;
    auto *tagLabel = new TagLabel(tag, m_style.accentColor, this);
    QFont f = tagLabel->font();
    f.setPointSizeF(Theme::fontXs);
    tagLabel->setFont(f);
    const int idx = m_topRow->indexOf(m_sourceIcon);
    m_topRow->insertWidget(idx, tagLabel, 0, Qt::AlignVCenter);
}

void NotificationWidget::setActionWidget(QWidget *w)
{
    if (!m_topRow || !w) return;
    const int idx = m_topRow->indexOf(m_sourceIcon);
    m_topRow->insertWidget(idx >= 0 ? idx : m_topRow->count(), w, 0, Qt::AlignVCenter);
}
