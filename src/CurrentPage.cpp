#include "CurrentPage.h"
#include "Database.h"
#include "LiveEvent.h"
#include "LiveEventBus.h"
#include "Theme.h"

#include <QDateTime>
#include <QFrame>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

static QString formatDuration(int secs)
{
    if (secs <= 0) return {};
    const int h = secs / 3600;
    const int m = (secs % 3600) / 60;
    if (h > 0) return QStringLiteral("%1h %2m").arg(h).arg(m);
    return QStringLiteral("%1m").arg(m);
}

static NotificationStyle zoneStyle()
{
    NotificationStyle s;
    s.accentColor = QColor(100, 170, 215);
    return s;
}

CurrentPage::CurrentPage(QWidget *parent)
    : QWidget(parent)
{
    m_scroll = new QScrollArea(this);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->setFrameShape(QFrame::NoFrame);

    m_content = new QWidget;
    m_contentLayout = new QVBoxLayout(m_content);
    m_contentLayout->setSpacing(6);
    m_contentLayout->setContentsMargins(Theme::spacingSm, Theme::spacingSm,
                                        Theme::spacingSm, Theme::spacingSm);

    m_loadMoreBtn = new QPushButton(
        QStringLiteral("Load %1 more zone transitions").arg(kDbZoneLimit), m_content);
    m_loadMoreBtn->setFlat(true);
    connect(m_loadMoreBtn, &QPushButton::clicked, this, &CurrentPage::onLoadMore);

    // Layout bottom anchor: [load-more btn] [stretch]
    // Live items are prepended at index 0; DB zones inserted before the btn.
    m_contentLayout->addWidget(m_loadMoreBtn);
    m_contentLayout->addStretch();
    setLoadMoreVisible(false);

    m_scroll->setWidget(m_content);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_scroll, 1);

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &CurrentPage::onLiveEvent);
}

void CurrentPage::setDatabase(Database *db)
{
    m_db    = db;
    m_dirty = true;
}

void CurrentPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_db)
        rebuildDbZones();
}

// ---------------------------------------------------------------------------
// Public notification API (passes non-zone live events straight through)
// ---------------------------------------------------------------------------

void CurrentPage::addNotification(const QString &message, const NotificationStyle &style)
{
    auto *w = new NotificationWidget(
        {}, {}, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
    prependWidget(w);
}

void CurrentPage::addNotification(const QString &title, const QString &tag,
                                   const QString &message, const NotificationStyle &style)
{
    auto *w = new NotificationWidget(
        title, tag, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
    prependWidget(w);
}

// ---------------------------------------------------------------------------
// Live event handling
// ---------------------------------------------------------------------------

void CurrentPage::onLiveEvent(const LiveEvent &event)
{
    if (event.type == LiveEventType::AreaEntered) {
        const QString areaName  = event.data.value("area_name").toString();
        const int     areaLevel = event.data.value("area_level").toInt();

        // Stamp the previous zone's card with the time spent there.
        if (m_prevZoneCard && m_db) {
            // The worker closes span N-1 before emitting AreaEntered for span N,
            // so fetchZoneTransitions(2, 0) gives [new zone, previous zone].
            const auto recent = m_db->fetchZoneTransitions(2, 0);
            if (recent.size() >= 2 && recent[1].durationSecs > 0)
                m_prevZoneCard->setMessage(formatDuration(recent[1].durationSecs));
        }

        const QString ts = QDateTime::currentDateTime().toString("HH:mm");
        auto *card = makeZoneCard(areaName, areaLevel, ts, -1);
        prependWidget(card);
        m_prevZoneCard = card;

    } else if (event.type == LiveEventType::SessionStart) {
        m_dirty = true;
        if (isVisible() && m_db)
            rebuildDbZones();
    }
}

// ---------------------------------------------------------------------------
// DB zone section
// ---------------------------------------------------------------------------

void CurrentPage::rebuildDbZones()
{
    if (!m_db) return;
    m_dirty = false;

    // Remove and delete all previously loaded DB zone widgets.
    for (NotificationWidget *w : m_dbZoneWidgets) {
        m_contentLayout->removeWidget(w);
        delete w;
    }
    m_dbZoneWidgets.clear();
    m_prevZoneCard  = nullptr;
    m_dbZoneOffset  = 0;

    setLoadMoreVisible(false);

    const auto zones = m_db->fetchZoneTransitions(kDbZoneLimit, 0);
    m_dbZoneOffset   = zones.size();

    for (const auto &z : zones) {
        const QString ts = z.enteredAt.mid(11, 5); // HH:MM
        auto *card = makeZoneCard(z.areaName, z.areaLevel, ts, z.durationSecs);
        insertDbZone(card);

        // The first (newest) DB zone that has no duration is the current zone.
        if (z.durationSecs < 0 && !m_prevZoneCard)
            m_prevZoneCard = card;
    }

    setLoadMoreVisible(zones.size() == kDbZoneLimit);
}

void CurrentPage::onLoadMore()
{
    if (!m_db) return;

    const int prevMax   = m_scroll->verticalScrollBar()->maximum();
    const int prevValue = m_scroll->verticalScrollBar()->value();

    const auto zones = m_db->fetchZoneTransitions(kDbZoneLimit, m_dbZoneOffset);
    m_dbZoneOffset += zones.size();

    for (const auto &z : zones) {
        const QString ts = z.enteredAt.mid(11, 5);
        insertDbZone(makeZoneCard(z.areaName, z.areaLevel, ts, z.durationSecs));
    }

    setLoadMoreVisible(zones.size() == kDbZoneLimit);

    QTimer::singleShot(0, this, [this, prevMax, prevValue]() {
        const int delta = m_scroll->verticalScrollBar()->maximum() - prevMax;
        m_scroll->verticalScrollBar()->setValue(prevValue + delta);
    });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NotificationWidget *CurrentPage::makeZoneCard(const QString &areaName, int areaLevel,
                                               const QString &timestamp, int durationSecs)
{
    const QString tag  = areaLevel > 0 ? QStringLiteral("lv %1").arg(areaLevel) : QString{};
    const QString body = durationSecs > 0 ? formatDuration(durationSecs) : QString{};
    return new NotificationWidget(areaName, tag, body, timestamp, zoneStyle(), m_content);
}

void CurrentPage::insertDbZone(NotificationWidget *card)
{
    // Insert before the load-more button (or before the stretch if button is absent).
    const int btnIdx = m_contentLayout->indexOf(m_loadMoreBtn);
    const int pos    = btnIdx >= 0 ? btnIdx : (m_contentLayout->count() - 1);
    m_contentLayout->insertWidget(pos, card);
    m_dbZoneWidgets.append(card);
}

void CurrentPage::setLoadMoreVisible(bool visible)
{
    const int idx = m_contentLayout->indexOf(m_loadMoreBtn);
    if (visible && idx < 0) {
        // Re-insert before the trailing stretch.
        m_contentLayout->insertWidget(m_contentLayout->count() - 1, m_loadMoreBtn);
        m_loadMoreBtn->show();
    } else if (!visible && idx >= 0) {
        m_contentLayout->removeWidget(m_loadMoreBtn);
        m_loadMoreBtn->hide();
    }
}

void CurrentPage::prependWidget(QWidget *w)
{
    m_contentLayout->insertWidget(0, w);
    m_contentLayout->activate();
    QTimer::singleShot(0, this, [this]() {
        m_scroll->verticalScrollBar()->setValue(0);
    });
}
