// src/CurrentPage.cpp (C++)

#include "ui/log/CurrentPage.h"
#include "db/Database.h"
#include "util/Docs.h"
#include "events/LiveEvent.h"
#include "events/LiveEventBus.h"
#include "db/QueryService.h"
#include "ui/widgets/ScrollJumpButton.h"
#include "ui/Theme.h"

#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

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

static NotificationStyle clientScreenStyle()
{
  NotificationStyle s;
  s.accentColor = QColor(160, 130, 95);
  return s;
}

static NotificationStyle altTabStyle()
{
  NotificationStyle s;
  s.accentColor = QColor(110, 110, 130);
  return s;
}

static NotificationStyle afkStyle()
{
  NotificationStyle s;
  s.accentColor = QColor(120, 100, 160);
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
  connect(vsb, &QScrollBar::valueChanged, this, [this](int)
          { updateScrollDownBtn(); });

  // Clears m_pendingScrollTo 100ms after the last rangeChanged, by which
  // point the layout cascade is definitely finished.
  m_scrollSettleTimer = new QTimer(this);
  m_scrollSettleTimer->setSingleShot(true);
  m_scrollSettleTimer->setInterval(100);
  connect(m_scrollSettleTimer, &QTimer::timeout, this, [this]()
          { m_pendingScrollTo = -1; });

  connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
          this, &CurrentPage::onLiveEvent);
}

void CurrentPage::setQueryService(QueryService *qs)
{
  m_queryService = qs;
  m_dirty = true;
}

void CurrentPage::markDirty()
{
  m_dirty = true;
  if (isVisible() && m_queryService)
    rebuildDbZones();
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
  if (games.isEmpty() && m_sessionStartCard)
  {
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
  m_scrollDownBtn->move(rect().right() - m_scrollDownBtn->width() - Theme::spacing3xl,
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
  if (bulk)
  {
    m_prevZoneCard = nullptr;
    m_dirty = true;
    if (isVisible() && m_queryService)
      rebuildDbZones();
    return;
  }

  if (event.type == LiveEventType::AreaEntered)
  {
    const QString areaName = event.data.value("area_name").toString();
    const QString areaCode = event.data.value("area_code").toString();
    const QString areaType = event.data.value("area_type").toString();
    const QString areaSubtype = event.data.value("area_subtype").toString();
    const int areaLevel = event.data.value("area_level").toInt();

    // Stamp the previous zone's card with the time spent there. The ingest worker
    // closes span N-1 before emitting AreaEntered for span N, so the two most
    // recent transitions are [new zone, previous zone].
    if (m_prevZoneCard && m_queryService)
    {
      QPointer<NotificationWidget> prevCard = m_prevZoneCard;
      m_queryService->fetchZoneTransitions(2, 0,
                                           [prevCard](QList<Database::ZoneTransitionRecord> zones)
                                           {
                                             if (!prevCard) return; // widget deleted by a rebuild before callback fired
                                             if (zones.size() >= 2 && zones[1].durationSecs > 0)
                                             {
                                               const QString dur = "\xc2\xb7 " + formatDuration(zones[1].durationSecs);
                                               if (zones[1].areaType.isEmpty())
                                                 prevCard->setHeaderSuffix(dur);
                                               else
                                                 prevCard->setHeaderSuffix("entered " + dur);
                                             }
                                           });
    }

    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    auto *card = makeZoneCard(areaName, areaCode, areaType, areaSubtype, areaLevel, ts, -1);
    appendLiveWidget(card);
    m_prevZoneCard = card;
  }
  else if (event.type == LiveEventType::LoginScreen)
  {
    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    auto *card = new NotificationWidget("Login screen", {}, {}, ts, clientScreenStyle(), m_content);
    card->setLeadingIcon(QStringLiteral(":/icons/box-arrow-in-right.svg"), QColor(160, 130, 95), 20);
    appendLiveWidget(card);
    m_prevZoneCard = nullptr; // player left a zone; don't stamp it on the next area entry
  }
  else if (event.type == LiveEventType::CharSelect)
  {
    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    auto *card = new NotificationWidget("Character select", {}, {}, ts, clientScreenStyle(), m_content);
    card->setLeadingIcon(QStringLiteral(":/icons/person-fill.svg"), QColor(160, 130, 95), 20);
    appendLiveWidget(card);
  }
  else if (event.type == LiveEventType::SessionStart)
  {
    m_dirty = true;
    if (isVisible() && m_queryService)
      rebuildDbZones();
  }
  else if (event.type == LiveEventType::AltTabOut)
  {
    // If a DB rebuild already shows this exact pending record, skip the live
    // card — it would appear on top of the DB card as a duplicate.
    if (event.timestamp == m_dbAltTabOutTs)
      return;
    auto *card = new NotificationWidget("Alt-Tab", {}, {},
        event.timestamp.mid(11, 5), altTabStyle(), m_content);
    card->setLeadingIcon(QStringLiteral(":/icons/indent.svg"), QColor(110, 110, 130), 20);
    appendLiveWidget(card);
  }
  else if (event.type == LiveEventType::AltTabBack)
  {
    // Don't rebuild immediately — the natural setRunningGames cycle will pick up
    // the completed record with its duration from the DB within a second or two.
    m_dirty = true;
  }
}

// ---------------------------------------------------------------------------
// DB zone section
// ---------------------------------------------------------------------------

void CurrentPage::rebuildDbZones()
{
  if (!m_queryService)
    return;
  if (m_rebuildInFlight)
  {
    m_dirty = true;
    return;
  }
  m_dirty = false;
  m_rebuildInFlight = true;

  // Capture scroll state and running-games snapshot before the async gap.
  const auto *sb = m_scroll->verticalScrollBar();
  const int prevMax = sb->maximum();
  const int distFromBottom = prevMax > 0 ? (prevMax - sb->value()) : -1;

  const QList<WindowState> runningGames = m_runningGames;
  const QMap<quint32, QString> detectedAt = m_detectedAt;

  // Determine how many session events we need:
  //   single client: 10 (search for matching start event by folder+time)
  //   multi-client:  50 (interleaved with zones)
  //   no games:       0
  const int sessionLimit = runningGames.size() == 1  ? 10
                           : runningGames.size() > 1 ? 50
                                                     : 0;
  const int zoneLimit = runningGames.isEmpty() ? 0 : kDbZoneLimit;

  m_queryService->fetchCurrentPageData(sessionLimit, zoneLimit,
                                       [this, distFromBottom, runningGames, detectedAt](QueryService::CurrentPageData data)
                                       {
                                         m_rebuildInFlight = false;
                                         applyCurrentPageData(data, runningGames, detectedAt, distFromBottom);
                                         if (m_dirty)
                                           rebuildDbZones();
                                       });
}

void CurrentPage::applyCurrentPageData(const QueryService::CurrentPageData &data,
                                       const QList<WindowState> &runningGames,
                                       const QMap<quint32, QString> &detectedAt,
                                       int distFromBottom)
{
  const auto &sessionEvents = data.sessionEvents;
  const auto &zones = data.zones;

  // Clear existing widgets right before replacing them. Old content remains
  // visible during the async DB fetch so WM_PAINT never sees a blank area.
  m_scroll->setUpdatesEnabled(false);

  for (QWidget *w : m_liveEventWidgets)
  {
    m_contentLayout->removeWidget(w);
    delete w;
  }
  m_liveEventWidgets.clear();

  if (m_sessionStartCard)
  {
    m_contentLayout->removeWidget(m_sessionStartCard);
    delete m_sessionStartCard;
    m_sessionStartCard = nullptr;
  }

  for (NotificationWidget *w : m_dbZoneWidgets)
  {
    m_contentLayout->removeWidget(w);
    delete w;
  }
  m_dbZoneWidgets.clear();
  m_prevZoneCard = nullptr;
  m_dbZoneOffset = 0;
  m_dbAltTabOutTs.clear();

  setLoadMoreVisible(false);

  // --- Session-running card(s) at the top ---
  if (!runningGames.isEmpty())
  {
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
      if (!sessionEvents.isEmpty() && sessionEvents.last().eventType == QLatin1String("start"))
        return &sessionEvents.last();

      if (g.installDir.isEmpty())
        return nullptr;
      const QString timeStr = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;
      const QTime winTime = QTime::fromString(timeStr, "HH:mm");
      if (!winTime.isValid())
        return nullptr;

      for (int i = sessionEvents.size() - 1; i >= 0; --i)
      {
        const auto &ev = sessionEvents[i];
        if (ev.eventType != QLatin1String("start"))
          continue;
        if (!ev.installPath.startsWith(g.installDir, Qt::CaseInsensitive))
          continue;
        const QTime evTime = QTime::fromString(ev.occurredAt.mid(11, 5), "HH:mm");
        if (!evTime.isValid())
          continue;
        int diff = qAbs(winTime.secsTo(evTime));
        diff = qMin(diff, 86400 - diff);
        if (diff <= 60)
          return &ev;
      }
      return nullptr;
    };

    if (singleClient)
    {
      const auto &g = runningGames[0];
      const QString detected = detectedAt.value(g.pid); // "HH:mm:ss"
      const QString ts = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;

      auto *card = new NotificationWidget(
          "Game is running", {}, {}, ts, sessionStyle(), container);
      card->setHeaderSuffix("\xc2\xb7 " + g.executableName);
      card->setSource(docSource("OS, Client.txt", "sources/game-running"));

      QList<QPair<QString, QString>> details;
      const auto *ev = findStartEvent(g, detected);
      if (ev)
      {
        if (!ev->charName.isEmpty())
        {
          QString charInfo = ev->charName;
          if (!ev->charClass.isEmpty())
            charInfo += " \xc2\xb7 " + ev->charClass;
          details.append({"Character", charInfo});
        }
        details.append({"Started", ev->occurredAt});
      }
      else
      {
        details.append({"Started", g.startedAt.isEmpty() ? detected : g.startedAt});
      }
      details.append({"PID", QString::number(g.pid)});
      if (!g.installDir.isEmpty())
        details.append({"Folder", g.installDir});
      card->setDetailRows(details);
      cl->addWidget(card);
    }
    else
    {
      for (const auto &g : runningGames)
      {
        const QString detected = detectedAt.value(g.pid);
        const QString ts = g.startedAt.isEmpty() ? detected.left(5) : g.startedAt;
        auto *card = new NotificationWidget(
            "Game is running", {}, {}, ts, sessionStyle(), container);
        card->setHeaderSuffix("\xc2\xb7 " + g.executableName);
        card->setSource(docSource("OS, Client.txt", "sources/game-running"));
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

  if (runningGames.size() > 1)
  {
    QList<Database::SessionEventRecord> sessionStarts;
    for (const auto &ev : sessionEvents)
    {
      if (ev.eventType == QLatin1String("start"))
        sessionStarts.append(ev);
    }

    const auto &afkMc = data.afkRecords;   // newest-first
    const auto &atMc  = data.altTabRecords; // newest-first
    NotificationWidget *lastZoneCard = nullptr;
    int zi   = zones.size() - 1;
    int si   = 0;
    int ai   = afkMc.size() - 1;
    int ati  = atMc.size() - 1;

    while (zi >= 0 || si < (int)sessionStarts.size() || ai >= 0 || ati >= 0)
    {
      const QString zTs  = (zi  >= 0) ? zones[zi].enteredAt                              : QString{};
      const QString sTs  = (si  < (int)sessionStarts.size()) ? sessionStarts[si].occurredAt : QString{};
      const QString aTs  = (ai  >= 0) ? afkMc[ai].afkOnAt                                : QString{};
      const QString atTs = (ati >= 0) ? atMc[ati].outAt                                  : QString{};

      // Zones before session-starts before AFK before alt-tab when tied.
      const bool takeZone    = !zTs.isEmpty()  && (sTs.isEmpty()  || zTs <= sTs)  && (aTs.isEmpty()  || zTs <= aTs)  && (atTs.isEmpty() || zTs <= atTs);
      const bool takeSession = !takeZone   && !sTs.isEmpty()  && (aTs.isEmpty()  || sTs <= aTs)  && (atTs.isEmpty() || sTs <= atTs);
      const bool takeAfkMc   = !takeZone   && !takeSession && !aTs.isEmpty()  && (atTs.isEmpty() || aTs <= atTs);

      if (takeZone)
      {
        const auto &z = zones[zi];
        auto *card = makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype,
                                  z.areaLevel, z.enteredAt.mid(11, 5), z.durationSecs);
        m_contentLayout->addWidget(card);
        m_dbZoneWidgets.append(card);
        lastZoneCard = card;
        --zi;
      }
      else if (takeSession)
      {
        const auto &ev = sessionStarts[si];
        auto *card = new NotificationWidget(
            "Game started", {}, {}, ev.occurredAt.mid(11, 5),
            sessionStyle(), m_content);
        if (!ev.charName.isEmpty())
          card->setHeaderSuffix("\xc2\xb7 " + ev.charName);
        card->setSource(docSource("Client.txt", "sources/game-started"));
        QList<QPair<QString, QString>> details;
        details.append({"Time", ev.occurredAt});
        if (!ev.charName.isEmpty())
        {
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
      else if (takeAfkMc)
      {
        const auto &a = afkMc[ai];
        auto *card = new NotificationWidget("AFK", {}, {}, a.afkOnAt.mid(11, 5), afkStyle(), m_content);
        card->setLeadingIcon(QStringLiteral(":/icons/stopwatch-fill.svg"), QColor(120, 100, 160), 20);
        if (a.durationSecs > 0)
          card->setHeaderSuffix("\xc2\xb7 " + formatDuration(a.durationSecs));
        m_contentLayout->addWidget(card);
        m_dbZoneWidgets.append(card);
        --ai;
      }
      else
      {
        const auto &r = atMc[ati];
        auto *card = new NotificationWidget("Alt-Tab", {}, {}, r.outAt.mid(11, 5), altTabStyle(), m_content);
        card->setLeadingIcon(QStringLiteral(":/icons/indent.svg"), QColor(110, 110, 130), 20);
        if (r.durationSecs > 0)
          card->setHeaderSuffix("\xc2\xb7 " + formatDuration(r.durationSecs));
        else {
          m_dbAltTabOutTs = r.outAt;
          qDebug() << "[CurrentPage] pending alt-tab card (no duration), out_at=" << r.outAt;
        }
        m_contentLayout->addWidget(card);
        m_dbZoneWidgets.append(card);
        --ati;
      }
    }

    if (!zones.isEmpty() && zones[0].durationSecs < 0)
      m_prevZoneCard = lastZoneCard;
  }
  else
  {
    const auto &cse = data.clientScreenEvents; // newest-first
    const auto &afk = data.afkRecords;          // newest-first
    const auto &at  = data.altTabRecords;        // newest-first
    NotificationWidget *lastZoneCard = nullptr;
    int zi  = zones.size() - 1;                  // walk oldest→newest
    int ci  = cse.size() - 1;
    int ai  = afk.size() - 1;
    int ati = at.size() - 1;

    while (zi >= 0 || ci >= 0 || ai >= 0 || ati >= 0)
    {
      const QString zTs  = (zi  >= 0) ? zones[zi].enteredAt   : QString{};
      const QString cTs  = (ci  >= 0) ? cse[ci].occurredAt    : QString{};
      const QString aTs  = (ai  >= 0) ? afk[ai].afkOnAt       : QString{};
      const QString atTs = (ati >= 0) ? at[ati].outAt          : QString{};

      // Pick the oldest event; tie-break: zones > screen > AFK > alt-tab.
      const bool takeZone   = !zTs.isEmpty()   && (cTs.isEmpty()  || zTs <= cTs)  && (aTs.isEmpty()  || zTs <= aTs)  && (atTs.isEmpty() || zTs <= atTs);
      const bool takeScreen = !takeZone   && !cTs.isEmpty()  && (aTs.isEmpty()  || cTs <= aTs)  && (atTs.isEmpty() || cTs <= atTs);
      const bool takeAfk    = !takeZone   && !takeScreen && !aTs.isEmpty()  && (atTs.isEmpty() || aTs <= atTs);

      if (takeZone)
      {
        const auto &z = zones[zi];
        auto *card = makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype,
                                  z.areaLevel, z.enteredAt.mid(11, 5), z.durationSecs);
        appendDbZone(card);
        lastZoneCard = card;
        --zi;
      }
      else if (takeScreen)
      {
        const auto &ev = cse[ci];
        const bool isLogin = ev.eventType == QLatin1String("login_screen");
        auto *card = new NotificationWidget(
            isLogin ? "Login screen" : "Character select",
            {}, {}, ev.occurredAt.mid(11, 5),
            clientScreenStyle(), m_content);
        if (isLogin)
          card->setLeadingIcon(QStringLiteral(":/icons/box-arrow-in-right.svg"),
                               QColor(160, 130, 95), 20);
        else
          card->setLeadingIcon(QStringLiteral(":/icons/person-fill.svg"),
                               QColor(160, 130, 95), 20);
        card->setSource(docSource("Client.txt", "sources/zone-transition"));
        appendDbZone(card);
        --ci;
      }
      else if (takeAfk)
      {
        const auto &a = afk[ai];
        auto *card = new NotificationWidget("AFK", {}, {}, a.afkOnAt.mid(11, 5), afkStyle(), m_content);
        card->setLeadingIcon(QStringLiteral(":/icons/stopwatch-fill.svg"), QColor(120, 100, 160), 20);
        if (a.durationSecs > 0)
          card->setHeaderSuffix("\xc2\xb7 " + formatDuration(a.durationSecs));
        appendDbZone(card);
        --ai;
      }
      else
      {
        const auto &r = at[ati];
        auto *card = new NotificationWidget("Alt-Tab", {}, {}, r.outAt.mid(11, 5), altTabStyle(), m_content);
        card->setLeadingIcon(QStringLiteral(":/icons/indent.svg"), QColor(110, 110, 130), 20);
        if (r.durationSecs > 0)
          card->setHeaderSuffix("\xc2\xb7 " + formatDuration(r.durationSecs));
        else {
          m_dbAltTabOutTs = r.outAt;
          qDebug() << "[CurrentPage] pending alt-tab card (no duration), out_at=" << r.outAt;
        }
        appendDbZone(card);
        --ati;
      }
    }

    if (!zones.isEmpty() && zones[0].durationSecs < 0)
      m_prevZoneCard = lastZoneCard;
  }

  m_pendingScrollTo = (distFromBottom <= 4) ? 0 : distFromBottom;
  m_contentLayout->activate();
  m_scroll->setUpdatesEnabled(true);
}

void CurrentPage::onLoadMore()
{
  if (!m_queryService || m_loadMoreInFlight)
    return;

  const int prevMax = m_scroll->verticalScrollBar()->maximum();
  const int prevValue = m_scroll->verticalScrollBar()->value();
  const int fetchOffset = m_dbZoneOffset;

  m_loadMoreInFlight = true;
  m_loadMoreBtn->setEnabled(false);

  m_queryService->fetchZoneTransitions(kDbZoneLimit, fetchOffset,
                                       [this, prevMax, prevValue](QList<Database::ZoneTransitionRecord> zones)
                                       {
                                         m_loadMoreInFlight = false;
                                         m_dbZoneOffset += zones.size();

                                         // Insert older zones right after the load-more button.
                                         const int btnIdx = m_contentLayout->indexOf(m_loadMoreBtn);
                                         const int basePos = m_sessionStartCard ? 2 : 1;
                                         const int insertPos = btnIdx >= 0 ? btnIdx + 1 : basePos;
                                         for (const auto &z : zones)
                                         {
                                           const QString ts = z.enteredAt.mid(11, 5);
                                           auto *card = makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype, z.areaLevel, ts, z.durationSecs);
                                           m_contentLayout->insertWidget(insertPos, card);
                                           m_dbZoneWidgets.append(card);
                                         }

                                         setLoadMoreVisible(zones.size() == kDbZoneLimit);
                                         if (zones.size() == kDbZoneLimit)
                                           m_loadMoreBtn->setEnabled(true);

                                         QTimer::singleShot(0, this, [this, prevMax, prevValue]()
                                                            {
                const int delta = m_scroll->verticalScrollBar()->maximum() - prevMax;
                m_scroll->verticalScrollBar()->setValue(prevValue + delta); });
                                       });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QColor zoneAccent(const QString &areaType, const QString &areaSubtype, const QString &areaCode)
{
    // locked = hue fixed by in-game icon or explicit user constraint; free = arbitrary, adjust as needed
    if (areaType == QLatin1String("Hideout"))                               return {100, 170, 215}; // locked — blue leaf on blue circle (icon)
    if (areaSubtype == QLatin1String("Town"))                               return {215, 210, 70};  // locked — campaign yellow (user)
    if (areaType.contains(QLatin1String("Vaal side area"))) {
        if (areaType.startsWith(QLatin1String("Map")))                      return {100, 70,  150}; // locked — darker Map shade (user)
        return {150, 120, 45};                                                                      // locked — darker campaign shade (user)
    }
    if (areaType.startsWith(QLatin1String("Act")))                          return {210, 172, 65};  // locked — campaign (user); warm gold distinct from bright town yellow
    if (areaType == QLatin1String("Map"))                                   return {155, 110, 210}; // semi-locked — hue free but must stay distinctive (user)
    if (areaType == QLatin1String("Heist"))                                 return {75,  195, 185}; // free
    if (areaType == QLatin1String("Lab"))                                   return {90,  190, 115}; // locked — some kind of green (user)
    if (areaType == QLatin1String("Boss Arena"))                            return {210, 110, 80};  // free
    if (areaType == QLatin1String("PvP"))                                   return {210, 145, 75};  // free
    if (areaType == QLatin1String("Mechanic")) {
        if (areaCode.startsWith(QLatin1String("Sanctum")))                  return {170, 135, 40};  // locked — gold doors (icon); dark yellow, brighter shade passed to Acts
        if (areaCode == QLatin1String("Delve_Main"))                        return {40,  90,  160}; // locked — blue circle (icon); dark blue
        if (areaCode == QLatin1String("ChayulaLeague"))                     return {210, 110, 155}; // locked — pink hand on dark pink circle (icon)
        if (areaCode == QLatin1String("HeistHub"))                          return {200, 152, 58};  // semi-locked — icon is gold on red; amber splits the difference
        if (areaCode == QLatin1String("Menagerie_Hub"))                     return {205, 75,  110}; // locked — some kind of red (user) + red circle (icon); rose to avoid clash with Vaal
        if (areaCode == QLatin1String("KalguuranSettlersLeague"))           return {145, 158, 172}; // locked — silver glyph on dark blue (icon)
        if (areaCode == QLatin1String("Labyrinth_Airlock"))                 return {90,  195, 110}; // locked — green face on green circle (icon)
        return {135, 135, 155};                                                                     // free
    }
    return {100, 170, 215};
}

NotificationWidget *CurrentPage::makeZoneCard(const QString &areaName, const QString &areaCode,
                                              const QString &areaType, const QString &areaSubtype,
                                              int areaLevel, const QString &timestamp,
                                              int durationSecs)
{
  const bool showTag = areaLevel > 0 && areaType != QLatin1String("Hideout") && areaType != QLatin1String("Mechanic") && areaSubtype != QLatin1String("Town");
  const QString tag = showTag ? QStringLiteral("lv %1").arg(areaLevel) : QString{};
  if (!areaType.isEmpty())
  {
    const QString typeLabel = areaSubtype.isEmpty()
                                  ? areaType + ":"
                                  : areaType + " \xc2\xb7 " + areaSubtype + ":";
    const QColor accent = zoneAccent(areaType, areaSubtype, areaCode);
    NotificationStyle style;
    style.accentColor = accent;
    auto *card = new NotificationWidget(typeLabel, {}, {}, timestamp, style, m_content);
    card->setAreaName(areaName);
    card->setHeaderSuffix(durationSecs > 0 ? "entered \xc2\xb7 " + formatDuration(durationSecs) : "entered.");
    if (!tag.isEmpty())
      card->appendTopRowTag(tag);
    static const QHash<QString, QString> kMechanicIcons = {
        {"ChayulaLeague", ":/icons/tree-fill.svg"},
        {"Delve_Main", ":/icons/minecart-loaded.svg"},
        {"KalguuranSettlersLeague", ":/icons/coin.svg"},
        {"Labyrinth_Airlock", ":/icons/qr-code.svg"},
        {"SanctumFoyer_Fellshrine", ":/icons/door-open-fill.svg"},
        {"SanctumCellar", ":/icons/bullseye.svg"},
        {"SanctumNave", ":/icons/bullseye.svg"},
        {"SanctumCrypt", ":/icons/bullseye.svg"},
        {"SanctumVaults", ":/icons/bullseye.svg"},
        {"Menagerie_Hub", ":/icons/bug-fill.svg"},
        {"HeistHub", ":/icons/safe2-fill.svg"},
    };
    if (areaSubtype == QLatin1String("Town"))
      card->setLeadingIcon(QStringLiteral(":/icons/shop.svg"), accent, 20);
    else if (areaType == QLatin1String("Hideout"))
      card->setLeadingIcon(QStringLiteral(":/icons/house-fill.svg"), accent, 20);
    else if (areaType == QLatin1String("Map"))
      card->setLeadingIcon(QStringLiteral(":/icons/map-fill.svg"), accent, 20);
    else if (areaType == QLatin1String("Heist"))
      card->setLeadingIcon(QStringLiteral(":/icons/alarm-fill.svg"), accent, 20);
    else if (areaType.startsWith(QLatin1String("Act")) && areaSubtype == QLatin1String("nowp"))
      card->setLeadingIcon(QStringLiteral(":/icons/geo.svg"), accent, 20);
    else if (areaType.startsWith(QLatin1String("Act")) && areaSubtype.isEmpty())
      card->setLeadingIcon(QStringLiteral(":/icons/geo-fill.svg"), accent, 20);
    else if (auto it = kMechanicIcons.find(areaCode); it != kMechanicIcons.end())
      card->setLeadingIcon(*it, accent, 20);
    card->setSource(docSource("Client.txt", "sources/zone-transition"));
    return card;
  }
  auto *card = new NotificationWidget(areaName, tag, {}, timestamp, zoneStyle(), m_content);
  if (durationSecs > 0)
    card->setHeaderSuffix("\xc2\xb7 " + formatDuration(durationSecs));
  card->setSource(docSource("Client.txt", "sources/zone-transition"));
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
  if (visible && idx < 0)
  {
    // Insert after session card container (pos 2) if present, else after stretch (pos 1).
    const int pos = m_sessionStartCard ? 2 : 1;
    m_contentLayout->insertWidget(pos, m_loadMoreBtn);
    m_loadMoreBtn->show();
  }
  else if (!visible && idx >= 0)
  {
    m_contentLayout->removeWidget(m_loadMoreBtn);
    m_loadMoreBtn->hide();
  }
}

void CurrentPage::appendLiveWidget(QWidget *w)
{
  const auto *sb = m_scroll->verticalScrollBar();
  const bool atBottom = sb->value() >= sb->maximum() - 4;

  m_scroll->setUpdatesEnabled(false);

  if (m_liveEventWidgets.size() >= kLiveWidgetCap)
  {
    QWidget *oldest = m_liveEventWidgets.takeFirst();
    m_contentLayout->removeWidget(oldest);
    delete oldest;
  }

  m_contentLayout->addWidget(w);
  m_liveEventWidgets.append(w);

  if (!m_rebuildInFlight)
  {
    m_contentLayout->activate();
    m_scroll->setUpdatesEnabled(true);
  }

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
  if (m_pendingScrollTo >= 0 && max > 0)
  {
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
