#include "ChatPage.h"
#include "Database.h"
#include "QueryService.h"
#include "ScrollJumpButton.h"
#include "Theme.h"
#include "LiveEvent.h"
#include "LiveEventBus.h"

#include <functional>
#include <QCheckBox>
#include <QDate>
#include <QEnterEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
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

// ---- Channel colour / badge helpers -----------------------------------------

static QColor channelColor(const QString &ch)
{
    if (ch == "!")     return { 88, 148,  88};  // Local   – desaturated green
    if (ch == "#")     return {165,  78,  78};  // Global  – desaturated red
    if (ch == "$")     return {182, 112,  65};  // Trade   – desaturated orange
    if (ch == "%")     return { 78, 115, 170};  // Party   – desaturated blue
    if (ch == "&")     return {115, 118, 120};  // Guild   – desaturated gray
    if (ch == "@from") return {175, 105, 205};  // DM in   – lighter purple
    if (ch == "@to")   return { 90,  58, 120};  // DM out  – darker purple
    return                    {120, 120, 120};  // unknown – gray
}

static QString channelBadge(const QString &ch)
{
    if (ch == "!")     return "L";
    if (ch == "#")     return "#";
    if (ch == "$")     return "$";
    if (ch == "%")     return "%";
    if (ch == "&")     return "&";
    if (ch == "@from") return "❮";
    if (ch == "@to")   return "❯";
    return "?";
}

// ---- DateSeparator ----------------------------------------------------------

class DateSeparator : public QWidget
{
public:
    explicit DateSeparator(const QString &date, QWidget *parent = nullptr)
        : QWidget(parent), m_date(date)
    {
        setObjectName("separator");
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        QFont f = font();
        f.setPointSizeF(Theme::fontSm);
        setFont(f);
    }

    QSize sizeHint() const override { return {0, fontMetrics().height() + 20}; }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const int w = width(), mid = height() / 2;
        const int tw = fontMetrics().horizontalAdvance(m_date) + 16;
        const int tx = (w - tw) / 2;
        p.setPen(palette().mid().color());
        p.drawLine(16, mid, tx - 6, mid);
        p.drawLine(tx + tw + 6, mid, w - 16, mid);
        p.setPen(palette().placeholderText().color());
        p.drawText(tx, 0, tw, height(), Qt::AlignCenter, m_date);
    }

private:
    QString m_date;
};

// ---- ChatRow ----------------------------------------------------------------

class ChatRow : public QWidget
{
public:
    ChatRow(const QString &channel, const QString &player, const QString &guild,
            const QString &message,  const QString &timeLabel,
            QWidget *parent = nullptr)
        : QWidget(parent)
        , m_channel(channel), m_player(player), m_guild(guild)
        , m_message(message), m_time(timeLabel)
    {
        QSizePolicy sp(QSizePolicy::Expanding, QSizePolicy::Preferred);
        sp.setHeightForWidth(true);
        setSizePolicy(sp);
    }

    bool hasHeightForWidth() const override { return true; }

    int heightForWidth(int w) const override
    {
        const int textX = kBarW + kBadgePadL + kBadgeW + kBadgeTextPad;
        const int textW = w - textX - kPadR;
        if (textW <= 0) return 40;
        QFont boldF = font(); boldF.setBold(true);
        const int nameH = QFontMetrics(boldF).height();
        const int msgH  = QFontMetrics(font())
            .boundingRect(0, 0, textW, 10000, Qt::TextWordWrap, m_message).height();
        return kPadV + nameH + kGap + msgH + kPadV;
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

        const QColor accent = channelColor(m_channel);

        // Accent bar — stops short at the bottom to visually separate rows
        p.fillRect(0, 0, kBarW, height() - kBarGap, accent);

        // Fonts
        QFont boldF = font(); boldF.setBold(true);
        QFont smallF = font(); smallF.setPointSizeF(Theme::fontSm);
        const QFontMetrics boldFm(boldF), fm(font()), smallFm(smallF);

        const int nameH = boldFm.height();
        const int textX = kBarW + kBadgePadL + kBadgeW + kBadgeTextPad;
        const int textW = width() - textX - kPadR;

        // Square badge on the left
        const int badgeH = kBadgeW;
        p.setBrush(accent);
        p.setPen(Qt::NoPen);
        p.drawRoundedRect(kBarW + kBadgePadL, kPadV, kBadgeW, badgeH, 5, 5);

        if (m_channel == "@from" || m_channel == "@to") {
            const QString svgPath = (m_channel == "@from")
                ? QStringLiteral(":/icons/chevron-bar-left.svg")
                : QStringLiteral(":/icons/chevron-bar-right.svg");
            const int pad      = 6;
            const int iconSize = kBadgeW - pad * 2;
            const qreal dpr    = devicePixelRatioF();
            QPixmap pix(qRound(iconSize * dpr), qRound(iconSize * dpr));
            pix.setDevicePixelRatio(dpr);
            pix.fill(Qt::transparent);
            { QPainter gp(&pix); QSvgRenderer(svgPath).render(&gp); }
            { QPainter cp(&pix);
              cp.setCompositionMode(QPainter::CompositionMode_SourceIn);
              cp.fillRect(pix.rect(), Qt::white); }
            p.drawPixmap(kBarW + kBadgePadL + pad, kPadV + pad, pix);
        } else {
            p.setFont(boldF);
            p.setPen(Qt::white);
            p.drawText(kBarW + kBadgePadL, kPadV, kBadgeW, badgeH,
                       Qt::AlignCenter, channelBadge(m_channel));
        }

        int y = kPadV;

        // Name row – guild tag then player name, timestamp inline after name
        const int timeW      = smallFm.horizontalAdvance(m_time) + 4;
        const int nameAvailW = textW - timeW - 8;
        int nameX = textX;

        if (!m_guild.isEmpty()) {
            const QString gStr = QStringLiteral("<%1> ").arg(m_guild);
            const int gw = qMin(fm.horizontalAdvance(gStr), nameAvailW);
            p.setFont(font());
            p.setPen(palette().placeholderText().color());
            p.drawText(nameX, y, gw, nameH, Qt::AlignLeft | Qt::AlignVCenter, gStr);
            nameX += gw;
        }

        const int nameRemain = textX + nameAvailW - nameX;
        if (nameRemain > 0) {
            const QString eName = boldFm.elidedText(m_player, Qt::ElideRight, nameRemain);
            p.setFont(boldF);
            p.setPen(palette().windowText().color());
            p.drawText(nameX, y, nameRemain, nameH, Qt::AlignLeft | Qt::AlignVCenter, eName);
            nameX += boldFm.horizontalAdvance(eName);
        }

        // Timestamp immediately after name
        p.setFont(smallF);
        p.setPen(palette().placeholderText().color());
        p.drawText(nameX + 8, y, timeW, nameH, Qt::AlignLeft | Qt::AlignVCenter, m_time);

        y += nameH + kGap;

        // Message (same left indent as name, badge visually spans into this area)
        p.setFont(font());
        p.setPen(palette().windowText().color());
        p.drawText(textX, y, textW, height() - y - kPadV, Qt::TextWordWrap, m_message);
    }

private:
    static constexpr int kBarW        = 4;
    static constexpr int kBarGap      = 5;   // bottom gap on accent bar to mark row boundary
    static constexpr int kBadgePadL   = 4;   // gap between accent bar and badge
    static constexpr int kBadgeW      = 28;  // badge width
    static constexpr int kBadgeTextPad = 8;  // gap between badge and text
    static constexpr int kPadR        = 10;
    static constexpr int kPadV        = 6;
    static constexpr int kGap         = 3;

    QString m_channel, m_player, m_guild, m_message, m_time;
};

// ---- Date-bucket helpers ----------------------------------------------------

struct DateBucket { QString label, from, to; };

static QList<DateBucket> makeDateBuckets(const QDate &today, const QStringList &dates)
{
    QList<DateBucket> buckets;
    const QString todayStr = today.toString(Qt::ISODate);
    const QString yestStr  = today.addDays(-1).toString(Qt::ISODate);
    const int     dow      = today.dayOfWeek();
    const QDate   sow      = today.addDays(1 - dow);
    const QString sowStr   = sow.toString(Qt::ISODate);
    const QString slwStr   = sow.addDays(-7).toString(Qt::ISODate);
    const QString elwStr   = sow.addDays(-1).toString(Qt::ISODate);
    const QString somStr   = QDate(today.year(), today.month(), 1).toString(Qt::ISODate);

    buckets << DateBucket{"Today",      todayStr, todayStr};
    buckets << DateBucket{"Yesterday",  yestStr,  yestStr};
    buckets << DateBucket{"This Week",  sowStr,   todayStr};
    buckets << DateBucket{"Last Week",  slwStr,   elwStr};
    buckets << DateBucket{"This Month", somStr,   todayStr};

    const QLocale locale;
    for (int m = today.month() - 1; m >= 1; --m) {
        const QDate first = QDate(today.year(), m, 1);
        const QDate last  = first.addMonths(1).addDays(-1);
        buckets << DateBucket{locale.standaloneMonthName(m),
                              first.toString(Qt::ISODate),
                              last.toString(Qt::ISODate)};
    }

    QSet<int> years;
    for (const QString &d : dates)
        years.insert(d.left(4).toInt());
    years.remove(today.year());
    QList<int> sortedYears = years.values();
    std::sort(sortedYears.begin(), sortedYears.end(), std::greater<int>());
    for (int y : sortedYears)
        buckets << DateBucket{QString::number(y),
                              QStringLiteral("%1-01-01").arg(y),
                              QStringLiteral("%1-12-31").arg(y)};
    return buckets;
}

static QStringList datesInBucket(const QStringList &dates, const DateBucket &b)
{
    QStringList result;
    for (const QString &d : dates)
        if (d >= b.from && d <= b.to) result << d;
    return result;
}

// ---- ScrollArrowButton ------------------------------------------------------

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

// ---- ChatPage ---------------------------------------------------------------

ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent)
{
    // ---- Checkbox row -------------------------------------------------------
    m_cbLocal  = new QCheckBox("Local",  this);
    m_cbGlobal = new QCheckBox("Global", this);
    m_cbParty  = new QCheckBox("Party",  this);
    m_cbDm     = new QCheckBox("DM",     this);
    m_cbTrade  = new QCheckBox("Trade",  this);
    m_cbGuild  = new QCheckBox("Guild",  this);

    for (QCheckBox *cb : {m_cbLocal, m_cbGlobal, m_cbParty, m_cbDm, m_cbTrade, m_cbGuild})
        cb->setChecked(true);

    m_cbLocal->setEnabled(false);
    m_cbLocal->setToolTip("Local chat is not yet captured from the log");

    m_filterBtn = new QPushButton(this);
    m_filterBtn->setFlat(true);
    m_filterBtn->setStyleSheet("QPushButton { text-align: right; padding: 4px 8px; }");
    connect(m_filterBtn, &QPushButton::clicked, this, [this] {
        if (m_view->currentIndex() == 1)
            m_view->setCurrentIndex(0);
        else
            openFilterPanel();
    });

    auto *cbRow = new QWidget(this);
    auto *cbBox = new QHBoxLayout(cbRow);
    cbBox->setContentsMargins(Theme::spacingSm, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
    cbBox->setSpacing(Theme::spacingBase);
    cbBox->addWidget(m_cbLocal);
    cbBox->addWidget(m_cbGlobal);
    cbBox->addWidget(m_cbParty);
    cbBox->addWidget(m_cbDm);
    cbBox->addWidget(m_cbTrade);
    cbBox->addWidget(m_cbGuild);
    cbBox->addStretch(1);
    cbBox->addWidget(m_filterBtn);

    const auto onToggle = [this] {
        m_limit        = kInitialLimit;
        m_windowOffset = 0;
        m_fromDate.clear();
        m_toDate.clear();
        updateFilterLabel();
        if (isVisible()) rebuild();
        else m_dirty = true;
    };
    for (QCheckBox *cb : {m_cbGlobal, m_cbParty, m_cbDm, m_cbTrade, m_cbGuild})
        connect(cb, &QCheckBox::toggled, this, onToggle);

    updateFilterLabel();

    // ---- Separator ----------------------------------------------------------
    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);

    // ---- Scroll area --------------------------------------------------------
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
        m_filterScroll = new QScrollArea(m_filterPanel);
        m_filterScroll->setWidgetResizable(true);
        m_filterScroll->setFrameShape(QFrame::NoFrame);
        m_filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        // Placeholder list — replaced each time openFilterPanel is called.
        auto *placeholder = new QWidget;
        (new QVBoxLayout(placeholder))->addStretch(1);
        m_filterScroll->setWidget(placeholder);

        auto *upBtn   = new ScrollArrowButton("▲", -1, m_filterScroll, m_filterPanel);
        auto *downBtn = new ScrollArrowButton("▼",  1, m_filterScroll, m_filterPanel);

        auto *header = new QWidget(m_filterPanel);
        auto *hbox   = new QHBoxLayout(header);
        hbox->setContentsMargins(Theme::spacingXs, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
        hbox->setSpacing(Theme::spacingSm);

        m_backBtn = new QPushButton("← Back", header);
        m_backBtn->setFlat(true);
        connect(m_backBtn, &QPushButton::clicked, this, [this] {
            if (m_filterPath.isEmpty()) {
                m_view->setCurrentIndex(0);
            } else {
                m_filterPath.removeLast();
                refreshFilterPanel();
                m_filterScroll->verticalScrollBar()->setValue(0);
            }
        });

        m_filterTitle = new QLabel(header);
        QFont f = m_filterTitle->font(); f.setBold(true);
        m_filterTitle->setFont(f);

        hbox->addWidget(m_backBtn);
        hbox->addWidget(m_filterTitle, 1);

        auto *hdrSep = new QFrame(m_filterPanel);
        hdrSep->setFrameShape(QFrame::HLine);
        hdrSep->setFrameShadow(QFrame::Sunken);

        auto *vbox = new QVBoxLayout(m_filterPanel);
        vbox->setContentsMargins(0, 0, 0, 0);
        vbox->setSpacing(0);
        vbox->addWidget(header);
        vbox->addWidget(hdrSep);
        vbox->addWidget(upBtn);
        vbox->addWidget(m_filterScroll, 1);
        vbox->addWidget(downBtn);
    }

    // ---- Stacked view: 0 = chat scroll, 1 = filter panel -------------------
    m_view = new QStackedWidget(this);
    m_view->addWidget(m_scroll);
    m_view->addWidget(m_filterPanel);

    // ---- Main layout --------------------------------------------------------
    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, Theme::spacingXs, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(cbRow);
    vbox->addWidget(sep);
    vbox->addWidget(m_view, 1);

    // ---- Live rebuild timer -------------------------------------------------
    m_liveRebuildTimer = new QTimer(this);
    m_liveRebuildTimer->setSingleShot(true);
    m_liveRebuildTimer->setInterval(300);
    connect(m_liveRebuildTimer, &QTimer::timeout, this, [this] {
        if (!isVisible()) { m_dirty = true; return; }
        rebuild(); // scroll-to-bottom handled in applyChats via m_liveRebuildScrollToBottom
    });

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &ChatPage::onLiveChat);

    m_scrollDownBtn = new ScrollJumpButton(this);
    m_scrollDownBtn->hide();
    m_scrollDownBtn->raise();
    connect(m_scrollDownBtn, &QPushButton::clicked, this, &ChatPage::jumpToLiveView);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { updateScrollDownBtn(); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int, int) { updateScrollDownBtn(); });
    connect(m_view, &QStackedWidget::currentChanged,
            this, [this](int) { updateScrollDownBtn(); });
}

void ChatPage::setQueryService(QueryService *qs)
{
    m_queryService = qs;
}

void ChatPage::setShowGuildTags(bool show)
{
    if (m_showGuildTags == show) return;
    m_showGuildTags = show;
    if (isVisible() && m_queryService) rebuild();
    else m_dirty = true;
}

void ChatPage::reload()
{
    m_dirty        = false;
    m_limit        = kInitialLimit;
    m_windowOffset = 0;
    rebuild();
    QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
}

void ChatPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_queryService)
        reload();
}

void ChatPage::onLiveChat(const LiveEvent &event, bool bulk)
{
    if (event.type == LiveEventType::Chat) {
        const QString ch = event.data.value("channel").toString();
        if (ch.isEmpty() || !activeChannels().contains(ch[0]))
            return;
    } else if (event.type == LiveEventType::Whisper) {
        if (!m_cbDm->isChecked()) return;
    } else {
        return;
    }

    if (!isVisible()) { m_dirty = true; return; }

    if (bulk) {
        m_liveRebuildTimer->stop();
        rebuild();
        return;
    }

    // Only auto-scroll if the view was already at the bottom and no date filter active.
    if (!m_liveRebuildTimer->isActive() && m_fromDate.isEmpty())
        m_liveRebuildScrollToBottom =
            m_scroll->verticalScrollBar()->value() >= m_scroll->verticalScrollBar()->maximum() - 4;

    if (m_fromDate.isEmpty())
        m_liveRebuildTimer->start();
    else
        m_dirty = true;
}

QSet<QChar> ChatPage::activeChannels() const
{
    QSet<QChar> result;
    if (m_cbGlobal->isChecked()) result.insert(QLatin1Char('#'));
    if (m_cbTrade->isChecked())  result.insert(QLatin1Char('$'));
    if (m_cbParty->isChecked())  result.insert(QLatin1Char('%'));
    if (m_cbGuild->isChecked())  result.insert(QLatin1Char('&'));
    return result;
}

void ChatPage::updateFilterLabel()
{
    if (!m_fromDate.isEmpty()) {
        const QString label = (m_fromDate == m_toDate)
            ? m_fromDate
            : QStringLiteral("%1 – %2").arg(m_fromDate, m_toDate);
        m_filterBtn->setText(QStringLiteral("Filtered: %1").arg(label));
    } else {
        m_filterBtn->setText("Filter");
    }
}

void ChatPage::rebuild()
{
    if (!m_queryService) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    const QSet<QChar> channels   = activeChannels();
    const bool        includeDms = m_cbDm->isChecked();

    // Capture scroll position for live rebuild restoration (not at bottom, not load-previous).
    if (m_scrollRestoreMax < 0 && !m_liveRebuildScrollToBottom) {
        const auto *sb = m_scroll->verticalScrollBar();
        if (sb->maximum() > 0) {
            m_scrollRestoreMax   = sb->maximum();
            m_scrollRestoreValue = sb->value();
        }
    }

    m_queryService->fetchChats(channels, includeDms, m_limit, m_fromDate, m_toDate, m_windowOffset,
        [this](QList<Database::ChatRecord> records) {
            m_rebuildInFlight = false;
            applyChats(records);
            if (m_dirty) QTimer::singleShot(0, this, [this] { rebuild(); });
        });
}

void ChatPage::applyChats(const QList<Database::ChatRecord> &records)
{
    auto *content = new QWidget;
    auto *layout  = new QVBoxLayout(content);
    layout->setContentsMargins(0, Theme::spacingSm, 0, Theme::spacingSm);
    layout->setSpacing(0);
    layout->addStretch(1);

    // "Load previous 50" at the top.
    if (records.size() == m_limit) {
        auto *btn = new QPushButton(
            QStringLiteral("Load previous %1 messages").arg(kPageStep), content);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this] {
            m_scrollRestoreMax   = m_scroll->verticalScrollBar()->maximum();
            m_scrollRestoreValue = m_scroll->verticalScrollBar()->value();
            if (m_limit < kMaxWindow) {
                m_limit += kPageStep;
            } else {
                m_windowOffset += kPageStep;
                m_scrollRestoreNthRecord = kPageStep;
            }
            rebuild();
        });
        layout->addWidget(btn);
    }

    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QString lastDate;
    for (const auto &r : records) {
        const QString date = r.occurredAt.left(10);
        if (date != lastDate) {
            lastDate = date;
            layout->addWidget(new DateSeparator(date, content));
        }
        const QString timeLabel = (date == today)
            ? r.occurredAt.mid(11, 5)
            : r.occurredAt.left(16);
        const QString guild = m_showGuildTags ? r.guildTag : QString{};
        layout->addWidget(
            new ChatRow(r.channel, r.playerName, guild, r.message, timeLabel, content));
    }

    // "Load next 50" at the bottom — only when window is slid away from newest.
    if (m_windowOffset > 0) {
        auto *btn = new QPushButton(
            QStringLiteral("Load next %1 messages").arg(kPageStep), content);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this] {
            m_scrollRestoreMax   = m_scroll->verticalScrollBar()->maximum();
            m_scrollRestoreValue = m_scroll->verticalScrollBar()->value();
            m_windowOffset = qMax(0, m_windowOffset - kPageStep);
            rebuild();
        });
        layout->addWidget(btn);
    }

    delete m_content;
    m_content       = content;
    m_contentLayout = layout;
    m_scroll->setWidget(m_content);

    if (m_liveRebuildScrollToBottom) {
        m_liveRebuildScrollToBottom = false;
        m_scrollRestoreMax = -1;
        QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
    } else if (m_scrollRestoreMax >= 0) {
        const int prevMax   = m_scrollRestoreMax;
        const int prevValue = m_scrollRestoreValue;
        const int nthRecord = m_scrollRestoreNthRecord;
        m_scrollRestoreMax       = -1;
        m_scrollRestoreNthRecord = -1;
        QTimer::singleShot(0, this, [this, prevMax, prevValue, nthRecord] {
            if (nthRecord >= 0) {
                int count = 0;
                for (int i = 0; i < m_contentLayout->count(); ++i) {
                    QLayoutItem *li = m_contentLayout->itemAt(i);
                    QWidget *w = li ? li->widget() : nullptr;
                    if (!w || qobject_cast<QPushButton*>(w)
                            || w->objectName() == "separator") continue;
                    if (count++ == nthRecord) {
                        m_scroll->verticalScrollBar()->setValue(
                            qMin(w->y(), m_scroll->verticalScrollBar()->maximum()));
                        return;
                    }
                }
            }
            const int delta = m_scroll->verticalScrollBar()->maximum() - prevMax;
            m_scroll->verticalScrollBar()->setValue(prevValue + delta);
        });
    } else {
        QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
    }
}

void ChatPage::openFilterPanel()
{
    if (!m_queryService) return;
    const QSet<QChar> channels   = activeChannels();
    const bool        includeDms = m_cbDm->isChecked();
    m_queryService->fetchChatDates(channels, includeDms, [this](QStringList dates) {
        m_cachedDates = std::move(dates);
        m_filterPath.clear();
        refreshFilterPanel();
        m_filterScroll->verticalScrollBar()->setValue(0);
        m_view->setCurrentIndex(1);
    });
}

void ChatPage::refreshFilterPanel()
{
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
        m_filterTitle->setText("Filter by date");
        m_backBtn->setText("✕ Close");

        addRow("Reset filter — Show all dates", false, [this] {
            m_view->setCurrentIndex(0);
            m_fromDate.clear();
            m_toDate.clear();
            m_limit = kInitialLimit; m_windowOffset = 0;
            updateFilterLabel();
            rebuild();
            QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
        });
        addSep();

        const QList<DateBucket> buckets = makeDateBuckets(QDate::currentDate(), m_cachedDates);
        for (const DateBucket &b : buckets) {
            const QStringList inBucket = datesInBucket(m_cachedDates, b);
            if (inBucket.isEmpty()) continue;

            const int n = inBucket.size();
            const QString label = n == 1
                ? QStringLiteral("%1  (%2)").arg(b.label, inBucket[0])
                : QStringLiteral("%1  (%2 days)").arg(b.label).arg(n);

            const bool drill = n > 1;
            addRow(label, drill, [this, b, inBucket, drill] {
                if (!drill) {
                    // Single date — apply directly
                    m_view->setCurrentIndex(0);
                    m_fromDate = m_toDate = inBucket[0];
                    m_limit = kInitialLimit; m_windowOffset = 0;
                    updateFilterLabel();
                    rebuild();
                    QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
                } else {
                    m_filterPath.append(b.label);
                    refreshFilterPanel();
                    m_filterScroll->verticalScrollBar()->setValue(0);
                }
            });
        }
    }
    // ---- Bucket level: list individual dates --------------------------------
    else {
        const QString &bucketLabel = m_filterPath[0];
        m_filterTitle->setText(bucketLabel);
        m_backBtn->setText("← Back");

        const QList<DateBucket> buckets = makeDateBuckets(QDate::currentDate(), m_cachedDates);
        QStringList dates;
        for (const DateBucket &b : buckets) {
            if (b.label == bucketLabel) {
                dates = datesInBucket(m_cachedDates, b);
                break;
            }
        }

        for (const QString &date : dates) {
            addRow(date, false, [this, date] {
                m_view->setCurrentIndex(0);
                m_fromDate = m_toDate = date;
                m_limit = kInitialLimit; m_windowOffset = 0;
                updateFilterLabel();
                rebuild();
                QTimer::singleShot(0, this, &ChatPage::scrollToBottom);
            });
        }
    }

    listLayout->addStretch(1);
    m_filterScroll->setWidget(listWidget);
}

void ChatPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_scrollDownBtn->move(rect().right()  - m_scrollDownBtn->width()  - Theme::spacing3xl,
                          rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}

void ChatPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom    = sb->value() >= sb->maximum() - 4;
    const bool hasNextPage = m_windowOffset > 0;
    const bool show        = m_view->currentIndex() == 0 && (!atBottom || hasNextPage);
    m_scrollDownBtn->setSkipMode(hasNextPage);
    m_scrollDownBtn->setVisible(show);
}

void ChatPage::scrollToBottom()
{
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void ChatPage::jumpToLiveView()
{
    if (m_windowOffset == 0) {
        scrollToBottom();
        return;
    }
    m_windowOffset           = 0;
    m_limit                  = kInitialLimit;
    m_fromDate.clear();
    m_toDate.clear();
    m_scrollRestoreMax       = -1;
    m_scrollRestoreNthRecord = -1;
    updateFilterLabel();
    rebuild();
}
