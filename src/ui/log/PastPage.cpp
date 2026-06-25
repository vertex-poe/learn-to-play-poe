#include "ui/log/PastPage.h"
#include "db/Database.h"
#include "util/Docs.h"
#include "events/LiveEvent.h"
#include "db/QueryService.h"
#include "events/LiveEventBus.h"
#include "ui/widgets/NotificationWidget.h"
#include "ui/widgets/ScrollJumpButton.h"
#include "ui/Theme.h"

#include <QDate>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

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

// ---- helpers ----------------------------------------------------------------

static QString formatDuration(int secs)
{
    if (secs <= 0) return {};
    constexpr int kYear  = 365 * 86400;
    constexpr int kMonth = 30  * 86400;
    constexpr int kWeek  = 7   * 86400;
    const int Y = secs / kYear;
    const int M = (secs % kYear)  / kMonth;
    const int W = (secs % kMonth) / kWeek;
    const int D = (secs % kWeek)  / 86400;
    const int h = (secs % 86400)  / 3600;
    const int m = (secs % 3600)   / 60;
    const int s = secs % 60;
    if (Y > 0)
        return (Y > 5 || M == 0) ? QStringLiteral("%1Y").arg(Y)
                                  : QStringLiteral("%1Y%2M").arg(Y).arg(M);
    if (M > 0)
        return (M > 5 || W == 0) ? QStringLiteral("%1M").arg(M)
                                  : QStringLiteral("%1M%2W").arg(M).arg(W);
    if (W > 0)
        return (W > 5 || D == 0) ? QStringLiteral("%1W").arg(W)
                                  : QStringLiteral("%1W%2D").arg(W).arg(D);
    if (D > 0)
        return (D > 5 || h == 0) ? QStringLiteral("%1D").arg(D)
                                  : QStringLiteral("%1D%2h").arg(D).arg(h);
    if (h > 0)
        return (h > 5 || m == 0) ? QStringLiteral("%1h").arg(h)
                                  : QStringLiteral("%1h%2m").arg(h).arg(m);
    if (m > 0)
        return (m > 5 || s == 0) ? QStringLiteral("%1m").arg(m)
                                  : QStringLiteral("%1m%2s").arg(m).arg(s);
    return QStringLiteral("%1s").arg(s);
}

static QString formatDuration(double secs)
{
    if (secs <= 0.0) return {};
    const int si = static_cast<int>(secs);
    const int ms = qRound((secs - si) * 1000);
    if (si > 5) return QStringLiteral("%1s").arg(si);
    if (ms > 0) return QStringLiteral("%1.%2s").arg(si).arg(ms, 3, 10, QChar('0'));
    return QStringLiteral("%1s").arg(si);
}

// ---- PastPage ---------------------------------------------------------------

PastPage::PastPage(QWidget *parent)
    : QWidget(parent)
{
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_content = new QWidget;
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->addStretch(1);
    m_scroll->setWidget(m_content);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_scroll, 1);

    m_scrollDownBtn = new ScrollJumpButton(this);
    m_scrollDownBtn->hide();
    m_scrollDownBtn->raise();
    connect(m_scrollDownBtn, &QPushButton::clicked, this, &PastPage::jumpToLiveView);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { updateScrollDownBtn(); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int, int) { updateScrollDownBtn(); });

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &PastPage::onLiveEvent);
}

void PastPage::setQueryService(QueryService *qs)
{
    m_queryService = qs;
    m_limit        = kInitialLimit;
    m_windowOffset = 0;
    m_dirty        = true;
}

void PastPage::markDirty()
{
    m_dirty = true;
}

void PastPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_queryService)
        rebuild();
}

void PastPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_scrollDownBtn->move(rect().right()  - m_scrollDownBtn->width()  - Theme::spacing3xl,
                          rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}

void PastPage::onLiveEvent(const LiveEvent &event, bool bulk)
{
    if (bulk || event.type == LiveEventType::SessionStart)
        m_dirty = true;
}

void PastPage::rebuild()
{
    if (!m_queryService) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    m_queryService->fetchSessionEvents(m_limit, m_windowOffset,
        [this](QList<Database::SessionEventRecord> events) {
            m_rebuildInFlight = false;
            applySessionEvents(events);
            if (m_dirty) QTimer::singleShot(0, this, [this] { rebuild(); });
        });
}

void PastPage::applySessionEvents(const QList<Database::SessionEventRecord> &events)
{
    auto *content = new QWidget;
    auto *layout  = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::spacingSm, Theme::spacingSm,
                               Theme::spacingSm, Theme::spacingSm);
    layout->setSpacing(6);
    layout->addStretch(1);

    // "Load previous 50" at the top — shows when there may be older items.
    if (events.size() == m_limit) {
        auto *btn = new QPushButton(
            QStringLiteral("Load previous %1 events").arg(kPageStep), content);
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

    if (events.isEmpty()) {
        auto *label = new QLabel("No sessions recorded yet.", content);
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, Theme::textPlaceholder);
        label->setPalette(pal);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    } else {
        // Pair each "start" with the following "stop" into a single session card.
        // Unpaired stops (window-boundary orphans) are rendered alone.
        struct Session {
            Database::SessionEventRecord start;
            Database::SessionEventRecord stop;
            bool hasStart{false};
            bool hasStopped{false};
        };
        QList<Session> sessions;
        for (int i = 0; i < events.size(); ++i) {
            if (events[i].eventType == "start") {
                Session s;
                s.start    = events[i];
                s.hasStart = true;
                if (i + 1 < events.size() && events[i + 1].eventType == "stop") {
                    s.stop       = events[++i];
                    s.hasStopped = true;
                }
                sessions.append(s);
            } else {
                Session s;
                s.stop       = events[i];
                s.hasStopped = true;
                sessions.append(s);
            }
        }

        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QString lastDate;

        for (const auto &s : sessions) {
            const QString &anchor    = s.hasStart ? s.start.occurredAt : s.stop.occurredAt;
            const QString  date      = anchor.left(10);
            const QString  timeLabel = (date == today) ? anchor.mid(11, 5) : anchor.left(16);

            if (date != lastDate) {
                lastDate = date;
                layout->addWidget(new DateSeparator(date, content));
            }

            NotificationStyle style;
            style.accentColor = s.hasStopped ? QColor{130, 130, 130} : QColor{80, 180, 80};

            const QString active = formatDuration(s.hasStopped ? s.stop.activeSecs : -1);
            const QString total  = formatDuration(s.hasStopped ? s.stop.totalSecs  : -1);

            auto *card = new NotificationWidget("Session", {}, {}, timeLabel, style, content);

            // Header suffix: duration then character
            QString suffix;
            if (!active.isEmpty())       suffix = active;
            else if (!total.isEmpty())   suffix = total;
            if (!s.start.charName.isEmpty()) {
                if (!suffix.isEmpty()) suffix += " \xc2\xb7 ";
                suffix += s.start.charName;
            }
            if (!suffix.isEmpty())
                card->setHeaderSuffix("\xc2\xb7 " + suffix);

            QList<QPair<QString, QString>> details;
            if (s.hasStart)   details.append({"Started",  s.start.occurredAt});
            if (s.hasStopped) details.append({"Ended",    s.stop.occurredAt});
            if (!active.isEmpty()) details.append({"Active", active});
            if (!total.isEmpty())  details.append({"Total",  total});
            if (!s.start.charName.isEmpty()) {
                QString charInfo = s.start.charName;
                if (!s.start.charClass.isEmpty())
                    charInfo += " \xc2\xb7 " + s.start.charClass;
                details.append({"Character", charInfo});
            }
            if (!s.start.installPath.isEmpty())
                details.append({"Install", s.start.installPath});
            card->setDetailRows(details);

            card->setSource(docSource("Client.txt", "sources/game-started"));
            layout->addWidget(card);

            if (s.hasStart && !s.hasStopped) {
                auto *btn = new QPushButton("Open current session \xe2\x86\x92", content);
                btn->setFlat(true);
                connect(btn, &QPushButton::clicked, this, &PastPage::viewCurrentRequested);
                layout->addWidget(btn);
            }
        }
    }

    // "Load next 50" at the bottom — shows when we've slid the window away from newest.
    if (m_windowOffset > 0) {
        auto *btn = new QPushButton(
            QStringLiteral("Load next %1 events").arg(kPageStep), content);
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

    if (m_scrollRestoreMax >= 0) {
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
        QTimer::singleShot(0, this, &PastPage::scrollToBottom);
    }
}

void PastPage::scrollToBottom()
{
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void PastPage::jumpToLiveView()
{
    if (m_windowOffset == 0) {
        scrollToBottom();
        return;
    }
    m_windowOffset           = 0;
    m_limit                  = kInitialLimit;
    m_scrollRestoreMax       = -1;
    m_scrollRestoreNthRecord = -1;
    rebuild();
}

void PastPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom    = sb->value() >= sb->maximum() - 4;
    const bool hasNextPage = m_windowOffset > 0;
    m_scrollDownBtn->setSkipMode(hasNextPage);
    m_scrollDownBtn->setVisible(!atBottom || hasNextPage);
}
