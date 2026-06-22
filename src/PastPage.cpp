#include "PastPage.h"
#include "Database.h"
#include "LiveEvent.h"
#include "QueryService.h"
#include "LiveEventBus.h"
#include "NotificationWidget.h"
#include "ScrollJumpButton.h"
#include "Theme.h"

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
    const int h = secs / 3600;
    const int m = (secs % 3600) / 60;
    if (h > 0) return QStringLiteral("%1h %2m").arg(h).arg(m);
    return QStringLiteral("%1m").arg(m);
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
            if (m_dirty) rebuild();
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
            QStringLiteral("Load previous %1 notifications").arg(kPageStep), content);
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
        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QString lastDate;

        for (const auto &ev : events) {
            const QString date = ev.occurredAt.left(10);
            if (date != lastDate) {
                lastDate = date;
                layout->addWidget(new DateSeparator(date, content));
            }
            const QString timeLabel = (date == today)
                ? ev.occurredAt.mid(11, 5)
                : ev.occurredAt.left(16);

            if (ev.eventType == "start") {
                NotificationStyle style;
                style.accentColor = {80, 180, 80};
                QString msg;
                if (!ev.charName.isEmpty()) {
                    msg = ev.charName;
                    if (!ev.charClass.isEmpty())
                        msg += " \xc2\xb7 " + ev.charClass;
                }
                layout->addWidget(new NotificationWidget(
                    "Game started", {}, msg, timeLabel, style, content));
            } else {
                NotificationStyle style;
                style.accentColor = {130, 130, 130};
                QString msg;
                const QString active = formatDuration(ev.activeSecs);
                const QString total  = formatDuration(ev.totalSecs);
                if (!active.isEmpty())
                    msg = "Active: " + active;
                else if (!total.isEmpty())
                    msg = "Duration: " + total;
                layout->addWidget(new NotificationWidget(
                    "Game stopped", {}, msg, timeLabel, style, content));
            }
        }
    }

    // "Load next 50" at the bottom — shows when we've slid the window away from newest.
    if (m_windowOffset > 0) {
        auto *btn = new QPushButton(
            QStringLiteral("Load next %1 notifications").arg(kPageStep), content);
        btn->setFlat(true);
        connect(btn, &QPushButton::clicked, this, [this] {
            m_scrollRestoreMax   = -1;   // scroll to bottom of new content
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
        m_scrollRestoreMax = -1;
        QTimer::singleShot(0, this, [this, prevMax, prevValue] {
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
    m_windowOffset   = 0;
    m_limit          = kInitialLimit;
    m_scrollRestoreMax = -1;
    rebuild();
}

void PastPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    m_scrollDownBtn->setVisible(!atBottom);
}
