#include "DmPage.h"
#include "Database.h"
#include "QueryService.h"
#include "ScrollJumpButton.h"
#include "Theme.h"
#include "LiveEvent.h"
#include "LiveEventBus.h"

#include <functional>
#include <QDate>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMap>
#include <QPainter>
#include <QPushButton>
#include <QSvgRenderer>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QSet>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

// ---- DateSeparator ----------------------------------------------------------

class DateSeparator : public QWidget
{
public:
    explicit DateSeparator(const QString &date, QWidget *parent = nullptr)
        : QWidget(parent), m_date(date)
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = font();
        f.setPointSizeF(Theme::fontSm);
        setFont(f);
    }

    QSize sizeHint() const override
    {
        return {0, fontMetrics().height() + 20};
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int w   = width();
        const int mid = height() / 2;

        const int textW = fontMetrics().horizontalAdvance(m_date) + 16;
        const int textX = (w - textW) / 2;

        p.setPen(palette().mid().color());
        p.drawLine(16, mid, textX - 6, mid);
        p.drawLine(textX + textW + 6, mid, w - 16, mid);

        p.setPen(palette().placeholderText().color());
        p.drawText(textX, 0, textW, height(), Qt::AlignCenter, m_date);
    }

private:
    QString m_date;
};

// ---- WhisperBubble ----------------------------------------------------------

class WhisperBubble : public QWidget
{
public:
    WhisperBubble(const QString &direction, const QString &playerName,
                  const QString &guildTag,  const QString &message,
                  const QString &time,      bool showName,
                  bool showGuildTag, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_direction(direction)
        , m_playerName(playerName)
        , m_guildTag(guildTag)
        , m_message(message)
        , m_time(time)
        , m_showName(showName)
        , m_showGuildTag(showGuildTag)
    {
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int w) const override
    {
        const int bw = bubbleWidth(w);
        if (bw <= 0) return 48;
        const int tw = qMax(bw - 2 * kPad, 1);

        QFontMetrics fm(font());
        QFont boldF = font();
        boldF.setBold(true);

        int h = kPad;
        if (m_showName)
            h += QFontMetrics(boldF).height() + 4;
        h += fm.boundingRect(0, 0, tw, 10000, Qt::TextWordWrap, m_message).height();
        h += kPad;
        return h + 2 * kMargin;
    }

    QSize sizeHint() const override
    {
        return {200, heightForWidth(width() > 0 ? width() : 400)};
    }

protected:
    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        if (e->size().width() != e->oldSize().width())
            updateGeometry();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        const bool out = (m_direction == "to");
        const int  w   = width();
        const int  bw  = bubbleWidth(w);
        const int  bx  = out ? w - kMargin - bw : kMargin;
        const int  tw  = qMax(bw - 2 * kPad, 1);

        QFontMetrics fm(font());
        QFont boldF = font();
        boldF.setBold(true);

        // Compute bubble height (same logic as heightForWidth)
        int bh = kPad;
        if (m_showName) bh += QFontMetrics(boldF).height() + 4;
        const QRect msgR = fm.boundingRect(0, 0, tw, 10000, Qt::TextWordWrap, m_message);
        bh += msgR.height() + 4 + fm.height() + kPad;

        // Background
        const QColor bg = out ? palette().highlight().color()
                               : palette().alternateBase().color();
        p.setBrush(bg);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(bx, kMargin, bw, bh, kRadius, kRadius);

        const QColor fg  = out ? palette().highlightedText().color()
                                : palette().windowText().color();
        const QColor dim = out ? palette().highlightedText().color().darker(140)
                                : palette().placeholderText().color();

        int y = kMargin + kPad;

        // Name row
        if (m_showName) {
            const int nh = QFontMetrics(boldF).height();
            p.setFont(boldF);
            p.setPen(fg);
            const QString guildPrefix = (m_showGuildTag && !m_guildTag.isEmpty())
                ? QStringLiteral("<%1> ").arg(m_guildTag)
                : QString{};

            int textX = bx + kPad;
            if (out) {
                const qreal dpr = devicePixelRatioF();
                QPixmap pix(qRound(nh * dpr), qRound(nh * dpr));
                pix.setDevicePixelRatio(dpr);
                pix.fill(Qt::transparent);
                { QPainter gp(&pix); QSvgRenderer(QStringLiteral(":/icons/chevron-bar-right.svg")).render(&gp); }
                { QPainter cp(&pix);
                  cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
                  cp.fillRect(pix.rect(), fg); }
                p.drawPixmap(textX, y, pix);
                textX += nh + 4;
            }

            const QString label = guildPrefix + m_playerName;
            p.drawText(textX, y, bx + kPad + tw - textX, nh, Qt::AlignLeft | Qt::AlignVCenter, label);
            y += nh + 4;
        }

        // Message
        p.setFont(font());
        p.setPen(fg);
        p.drawText(bx + kPad, y, tw, msgR.height(), Qt::TextWordWrap, m_message);

        // Timestamp overlaid at the right of the last message line
        QFont smallF = font();
        smallF.setPointSizeF(Theme::fontSm);
        p.setFont(smallF);
        p.setPen(dim);
        const int lastLineY = y + msgR.height() - fm.height();
        p.drawText(bx + kPad, lastLineY, tw, fm.height(),
                   Qt::AlignRight | Qt::AlignVCenter, m_time);
    }

private:
    int bubbleWidth(int containerW) const
    {
        return qMin(static_cast<int>(containerW * kMaxRatio), containerW - 2 * kMargin);
    }

    static constexpr int    kMargin   = Theme::spacingSm;
    static constexpr int    kPad      = 10;
    static constexpr double kMaxRatio = 0.65;
    static constexpr int    kRadius   = 8;

    QString m_direction;
    QString m_playerName;
    QString m_guildTag;
    QString m_message;
    QString m_time;
    bool    m_showName;
    bool    m_showGuildTag;
};

// ---- Filter panel helpers ---------------------------------------------------

static constexpr int kAlphaThreshold = 100;

struct BucketDef {
    QString label;
    QString from;
    QString to;
};

// Builds the ordered list of time buckets, including year buckets derived from
// the partner data.
static QList<BucketDef> makeBuckets(const QDate &today,
                                     const QList<Database::PartnerRecord> &partners)
{
    QList<BucketDef> buckets;
    const QString todayStr = today.toString(Qt::ISODate);
    const QString yestStr  = today.addDays(-1).toString(Qt::ISODate);
    const int     dow      = today.dayOfWeek();
    const QDate   sowDate  = today.addDays(1 - dow);
    const QString sowStr   = sowDate.toString(Qt::ISODate);
    const QString slwStr   = sowDate.addDays(-7).toString(Qt::ISODate);
    const QString elwStr   = sowDate.addDays(-1).toString(Qt::ISODate);
    const QString somStr   = QDate(today.year(), today.month(), 1).toString(Qt::ISODate);

    buckets << BucketDef{"Today",      todayStr, todayStr};
    buckets << BucketDef{"Yesterday",  yestStr,  yestStr};
    buckets << BucketDef{"This Week",  sowStr,   todayStr};
    buckets << BucketDef{"Last Week",  slwStr,   elwStr};
    buckets << BucketDef{"This Month", somStr,   todayStr};

    const QLocale locale;
    for (int m = today.month() - 1; m >= 1; --m) {
        const QDate first = QDate(today.year(), m, 1);
        const QDate last  = first.addMonths(1).addDays(-1);
        buckets << BucketDef{locale.standaloneMonthName(m),
                             first.toString(Qt::ISODate),
                             last.toString(Qt::ISODate)};
    }

    QSet<int> years;
    for (const auto &p : partners)
        for (const QString &d : p.dates)
            years.insert(d.left(4).toInt());
    years.remove(today.year());
    QList<int> sortedYears = years.values();
    std::sort(sortedYears.begin(), sortedYears.end(), std::greater<int>());
    for (int y : sortedYears)
        buckets << BucketDef{QString::number(y),
                             QStringLiteral("%1-01-01").arg(y),
                             QStringLiteral("%1-12-31").arg(y)};
    return buckets;
}

static QStringList namesInBucket(const QList<Database::PartnerRecord> &partners,
                                  const BucketDef &b)
{
    QStringList names;
    for (const auto &p : partners)
        for (const QString &d : p.dates)
            if (d >= b.from && d <= b.to) { names << p.name; break; }
    names.sort(Qt::CaseInsensitive);
    return names;
}

static QStringList namesForLetter(const QStringList &names, const QString &letter)
{
    QStringList result;
    const bool isSym = (letter == "sym");
    const QChar ch   = isSym ? QChar() : letter[0].toUpper();
    for (const QString &n : names) {
        if (n.isEmpty()) continue;
        if (isSym ? !n[0].isLetter() : n[0].toUpper() == ch)
            result << n;
    }
    return result;
}

// Scrolls a QScrollArea while the mouse hovers over the button.
class ScrollArrowButton : public QPushButton
{
public:
    ScrollArrowButton(const QString &text, int dir, QScrollArea *target, QWidget *parent)
        : QPushButton(text, parent), m_dir(dir), m_target(target)
    {
        setFlat(true);
        m_timer = new QTimer(this);
        m_timer->setInterval(50);
        connect(m_timer, &QTimer::timeout, this, [this] {
            auto *sb = m_target->verticalScrollBar();
            sb->setValue(sb->value() + m_dir * 30);
        });
        auto *sb = m_target->verticalScrollBar();
        connect(sb, &QScrollBar::valueChanged, this, [this](int) { updateEnabled(); });
        connect(sb, &QScrollBar::rangeChanged, this, [this](int, int) { updateEnabled(); });
        updateEnabled();
    }

protected:
    void enterEvent(QEnterEvent *e) override { QPushButton::enterEvent(e); m_timer->start(); }
    void leaveEvent(QEvent    *e) override { QPushButton::leaveEvent(e); m_timer->stop();  }

private:
    void updateEnabled()
    {
        const auto *sb = m_target->verticalScrollBar();
        const bool canScroll = (m_dir < 0) ? sb->value() > sb->minimum()
                                           : sb->value() < sb->maximum();
        setEnabled(canScroll);
        if (!canScroll) m_timer->stop();
    }

    int          m_dir;
    QScrollArea *m_target;
    QTimer      *m_timer{};
};

// ---- DmPage ----------------------------------------------------------------

DmPage::DmPage(QWidget *parent)
    : QWidget(parent)
{
    // ---- Header row: conversation label + filter button --------------------
    m_conversationLabel = new QLabel("All Combined", this);

    m_filterBtn = new QPushButton("Filter", this);
    m_filterBtn->setFlat(true);
    m_filterBtn->setStyleSheet("QPushButton { padding: 4px 8px; }");
    connect(m_filterBtn, &QPushButton::clicked, this, [this] {
        if (m_view->currentIndex() == 1)
            m_view->setCurrentIndex(0);
        else
            openFilterPanel();
    });

    auto *headerRow = new QWidget(this);
    auto *headerBox = new QHBoxLayout(headerRow);
    headerBox->setContentsMargins(Theme::spacingSm, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
    headerBox->setSpacing(Theme::spacingSm);
    headerBox->addWidget(m_conversationLabel);
    headerBox->addStretch(1);
    headerBox->addWidget(m_filterBtn);

    auto *headerSep = new QFrame(this);
    headerSep->setFrameShape(QFrame::HLine);
    headerSep->setFrameShadow(QFrame::Sunken);

    // ---- Conversation scroll area -------------------------------------------
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_content = new QWidget;
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->addStretch(1);
    m_scroll->setWidget(m_content);

    // ---- Filter panel -------------------------------------------------------
    m_filterPanel = new QWidget(this);
    {
        // Scroll area is created first so arrow buttons can reference it.
        m_filterScroll = new QScrollArea(m_filterPanel);
        m_filterScroll->setWidgetResizable(true);
        m_filterScroll->setFrameShape(QFrame::NoFrame);
        m_filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        m_filterListWidget = new QWidget;
        m_filterListLayout = new QVBoxLayout(m_filterListWidget);
        m_filterListLayout->setContentsMargins(0, 0, 0, 0);
        m_filterListLayout->setSpacing(0);
        m_filterScroll->setWidget(m_filterListWidget);

        auto *upBtn   = new ScrollArrowButton("▲", -1, m_filterScroll, m_filterPanel);
        auto *downBtn = new ScrollArrowButton("▼",  1, m_filterScroll, m_filterPanel);

        // Header row: back button + title label
        auto *header = new QWidget(m_filterPanel);
        auto *hbox   = new QHBoxLayout(header);
        hbox->setContentsMargins(Theme::spacingXs, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
        hbox->setSpacing(Theme::spacingSm);

        m_backBtn = new QPushButton("← Back", header);
        m_backBtn->setFlat(true);
        connect(m_backBtn, &QPushButton::clicked, this, [this] {
            if (m_filterPath.isEmpty())
                m_view->setCurrentIndex(0);
            else {
                m_filterPath.removeLast();
                refreshFilterPanel();
                m_filterScroll->verticalScrollBar()->setValue(0);
            }
        });

        m_filterTitle = new QLabel(header);
        QFont f = m_filterTitle->font();
        f.setBold(true);
        m_filterTitle->setFont(f);

        hbox->addWidget(m_backBtn);
        hbox->addWidget(m_filterTitle, 1);

        auto *headerSep = new QFrame(m_filterPanel);
        headerSep->setFrameShape(QFrame::HLine);
        headerSep->setFrameShadow(QFrame::Sunken);

        auto *vbox = new QVBoxLayout(m_filterPanel);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        vbox->addWidget(header);
        vbox->addWidget(headerSep);
        vbox->addWidget(upBtn);
        vbox->addWidget(m_filterScroll, 1);
        vbox->addWidget(downBtn);
    }

    // ---- Stacked view: page 0 = conversations, page 1 = filter panel --------
    m_view = new QStackedWidget(this);
    m_view->addWidget(m_scroll);
    m_view->addWidget(m_filterPanel);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, Theme::spacingXs, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(headerRow);
    vbox->addWidget(headerSep);
    vbox->addWidget(m_view, 1);

    // ---- Live rebuild timer -------------------------------------------------
    m_liveRebuildTimer = new QTimer(this);
    m_liveRebuildTimer->setSingleShot(true);
    m_liveRebuildTimer->setInterval(300);
    connect(m_liveRebuildTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()) { m_dirty = true; return; }
        rebuild(); // scroll-to-bottom handled in applyWhispers via m_liveRebuildScrollToBottom
    });

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &DmPage::onLiveWhisper);

    m_scrollDownBtn = new ScrollJumpButton(this);
    m_scrollDownBtn->hide();
    m_scrollDownBtn->raise();
    connect(m_scrollDownBtn, &QPushButton::clicked, this, &DmPage::jumpToLiveView);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { updateScrollDownBtn(); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int, int) { updateScrollDownBtn(); });
    connect(m_view, &QStackedWidget::currentChanged,
            this, [this](int) { updateScrollDownBtn(); });
}

void DmPage::setQueryService(QueryService *qs)
{
    m_queryService = qs;
}

void DmPage::setShowGuildTags(bool show)
{
    if (m_showGuildTags == show) return;
    m_showGuildTags = show;
    if (isVisible() && m_queryService) rebuild();
    else m_dirty = true;
}

void DmPage::reload()
{
    m_dirty        = false;
    m_limit        = kInitialLimit;
    m_windowOffset = 0;
    rebuild();
    QTimer::singleShot(0, this, &DmPage::scrollToBottom);
}

void DmPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_queryService)
        reload();
}

void DmPage::onLiveWhisper(const LiveEvent &event, bool bulk)
{
    if (event.type != LiveEventType::Whisper) return;

    if (!isVisible()) {
        m_dirty = true;
        return;
    }

    if (bulk) {
        m_liveRebuildTimer->stop();
        rebuild();
        return;
    }

    if (!m_liveRebuildTimer->isActive())
        m_liveRebuildScrollToBottom = m_scroll->verticalScrollBar()->value()
                                      >= m_scroll->verticalScrollBar()->maximum() - 4;

    m_liveRebuildTimer->start();
}

void DmPage::onPlayerSelected(const QString &name)
{
    if (m_filterPlayer == name) return;
    m_filterPlayer = name;
    if (name.isEmpty())
        m_conversationLabel->setText("All Combined");
    else
        m_conversationLabel->setText(QStringLiteral("Conversation with %1").arg(name));
    m_limit        = kInitialLimit;
    m_windowOffset = 0;
    rebuild();
    QTimer::singleShot(0, this, &DmPage::scrollToBottom);
}

void DmPage::openFilterPanel()
{
    if (!m_queryService) return;
    m_queryService->fetchWhisperPartnersWithDates(
        [this](QList<Database::PartnerRecord> partners) {
            m_cachedPartners = std::move(partners);
            m_filterPath.clear();
            refreshFilterPanel();
            m_filterScroll->verticalScrollBar()->setValue(0);
            m_view->setCurrentIndex(1);
        });
}

void DmPage::refreshFilterPanel()
{
    // Replace the list widget so Qt cleans up old rows automatically.
    auto *listWidget = new QWidget;
    auto *listLayout = new QVBoxLayout(listWidget);
    listLayout->setContentsMargins(0, 0, 0, 0);
    listLayout->setSpacing(0);

    const auto addRow = [&](const QString &text, bool drill, std::function<void()> fn) {
        auto *btn = new QPushButton(text + (drill ? "  ›" : ""), listWidget);
        btn->setFlat(true);
        btn->setMinimumHeight(40);
        btn->setStyleSheet("QPushButton { text-align: left; padding: 4px 12px; }");
        QObject::connect(btn, &QPushButton::clicked, this, std::move(fn));
        listLayout->addWidget(btn);
    };

    const auto addSep = [&]() {
        auto *line = new QFrame(listWidget);
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        listLayout->addWidget(line);
    };

    // ---- Root level ---------------------------------------------------------
    if (m_filterPath.isEmpty()) {
        m_filterTitle->setText("Filter by person");

        addRow("Reset filter — Show all conversations combined", false, [this] { filterLeafSelected({}); });
        addSep();

        const QList<BucketDef> buckets = makeBuckets(QDate::currentDate(), m_cachedPartners);
        for (const BucketDef &b : buckets) {
            const QStringList names = namesInBucket(m_cachedPartners, b);
            if (names.isEmpty()) continue;
            const QString label = QStringLiteral("%1  (%2)").arg(b.label).arg(names.size());
            addRow(label, true, [this, blabel = b.label] {
                m_filterPath.append(blabel);
                refreshFilterPanel();
                m_filterScroll->verticalScrollBar()->setValue(0);
            });
        }

        addSep();

        const int total = m_cachedPartners.size();
        addRow(QStringLiteral("All  (%1)").arg(total), true, [this] {
            m_filterPath.append("All");
            refreshFilterPanel();
            m_filterScroll->verticalScrollBar()->setValue(0);
        });
    }
    // ---- Bucket or "All" level ----------------------------------------------
    else if (m_filterPath.size() == 1) {
        const QString &step = m_filterPath[0];
        m_filterTitle->setText(step);

        QStringList names;
        if (step == "All") {
            for (const auto &p : m_cachedPartners) names << p.name;
            names.sort(Qt::CaseInsensitive);
        } else {
            const QList<BucketDef> buckets = makeBuckets(QDate::currentDate(), m_cachedPartners);
            for (const BucketDef &b : buckets) {
                if (b.label == step) { names = namesInBucket(m_cachedPartners, b); break; }
            }
        }

        if (step != "All" && names.size() <= kAlphaThreshold) {
            for (const QString &name : names)
                addRow(name, false, [this, name] { filterLeafSelected(name); });
        } else {
            QStringList syms;
            QMap<QChar, int> letterCounts;
            for (const QString &name : names) {
                if (name.isEmpty()) continue;
                if (!name[0].isLetter()) syms << name;
                else letterCounts[name[0].toUpper()]++;
            }

            if (!syms.isEmpty()) {
                addRow(QStringLiteral("sym  (%1)").arg(syms.size()), true, [this] {
                    m_filterPath.append("sym");
                    refreshFilterPanel();
                    m_filterScroll->verticalScrollBar()->setValue(0);
                });
            }

            for (char c = 'A'; c <= 'Z'; ++c) {
                const QChar letter(c);
                const int count = letterCounts.value(letter, 0);
                if (count == 0) continue;
                addRow(QStringLiteral("%1  (%2)").arg(letter).arg(count), true,
                       [this, letter] {
                           m_filterPath.append(QString(letter));
                           refreshFilterPanel();
                           m_filterScroll->verticalScrollBar()->setValue(0);
                       });
            }
        }
    }
    // ---- Letter level -------------------------------------------------------
    else if (m_filterPath.size() == 2) {
        const QString &bucket = m_filterPath[0];
        const QString &letter = m_filterPath[1];
        m_filterTitle->setText(QStringLiteral("%1  ·  %2").arg(bucket, letter));

        QStringList allNames;
        if (bucket == "All") {
            for (const auto &p : m_cachedPartners) allNames << p.name;
        } else {
            const QList<BucketDef> buckets = makeBuckets(QDate::currentDate(), m_cachedPartners);
            for (const BucketDef &b : buckets) {
                if (b.label == bucket) { allNames = namesInBucket(m_cachedPartners, b); break; }
            }
        }
        allNames.sort(Qt::CaseInsensitive);

        for (const QString &name : namesForLetter(allNames, letter))
            addRow(name, false, [this, name] { filterLeafSelected(name); });
    }

    listLayout->addStretch(1);

    m_filterListWidget = listWidget;
    m_filterListLayout = listLayout;
    m_filterScroll->setWidget(listWidget);
}

void DmPage::filterLeafSelected(const QString &name)
{
    m_view->setCurrentIndex(0);
    onPlayerSelected(name);
}

void DmPage::rebuild()
{
    if (!m_queryService) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    // Capture scroll position for live rebuild restoration (not at bottom).
    if (m_scrollRestoreMax < 0 && !m_liveRebuildScrollToBottom) {
        const auto *sb = m_scroll->verticalScrollBar();
        if (sb->maximum() > 0) {
            m_scrollRestoreMax   = sb->maximum();
            m_scrollRestoreValue = sb->value();
        }
    }

    m_queryService->fetchWhispers(m_filterPlayer, m_limit, m_windowOffset,
        [this](QList<Database::WhisperRecord> whispers) {
            m_rebuildInFlight = false;
            applyWhispers(whispers);
            if (m_dirty) rebuild();
        });
}

void DmPage::applyWhispers(const QList<Database::WhisperRecord> &whispers)
{
    const bool showNames = m_filterPlayer.isEmpty();

    auto *content       = new QWidget;
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(Theme::spacingSm, Theme::spacingSm,
                                      Theme::spacingSm, Theme::spacingSm);
    contentLayout->setSpacing(2);
    contentLayout->addStretch(1);

    // "Load previous 50" at the top.
    if (whispers.size() == m_limit) {
        auto *btn = new QPushButton(
            QStringLiteral("Load previous %1 messages").arg(kPageStep), content);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this] {
            m_scrollRestoreMax   = m_scroll->verticalScrollBar()->maximum();
            m_scrollRestoreValue = m_scroll->verticalScrollBar()->value();
            if (m_limit < kMaxWindow)
                m_limit += kPageStep;
            else
                m_windowOffset += kPageStep;
            rebuild();
        });
        contentLayout->addWidget(btn);
    }

    QString lastDate;
    for (const auto &w : whispers) {
        const QString date = w.occurredAt.left(10);
        if (date != lastDate) {
            lastDate = date;
            contentLayout->addWidget(new DateSeparator(date, content));
        }
        const QString time = w.occurredAt.mid(11, 5);
        contentLayout->addWidget(
            new WhisperBubble(w.direction, w.playerName, w.guildTag, w.message,
                              time, showNames, m_showGuildTags, content));
    }

    // "Load next 50" at the bottom — only when window is slid away from newest.
    if (m_windowOffset > 0) {
        auto *btn = new QPushButton(
            QStringLiteral("Load next %1 messages").arg(kPageStep), content);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this] {
            m_scrollRestoreMax = -1;   // scroll to bottom of new content
            m_windowOffset = qMax(0, m_windowOffset - kPageStep);
            rebuild();
        });
        contentLayout->addWidget(btn);
    }

    delete m_content;
    m_content       = content;
    m_contentLayout = contentLayout;
    m_scroll->setWidget(m_content);

    if (m_liveRebuildScrollToBottom) {
        m_liveRebuildScrollToBottom = false;
        m_scrollRestoreMax = -1;
        QTimer::singleShot(0, this, &DmPage::scrollToBottom);
    } else if (m_scrollRestoreMax >= 0) {
        const int prevMax   = m_scrollRestoreMax;
        const int prevValue = m_scrollRestoreValue;
        m_scrollRestoreMax = -1;
        QTimer::singleShot(0, this, [this, prevMax, prevValue] {
            const int delta = m_scroll->verticalScrollBar()->maximum() - prevMax;
            m_scroll->verticalScrollBar()->setValue(prevValue + delta);
        });
    } else {
        QTimer::singleShot(0, this, &DmPage::scrollToBottom);
    }
}

void DmPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_scrollDownBtn->move(rect().right()  - m_scrollDownBtn->width()  - Theme::spacing3xl,
                          rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}

void DmPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    m_scrollDownBtn->setVisible(m_view->currentIndex() == 0 && !atBottom);
}

void DmPage::scrollToBottom()
{
    m_scroll->verticalScrollBar()->setValue(
        m_scroll->verticalScrollBar()->maximum());
}

void DmPage::jumpToLiveView()
{
    if (m_windowOffset == 0) {
        scrollToBottom();
        return;
    }
    m_windowOffset = 0;
    m_limit        = kInitialLimit;
    m_scrollRestoreMax = -1;
    rebuild();
}
