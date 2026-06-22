#include "CurrentPage.h"
#include "Database.h"
#include "LiveEvent.h"
#include "LiveEventBus.h"
#include "QueryService.h"
#include "ScrollJumpButton.h"
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

static NotificationStyle sessionStyle()
{
    NotificationStyle s;
    s.accentColor = QColor(80, 180, 80);
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

    // Layout: [stretch] [session card(s) container] [load-more btn] [db zones] [live events]
    m_contentLayout->addStretch(1);
    m_loadMoreBtn->hide();

    m_scroll->setWidget(m_content);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_scroll, 1);

    m_scrollDownBtn = new ScrollJumpButton(this);
    m_scrollDownBtn->hide();
    m_scrollDownBtn->raise();
    connect(m_scrollDownBtn, &QPushButton::clicked, this, &CurrentPage::scrollToBottom);

    auto *vsb = m_scroll->verticalScrollBar();
    // rangeChanged: re-apply any pending scroll target (layout is still settling).
    connect(vsb, &QScrollBar::rangeChanged, this, &CurrentPage::onScrollRangeChanged);
    // valueChanged: only update the scroll-down button visibility (no forced scroll).
    connect(vsb, &QScrollBar::valueChanged, this, [this](int) { updateScrollDownBtn(); });

    // Clears m_pendingScrollTo 100ms after the last rangeChanged, by which
    // point the layout cascade is definitely finished.
    m_scrollSettleTimer = new QTimer(this);
    m_scrollSettleTimer->setSingleShot(true);
    m_scrollSettleTimer->setInterval(100);
    connect(m_scrollSettleTimer, &QTimer::timeout, this, [this]() {
        m_pendingScrollTo = -1;
    });

    connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
            this, &CurrentPage::onLiveEvent);
}

void CurrentPage::setQueryService(QueryService *qs)
{
    m_queryService = qs;
    m_dirty        = true;
}

void CurrentPage::markDirty()
{
    m_dirty = true;
}

void CurrentPage::setRunningGames(const QList<WindowState> &games)
{
    const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");

    // Preserve detected-at timestamps for continuing PIDs; assign now for new ones.
    QMap<quint32, QString> updated;
    for (const auto &g : games)
        updated[g.pid] = m_detectedAt.value(g.pid, now);
    m_detectedAt = updated;

    // Immediately remove the session card when no games are running.
    if (games.isEmpty() && m_sessionStartCard) {
        m_contentLayout->removeWidget(m_sessionStartCard);
        delete m_sessionStartCard;
        m_sessionStartCard = nullptr;
    }

    m_runningGames = games;
    m_dirty = true;
    if (isVisible() && m_queryService)
        rebuildDbZones();
}

void CurrentPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    if (m_dirty && m_queryService)
        rebuildDbZones();
}

void CurrentPage::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    m_scrollDownBtn->move(rect().right()  - m_scrollDownBtn->width()  - Theme::spacing3xl,
                          rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}

// ---------------------------------------------------------------------------
// Public notification API (passes non-zone live events straight through)
// ---------------------------------------------------------------------------

void CurrentPage::addNotification(const QString &message, const NotificationStyle &style)
{
    auto *w = new NotificationWidget(
        {}, {}, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
    appendLiveWidget(w);
}

void CurrentPage::addNotification(const QString &title, const QString &tag,
                                   const QString &message, const NotificationStyle &style)
{
    auto *w = new NotificationWidget(
        title, tag, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
    appendLiveWidget(w);
}

// ---------------------------------------------------------------------------
// Live event handling
// ---------------------------------------------------------------------------

void CurrentPage::onLiveEvent(const LiveEvent &event, bool bulk)
{
    if (bulk) {
        m_prevZoneCard = nullptr;
        m_dirty = true;
        if (isVisible() && m_queryService)
            rebuildDbZones();
        return;
    }

    if (event.type == LiveEventType::AreaEntered) {
        const QString areaName  = event.data.value("area_name").toString();
        const int     areaLevel = event.data.value("area_level").toInt();

        // Stamp the previous zone's card with the time spent there. The ingest worker
        // closes span N-1 before emitting AreaEntered for span N, so the two most
        // recent transitions are [new zone, previous zone].
        if (m_prevZoneCard && m_queryService) {
            NotificationWidget *prevCard = m_prevZoneCard;
            m_queryService->fetchZoneTransitions(2, 0,
                [this, prevCard](QList<Database::ZoneTransitionRecord> zones) {
                    // m_prevZoneCard may have changed by the time this fires; use
                    // the captured pointer (still valid — still in m_dbZoneWidgets or
                    // m_liveEventWidgets) to stamp it directly.
                    if (zones.size() >= 2 && zones[1].durationSecs > 0)
                        prevCard->setHeaderSuffix("\xc2\xb7 " + formatDuration(zones[1].durationSecs));
                });
        }

        const QString ts = QDateTime::currentDateTime().toString("HH:mm");
        auto *card = makeZoneCard(areaName, areaLevel, ts, -1);
        appendLiveWidget(card);
        m_prevZoneCard = card;

    } else if (event.type == LiveEventType::SessionStart) {
        m_dirty = true;
        if (isVisible() && m_queryService)
            rebuildDbZones();
    }
}

// ---------------------------------------------------------------------------
// DB zone section
// ---------------------------------------------------------------------------

void CurrentPage::rebuildDbZones()
{
    if (!m_queryService) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    // Capture scroll state and running-games snapshot before the async gap.
    const auto *sb = m_scroll->verticalScrollBar();
    const int prevMax        = sb->maximum();
    const int distFromBottom = prevMax > 0 ? (prevMax - sb->value()) : -1;

    const QList<WindowState>    runningGames = m_runningGames;
    const QMap<quint32, QString> detectedAt  = m_detectedAt;

    // Clear live and zone widgets now (before the fetch) so the user never sees
    // a stale zone list. Suppress repaints until we rebuild.
    m_scroll->setUpdatesEnabled(false);

    for (QWidget *w : m_liveEventWidgets) {
        m_contentLayout->removeWidget(w);
        delete w;
    }
    m_liveEventWidgets.clear();

    if (m_sessionStartCard) {
        m_contentLayout->removeWidget(m_sessionStartCard);
        delete m_sessionStartCard;
        m_sessionStartCard = nullptr;
    }

    for (NotificationWidget *w : m_dbZoneWidgets) {
        m_contentLayout->removeWidget(w);
        delete w;
    }
    m_dbZoneWidgets.clear();
    m_prevZoneCard = nullptr;
    m_dbZoneOffset = 0;

    setLoadMoreVisible(false);

    // Determine how many session events we need:
    //   single client: 10 (search for matching start event by folder+time)
    //   multi-client:  50 (interleaved with zones)
    //   no games:       0
    const int sessionLimit = runningGames.size() == 1 ? 10
                           : runningGames.size()  > 1 ? 50 : 0;

    m_queryService->fetchCurrentPageData(sessionLimit, kDbZoneLimit,
        [this, distFromBottom, runningGames, detectedAt](QueryService::CurrentPageData data) {
            m_rebuildInFlight = false;
            applyCurrentPageData(data, runningGames, detectedAt, distFromBottom);
            if (m_dirty) rebuildDbZones();
        });
}

void CurrentPage::applyCurrentPageData(const QueryService::CurrentPageData &data,
                                        const QList<WindowState> &runningGames,
                                        const QMap<quint32, QString> &detectedAt,
                                        int distFromBottom)
{
    const auto &sessionEvents = data.sessionEvents;
    const auto &zones         = data.zones;

    // --- Session-running card(s) at the top ---
    if (!runningGames.isEmpty()) {
        const bool singleClient = (runningGames.size() == 1);

        auto *container = new QWidget(m_content);
        auto *cl = new QVBoxLayout(container);
        cl->setContentsMargins(0, 0, 0, 0);
        cl->setSpacing(6);

        // Returns the best matching "start" session event for a running game:
        // first tries the most-recent event (primary), then searches for any
        // start event whose installPath is under installDir and whose recorded
        // time is within 60 seconds of the window's reported start time.
        auto findStartEvent = [&](const WindowState &g, const QString &detected)
            -> const Database::SessionEventRecord *
        {
            if (!sessionEvents.isEmpty()
                    && sessionEvents.last().eventType == QLatin1String("start"))
                return &sessionEvents.last();

            if (g.installDir.isEmpty()) return nullptr;
            const QString timeStr = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;
            const QTime winTime   = QTime::fromString(timeStr, "HH:mm");
            if (!winTime.isValid()) return nullptr;

            for (int i = sessionEvents.size() - 1; i >= 0; --i) {
                const auto &ev = sessionEvents[i];
                if (ev.eventType != QLatin1String("start")) continue;
                if (!ev.installPath.startsWith(g.installDir, Qt::CaseInsensitive)) continue;
                const QTime evTime = QTime::fromString(ev.occurredAt.mid(11, 5), "HH:mm");
                if (!evTime.isValid()) continue;
                int diff = qAbs(winTime.secsTo(evTime));
                diff = qMin(diff, 86400 - diff);
                if (diff <= 60) return &ev;
            }
            return nullptr;
        };

        if (singleClient) {
            const auto &g          = runningGames[0];
            const QString detected  = detectedAt.value(g.pid);  // "HH:mm:ss"
            const QString ts        = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;

            auto *card = new NotificationWidget(
                "Game is running", {}, {}, ts, sessionStyle(), container);
            card->setHeaderSuffix("\xc2\xb7 " + g.executableName);

            QList<QPair<QString, QString>> details;
            const auto *ev = findStartEvent(g, detected);
            if (ev) {
                if (!ev->charName.isEmpty()) {
                    QString charInfo = ev->charName;
                    if (!ev->charClass.isEmpty())
                        charInfo += " \xc2\xb7 " + ev->charClass;
                    details.append({"Character", charInfo});
                }
                details.append({"Started", ev->occurredAt});
            } else {
                details.append({"Started", g.startedAt.isEmpty() ? detected : g.startedAt});
            }
            details.append({"PID", QString::number(g.pid)});
            if (!g.installDir.isEmpty())
                details.append({"Folder", g.installDir});
            card->setDetailRows(details);
            cl->addWidget(card);
        } else {
            for (const auto &g : runningGames) {
                const QString detected = detectedAt.value(g.pid);
                const QString ts       = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;
                auto *card = new NotificationWidget(
                    "Game is running", {}, {}, ts, sessionStyle(), container);
                card->setHeaderSuffix("\xc2\xb7 " + g.executableName);
                QList<QPair<QString, QString>> details;
                const auto *ev = findStartEvent(g, detected);
                details.append({"Started", ev ? ev->occurredAt
                                              : (g.startedAt.isEmpty() ? detected : g.startedAt)});
                details.append({"PID", QString::number(g.pid)});
                if (!g.installDir.isEmpty())
                    details.append({"Folder", g.installDir});
                card->setDetailRows(details);
                cl->addWidget(card);
            }
        }

        m_sessionStartCard = container;
        m_contentLayout->insertWidget(1, container);
    }

    // --- Zone (and session-start) cards ---
    m_dbZoneOffset = zones.size();

    if (runningGames.size() > 1) {
        QList<Database::SessionEventRecord> sessionStarts;
        for (const auto &ev : sessionEvents) {
            if (ev.eventType == QLatin1String("start"))
                sessionStarts.append(ev);
        }

        NotificationWidget *lastZoneCard = nullptr;
        int zi = zones.size() - 1;
        int si = 0;

        while (zi >= 0 || si < sessionStarts.size()) {
            const bool haveZone    = zi >= 0;
            const bool haveSession = si < sessionStarts.size();
            const bool takeZone    = haveZone && (!haveSession
                || zones[zi].enteredAt <= sessionStarts[si].occurredAt);

            if (takeZone) {
                const auto &z = zones[zi];
                auto *card = makeZoneCard(z.areaName, z.areaLevel,
                                          z.enteredAt.mid(11, 5), z.durationSecs);
                m_contentLayout->addWidget(card);
                m_dbZoneWidgets.append(card);
                lastZoneCard = card;
                --zi;
            } else {
                const auto &ev = sessionStarts[si];
                auto *card = new NotificationWidget(
                    "Game started", {}, {}, ev.occurredAt.mid(11, 5),
                    sessionStyle(), m_content);
                if (!ev.charName.isEmpty())
                    card->setHeaderSuffix("\xc2\xb7 " + ev.charName);
                QList<QPair<QString, QString>> details;
                details.append({"Time", ev.occurredAt});
                if (!ev.charName.isEmpty()) {
                    QString charInfo = ev.charName;
                    if (!ev.charClass.isEmpty())
                        charInfo += " \xc2\xb7 " + ev.charClass;
                    details.append({"Character", charInfo});
                }
                if (!ev.installPath.isEmpty())
                    details.append({"Install", ev.installPath});
                card->setDetailRows(details);
                m_contentLayout->addWidget(card);
                m_dbZoneWidgets.append(card);
                ++si;
            }
        }

        if (!zones.isEmpty() && zones[0].durationSecs < 0)
            m_prevZoneCard = lastZoneCard;
    } else {
        for (int i = zones.size() - 1; i >= 0; --i) {
            const auto &z = zones[i];
            appendDbZone(makeZoneCard(z.areaName, z.areaLevel,
                                      z.enteredAt.mid(11, 5), z.durationSecs));
        }
        if (!zones.isEmpty() && zones[0].durationSecs < 0)
            m_prevZoneCard = m_dbZoneWidgets.last();
    }

    setLoadMoreVisible(zones.size() == kDbZoneLimit);

    m_pendingScrollTo = (distFromBottom <= 4) ? 0 : distFromBottom;
    m_scroll->setUpdatesEnabled(true);
}

void CurrentPage::onLoadMore()
{
    if (!m_queryService || m_loadMoreInFlight) return;

    const int prevMax    = m_scroll->verticalScrollBar()->maximum();
    const int prevValue  = m_scroll->verticalScrollBar()->value();
    const int fetchOffset = m_dbZoneOffset;

    m_loadMoreInFlight = true;
    m_loadMoreBtn->setEnabled(false);

    m_queryService->fetchZoneTransitions(kDbZoneLimit, fetchOffset,
        [this, prevMax, prevValue](QList<Database::ZoneTransitionRecord> zones) {
            m_loadMoreInFlight = false;
            m_dbZoneOffset += zones.size();

            // Insert older zones right after the load-more button.
            const int btnIdx    = m_contentLayout->indexOf(m_loadMoreBtn);
            const int basePos   = m_sessionStartCard ? 2 : 1;
            const int insertPos = btnIdx >= 0 ? btnIdx + 1 : basePos;
            for (const auto &z : zones) {
                const QString ts = z.enteredAt.mid(11, 5);
                auto *card = makeZoneCard(z.areaName, z.areaLevel, ts, z.durationSecs);
                m_contentLayout->insertWidget(insertPos, card);
                m_dbZoneWidgets.append(card);
            }

            setLoadMoreVisible(zones.size() == kDbZoneLimit);
            if (zones.size() == kDbZoneLimit)
                m_loadMoreBtn->setEnabled(true);

            QTimer::singleShot(0, this, [this, prevMax, prevValue]() {
                const int delta = m_scroll->verticalScrollBar()->maximum() - prevMax;
                m_scroll->verticalScrollBar()->setValue(prevValue + delta);
            });
        });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

NotificationWidget *CurrentPage::makeZoneCard(const QString &areaName, int areaLevel,
                                               const QString &timestamp, int durationSecs)
{
    const QString tag = areaLevel > 0 ? QStringLiteral("lv %1").arg(areaLevel) : QString{};
    auto *card = new NotificationWidget(areaName, tag, {}, timestamp, zoneStyle(), m_content);
    if (durationSecs > 0)
        card->setHeaderSuffix("\xc2\xb7 " + formatDuration(durationSecs));
    return card;
}

void CurrentPage::appendDbZone(NotificationWidget *card)
{
    m_contentLayout->addWidget(card);
    m_dbZoneWidgets.append(card);
}

void CurrentPage::setLoadMoreVisible(bool visible)
{
    const int idx = m_contentLayout->indexOf(m_loadMoreBtn);
    if (visible && idx < 0) {
        // Insert after session card container (pos 2) if present, else after stretch (pos 1).
        const int pos = m_sessionStartCard ? 2 : 1;
        m_contentLayout->insertWidget(pos, m_loadMoreBtn);
        m_loadMoreBtn->show();
    } else if (!visible && idx >= 0) {
        m_contentLayout->removeWidget(m_loadMoreBtn);
        m_loadMoreBtn->hide();
    }
}

void CurrentPage::appendLiveWidget(QWidget *w)
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;

    if (m_liveEventWidgets.size() >= kLiveWidgetCap) {
        QWidget *oldest = m_liveEventWidgets.takeFirst();
        m_contentLayout->removeWidget(oldest);
        delete oldest;
    }

    m_contentLayout->addWidget(w);
    m_liveEventWidgets.append(w);
    m_contentLayout->activate();

    if (atBottom)
        QTimer::singleShot(0, this, &CurrentPage::scrollToBottom);
}

void CurrentPage::scrollToBottom()
{
    m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void CurrentPage::onScrollRangeChanged(int, int max)
{
    // Called only on rangeChanged (layout settling), never on user scroll.
    // Re-apply the pending scroll target on every pass so that multi-pass
    // layout (e.g. scrollbar appearance causing a reflow) doesn't leave the
    // view at a wrong intermediate position.
    if (m_pendingScrollTo >= 0 && max > 0) {
        const int target = (m_pendingScrollTo == 0)
            ? max
            : qMax(0, max - m_pendingScrollTo);
        m_scroll->verticalScrollBar()->setValue(target);
        // Restart the settle timer: clear pending 100ms after the last
        // rangeChanged so the layout is definitely finished by then.
        m_scrollSettleTimer->start();
    }
    updateScrollDownBtn();
}

void CurrentPage::updateScrollDownBtn()
{
    const auto *sb = m_scroll->verticalScrollBar();
    const bool atBottom = sb->value() >= sb->maximum() - 4;
    m_scrollDownBtn->setVisible(!atBottom);
}
