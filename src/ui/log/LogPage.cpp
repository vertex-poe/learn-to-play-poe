#include "ui/log/LogPage.h"
#include "db/Database.h"
#include <QCoreApplication>
#include <cstdio>
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

// ---- LogPage ---------------------------------------------------------------

LogPage::LogPage(QWidget *parent)
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
    connect(m_scrollDownBtn, &QPushButton::clicked, this, &LogPage::jumpToLiveView);
    connect(m_scroll->verticalScrollBar(), &QScrollBar::valueChanged,
            this, [this](int) { updateScrollDownBtn(); });
    connect(m_scroll->verticalScrollBar(), &QScrollBar::rangeChanged,
            this, [this](int, int) { updateScrollDownBtn(); });

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &LogPage::onLiveEvent);
}

void LogPage::setQueryService(QueryService *qs)
{
    m_queryService = qs;
    m_limit        = kInitialLimit;
    m_windowOffset = 0;
    m_dirty        = true;
}

void LogPage::markDirty()
{
    m_dirty = true;
}

void LogPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_queryService)
        rebuild();
}

void LogPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_scrollDownBtn->move(rect().right()  - m_scrollDownBtn->width()  - Theme::spacing3xl,
                          rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}

void LogPage::onLiveEvent(const LiveEvent &event, bool bulk)
{
    if (bulk || event.type == LiveEventType::SessionStart)
        m_dirty = true;
}

void LogPage::rebuild()
{
    if (!m_queryService) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    m_queryService->fetchSessions(m_limit, m_windowOffset,
        [this](QList<Database::SessionRecord> sessions) {
            m_rebuildInFlight = false;
            applySessions(sessions);
            if (m_dirty) QTimer::singleShot(0, this, [this] { rebuild(); });
        });
}

void LogPage::applySessions(const QList<Database::SessionRecord> &sessions)
{
    auto *content = new QWidget;
    auto *layout  = new QVBoxLayout(content);
    layout->setContentsMargins(Theme::spacingSm, Theme::spacingSm,
                               Theme::spacingSm, Theme::spacingSm);
    layout->setSpacing(6);
    layout->addStretch(1);

    // "Load previous N" at the top — shows when there may be older sessions.
    if (sessions.size() == m_limit) {
        auto *btn = new QPushButton(
            QStringLiteral("Load previous %1 sessions").arg(kPageStep), content);
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

    if (sessions.isEmpty()) {
        auto *label = new QLabel("No sessions recorded yet.", content);
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, Theme::textPlaceholder);
        label->setPalette(pal);
        label->setAlignment(Qt::AlignCenter);
        layout->addWidget(label);
    } else {
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QString lastDate;

        for (const auto &s : sessions) {
            const QString date      = s.startedAt.left(10);
            const QString timeLabel = (date == today) ? s.startedAt.mid(11, 5) : s.startedAt.left(16);

            if (date != lastDate) {
                lastDate = date;
                layout->addWidget(new DateSeparator(date, content));
            }

            const bool running = s.endedAt.isEmpty();
            NotificationStyle style;
            style.accentColor = running ? QColor{80, 180, 80} : QColor{130, 130, 130};

            const QString active = formatDuration(s.activeSecs);
            const QString total  = formatDuration(s.totalSecs);

            auto *card = new NotificationWidget("Session", {}, {}, timeLabel, style, content);

            QString suffix;
            if (!active.isEmpty())      suffix = active;
            else if (!total.isEmpty())  suffix = total;
            if (!s.charName.isEmpty()) {
                if (!suffix.isEmpty()) suffix += " \xc2\xb7 ";
                suffix += s.charName;
            }
            if (!suffix.isEmpty())
                card->setHeaderSuffix("\xc2\xb7 " + suffix);

            QList<QPair<QString, QString>> details;
            details.append({"Started", s.startedAt});
            if (!s.endedAt.isEmpty()) details.append({"Ended",  s.endedAt});
            if (!active.isEmpty())    details.append({"Active", active});
            if (!total.isEmpty())     details.append({"Total",  total});
            if (!s.charName.isEmpty()) {
                QString charInfo = s.charName;
                if (!s.charClass.isEmpty())
                    charInfo += " \xc2\xb7 " + s.charClass;
                details.append({"Character", charInfo});
            }
            if (!s.installPath.isEmpty())
                details.append({"Install", s.installPath});
            card->setDetailRows(details);

            card->setSource(docSource("Client.txt", "sources/game-started"));

            auto *viewBtn = new QPushButton("View", card);
            viewBtn->setFlat(true);
            {
                QFont f = viewBtn->font();
                f.setPointSizeF(Theme::fontSm);
                viewBtn->setFont(f);
                viewBtn->setStyleSheet(QStringLiteral(
                    "QPushButton { padding-left: %1px; padding-right: %1px; padding-top: %2px; padding-bottom: %2px; }")
                    .arg(Theme::buttonPaddingH)
                    .arg(Theme::spacingXs));
            }
            const qint64 sessionId = running ? -1 : s.id;
            const QString startedAt = s.startedAt;
            connect(viewBtn, &QPushButton::clicked, this, [this, sessionId, startedAt] {
                emit viewSessionRequested(sessionId, startedAt);
            });
            card->setActionWidget(viewBtn);
            layout->addWidget(card);
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
        QTimer::singleShot(0, this, &LogPage::scrollToBottom);
    }

    if (!m_timingEmitted && qgetenv("L2P_STARTUP_TIMING_MODE") == "1") {
        m_timingEmitted = true;
        QTimer::singleShot(0, this, [] {
            fputs("STARTUP_TIMING:populated\n", stdout);
            fflush(stdout);
            QCoreApplication::quit();
        });
    }
}

void LogPage::scrollToBottom()
{
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void LogPage::jumpToLiveView()
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

void LogPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom    = sb->value() >= sb->maximum() - 4;
    const bool hasNextPage = m_windowOffset > 0;
    m_scrollDownBtn->setSkipMode(hasNextPage);
    m_scrollDownBtn->setVisible(!atBottom || hasNextPage);
}
