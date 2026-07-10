// src/SessionViewPage.cpp (C++)

#include "ui/log/SessionViewPage.h"
#include "ui/log/SessionQueryLimits.h"
#include "ui/log/ZoneAfkSuffix.h"
#include "services/PoeInfoRecords.h"
#include "util/Docs.h"
#include "events/LiveEvent.h"
#include "events/LiveEventBus.h"
#include "services/PoeInfoClient.h"
#include "ui/widgets/ScrollJumpButton.h"
#include "ui/Theme.h"

#include <QJsonArray>
#include <QJsonObject>

#include <QDateTime>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

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

SessionViewPage::SessionViewPage(QWidget *parent)
    : QWidget(parent)
{
  // ---- Sub-bar header (shown only when navigated via View) ----------------
  auto *backBtn = new QPushButton("\xe2\x86\x90 Back", this);
  backBtn->setFlat(true);
  connect(backBtn, &QPushButton::clicked, this, &SessionViewPage::backRequested);

  m_sessionLabel = new QLabel(this);
  m_sessionLabel->setAlignment(Qt::AlignCenter);
  {
      QPalette pal = m_sessionLabel->palette();
      pal.setColor(QPalette::WindowText, QColor(180, 180, 180));
      m_sessionLabel->setPalette(pal);
  }

  m_headerBar = new QWidget(this);
  auto *headerBox = new QHBoxLayout(m_headerBar);
  headerBox->setContentsMargins(Theme::spacingXs, Theme::spacingXs, Theme::spacingXs, Theme::spacingXs);
  headerBox->setSpacing(Theme::spacingSm);
  headerBox->addWidget(backBtn);
  headerBox->addWidget(m_sessionLabel, 1);

  m_headerSep = new QFrame(this);
  static_cast<QFrame *>(m_headerSep)->setFrameShape(QFrame::HLine);
  static_cast<QFrame *>(m_headerSep)->setFrameShadow(QFrame::Sunken);

  m_headerBar->setVisible(false);
  m_headerSep->setVisible(false);

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
  connect(m_loadMoreBtn, &QPushButton::clicked, this, &SessionViewPage::onLoadMore);

  // Layout: [stretch] [session card(s) container] [load-more btn] [db zones] [live events]
  m_contentLayout->addStretch(1);
  m_loadMoreBtn->hide();

  m_scroll->setWidget(m_content);

  auto *vbox = new QVBoxLayout(this);
  vbox->setContentsMargins(0, 0, 0, 0);
  vbox->setSpacing(0);
  vbox->addWidget(m_headerBar);
  vbox->addWidget(m_headerSep);
  vbox->addWidget(m_scroll, 1);

  m_scrollDownBtn = new ScrollJumpButton(this);
  m_scrollDownBtn->hide();
  m_scrollDownBtn->raise();
  connect(m_scrollDownBtn, &QPushButton::clicked, this, &SessionViewPage::scrollToBottom);

  auto *vsb = m_scroll->verticalScrollBar();
  connect(vsb, &QScrollBar::rangeChanged, this, &SessionViewPage::onScrollRangeChanged);
  connect(vsb, &QScrollBar::valueChanged, this, [this](int)
          { updateScrollDownBtn(); });

  m_scrollSettleTimer = new QTimer(this);
  m_scrollSettleTimer->setSingleShot(true);
  m_scrollSettleTimer->setInterval(100);
  connect(m_scrollSettleTimer, &QTimer::timeout, this, [this]()
          { m_pendingScrollTo = -1; });

  // Loading overlay — shown over the scroll area until the first data arrives.
  m_loadingOverlay = new QLabel("Loading data, please stand by...", this);
  m_loadingOverlay->setAlignment(Qt::AlignCenter);
  {
      QPalette pal = m_loadingOverlay->palette();
      pal.setColor(QPalette::WindowText, Theme::textPlaceholder);
      m_loadingOverlay->setPalette(pal);
  }
  m_loadingOverlay->hide();

  connect(LiveEventBus::instance(), &LiveEventBus::eventFired,
          this, &SessionViewPage::onLiveEvent);
}

QWidget *SessionViewPage::scrollViewport() const
{
  return m_scroll->viewport();
}

void SessionViewPage::setPoeInfoClient(PoeInfoClient *client)
{
  m_poeInfoClient = client;
  connect(client, &PoeInfoClient::connected, this, [this] {
    if (isVisible()) rebuildDbZones();
    else m_dirty = true;
  });
  m_dirty = true;
  triggerLoadIfNeeded();
}

void SessionViewPage::triggerLoadIfNeeded()
{
  if (m_dirty && m_poeInfoClient && isVisible()) {
    m_loadingOverlay->setGeometry(m_scroll->geometry());
    m_loadingOverlay->show();
    m_loadingOverlay->raise();
    if (m_poeInfoClient->isConnected()) {
      QTimer::singleShot(0, this, [this] {
        if (m_dirty && m_poeInfoClient && m_poeInfoClient->isConnected()) rebuildDbZones();
      });
    }
    // If not connected: overlay stays; rebuildDbZones fires on connected() signal.
  }
}

void SessionViewPage::showEvent(QShowEvent *e)
{
  QWidget::showEvent(e);
  triggerLoadIfNeeded();
}

void SessionViewPage::markDirty()
{
  m_dirty = true;
  if (isVisible() && m_poeInfoClient && m_poeInfoClient->isConnected())
    rebuildDbZones();
}

void SessionViewPage::preload()
{
  if (!m_dirty || !m_poeInfoClient || !m_poeInfoClient->isConnected() || m_rebuildInFlight) return;
  QTimer::singleShot(0, this, [this] {
    if (m_dirty && m_poeInfoClient && m_poeInfoClient->isConnected() && !isVisible()) rebuildDbZones();
  });
}

void SessionViewPage::preloadSession(qint64 sessionId, const QString &startedAt)
{
  if (isVisible() || !m_poeInfoClient || !m_poeInfoClient->isConnected()) return;
  if (sessionId == m_targetSessionId && !m_dirty) return;
  m_targetSessionId = sessionId;
  m_dirty = true;
  QTimer::singleShot(0, this, [this] {
    if (m_dirty && m_poeInfoClient && m_poeInfoClient->isConnected() && !isVisible()) rebuildDbZones();
  });
  Q_UNUSED(startedAt)
}

void SessionViewPage::viewSession(qint64 sessionId, const QString &startedAt)
{
  const bool alreadyLoaded = (sessionId == m_targetSessionId && !m_dirty);
  m_targetSessionId = sessionId;
  m_sessionLabel->setText(startedAt);
  m_headerBar->setVisible(true);
  m_headerSep->setVisible(true);

  if (!alreadyLoaded) {
    // Clear accumulated live event widgets (not relevant when switching sessions)
    m_scroll->setUpdatesEnabled(false);
    for (QWidget *w : m_liveEventWidgets) {
      m_contentLayout->removeWidget(w);
      delete w;
    }
    m_liveEventWidgets.clear();
    m_prevZoneCard = nullptr;
    m_scroll->setUpdatesEnabled(true);
    m_dirty = true;
  }

  if (isVisible() && m_poeInfoClient && m_poeInfoClient->isConnected() && m_dirty)
    rebuildDbZones();
}

void SessionViewPage::setRunningGames(const QList<WindowState> &games)
{
  const QString now = QDateTime::currentDateTime().toString("HH:mm:ss");

  QMap<quint32, QString> updated;
  for (const auto &g : games)
    updated[g.pid] = m_detectedAt.value(g.pid, now);
  m_detectedAt = updated;

  // In historical mode, track state but don't trigger a rebuild.
  if (m_targetSessionId >= 0)
  {
    m_runningGames = games;
    return;
  }

  // Live mode: existing behavior.
  if (games.isEmpty() && m_sessionStartCard)
  {
    m_contentLayout->removeWidget(m_sessionStartCard);
    delete m_sessionStartCard;
    m_sessionStartCard = nullptr;
  }

  m_runningGames = games;
  m_dirty = true;
  if (isVisible() && m_poeInfoClient && m_poeInfoClient->isConnected())
    rebuildDbZones();
}


void SessionViewPage::resizeEvent(QResizeEvent *e)
{
  QWidget::resizeEvent(e);
  m_loadingOverlay->setGeometry(m_scroll->geometry());
  m_scrollDownBtn->move(rect().right() - m_scrollDownBtn->width() - Theme::spacing3xl,
                        rect().bottom() - m_scrollDownBtn->height() - Theme::spacingBase);
}


// ---------------------------------------------------------------------------
// Public notification API (passes non-zone live events straight through)
// ---------------------------------------------------------------------------

void SessionViewPage::addNotification(const QString &message, const NotificationStyle &style)
{
  auto *w = new NotificationWidget(
      {}, {}, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
  appendLiveWidget(w);
}

void SessionViewPage::addNotification(const QString &title, const QString &tag,
                                  const QString &message, const NotificationStyle &style)
{
  auto *w = new NotificationWidget(
      title, tag, message, QDateTime::currentDateTime().toString("HH:mm"), style, m_content);
  appendLiveWidget(w);
}

// ---------------------------------------------------------------------------
// Live event handling
// ---------------------------------------------------------------------------

void SessionViewPage::onLiveEvent(const LiveEvent &event, bool bulk)
{
  // Historical mode: ignore live events entirely.
  if (m_targetSessionId >= 0)
    return;

  if (bulk)
  {
    m_prevZoneCard = nullptr;
    m_dirty = true;
    if (isVisible() && m_poeInfoClient && m_poeInfoClient->isConnected())
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

    if (m_prevZoneCard && m_poeInfoClient && m_poeInfoClient->isConnected())
    {
      QPointer<NotificationWidget> prevCard = m_prevZoneCard;
      QJsonObject zp;
      zp["session_id"] = qint64(-1);
      zp["limit"]      = 2;
      zp["offset"]     = 0;
      m_poeInfoClient->request("log.zones", zp,
        [prevCard](QJsonObject payload, QString error) {
          if (!prevCard || !error.isEmpty()) return;
          const QJsonArray arr = payload["zones"].toArray();
          if (arr.size() >= 2) {
            const QJsonObject o = arr[1].toObject();
            const int dur = o["duration_secs"].toInt(-1);
            const int afkSecs = o["afk_secs"].toInt(0);
            const bool hasAreaType = !o["area_type"].toString().isEmpty();
            prevCard->setHeaderSuffix(buildZoneSuffix(hasAreaType, dur, afkSecs, false));
          }
        });
    }

    // AFK continues into the new zone if it was already ongoing (mirrors the
    // server splitting the interval at the transition boundary — see
    // writer.closeSpan's continueAfk handling).
    const bool afkContinues = !m_liveAfkOnAt.isEmpty();

    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    auto *card = makeZoneCard(areaName, areaCode, areaType, areaSubtype, areaLevel, ts, -1);
    appendLiveWidget(card);
    m_prevZoneCard = card;
    setLiveAfkState(afkContinues ? event.timestamp : QString(), 0, !areaType.isEmpty());
  }
  else if (event.type == LiveEventType::LoginScreen)
  {
    const QString ts = QDateTime::currentDateTime().toString("HH:mm");
    auto *card = new NotificationWidget("Login screen", {}, {}, ts, clientScreenStyle(), m_content);
    card->setLeadingIcon(QStringLiteral(":/icons/box-arrow-in-right.svg"), QColor(160, 130, 95), 20);
    appendLiveWidget(card);
    m_prevZoneCard = nullptr;
    setLiveAfkState(QString(), 0, false);
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
    if (isVisible() && m_poeInfoClient && m_poeInfoClient->isConnected())
      rebuildDbZones();
  }
  else if (event.type == LiveEventType::AfkOn || event.type == LiveEventType::AltTabOut)
  {
    // AFK and alt-tab are folded into the same indicator on the zone card —
    // the game treats alt-tabbing out the same as an AFK timeout for
    // activity purposes (see writer.kindAfk/kindAltTab, both feeding
    // session_afk), so both are tracked identically here.
    setLiveAfkState(event.timestamp, m_liveAfkBaseSecs, m_liveZoneHasAreaType);
  }
  else if (event.type == LiveEventType::AfkOff || event.type == LiveEventType::AltTabBack)
  {
    // Resolve optimistically from the event's own duration_secs so the
    // highlight drops immediately, then reconcile against the server's
    // authoritative afk_secs once poe-info-service finalizes the interval —
    // it may differ if the away period straddled a zone transition (split
    // at the boundary server-side, see writer.closeSpan).
    const int durSecs = event.data.value("duration_secs").toInt();
    setLiveAfkState(QString(), m_liveAfkBaseSecs + durSecs, m_liveZoneHasAreaType);

    if (m_poeInfoClient && m_poeInfoClient->isConnected())
    {
      QPointer<SessionViewPage> self(this);
      QJsonObject zp;
      zp["session_id"] = qint64(-1);
      zp["limit"]      = 1;
      zp["offset"]     = 0;
      m_poeInfoClient->request("log.zones", zp,
        [self](QJsonObject payload, QString error) {
          if (!self || !error.isEmpty()) return;
          const QJsonArray arr = payload["zones"].toArray();
          if (arr.isEmpty()) return;
          const QJsonObject o = arr[0].toObject();
          self->setLiveAfkState(o["afk_open_since"].toString(), o["afk_secs"].toInt(0),
                                self->m_liveZoneHasAreaType);
        });
    }
  }
}

// ---------------------------------------------------------------------------
// DB zone section
// ---------------------------------------------------------------------------

void SessionViewPage::rebuildDbZones()
{
  if (!m_poeInfoClient || !m_poeInfoClient->isConnected())
    return;
  if (m_rebuildInFlight)
  {
    m_dirty = true;
    return;
  }
  m_dirty = false;
  m_rebuildInFlight = true;

  const auto *sb = m_scroll->verticalScrollBar();
  const int prevMax = sb->maximum();
  const int distFromBottom = prevMax > 0 ? (prevMax - sb->value()) : -1;

  const QList<WindowState> runningGames = m_runningGames;
  const QMap<quint32, QString> detectedAt = m_detectedAt;

  const SessionQueryLimits limits = computeSessionQueryLimits(
      m_targetSessionId < 0, runningGames.size(), kDbZoneLimit);

  QJsonObject params;
  params["session_id"] = m_targetSessionId;
  params["zone_limit"] = limits.zoneLimit;
  params["session_event_limit"] = limits.sessionEventLimit;

  QPointer<SessionViewPage> self(this);
  m_poeInfoClient->request("log.session", params,
    [self, distFromBottom, runningGames, detectedAt](QJsonObject payload, QString error) {
      if (!self) return;
      self->m_rebuildInFlight = false;
      if (!error.isEmpty()) {
        self->m_loadingOverlay->hide();
        self->m_dirty = true;
        return;
      }

      PageData data;

      const QJsonArray zonesArr = payload["zones"].toArray();
      for (const QJsonValue &v : zonesArr) {
        const QJsonObject o = v.toObject();
        Records::ZoneTransitionRecord r;
        r.areaName    = o["area_name"].toString();
        r.areaCode    = o["area_code"].toString();
        r.areaType    = o["area_type"].toString();
        r.areaSubtype = o["area_subtype"].toString();
        r.areaLevel   = o["area_level"].toInt(0);
        r.enteredAt   = o["entered_at"].toString();
        r.durationSecs = o["duration_secs"].toInt(-1);
        r.afkSecs     = o["afk_secs"].toInt(0);
        r.afkOpenSince = o["afk_open_since"].toString();
        data.zones.append(r);
      }

      const QJsonArray seArr = payload["session_events"].toArray();
      for (const QJsonValue &v : seArr) {
        const QJsonObject o = v.toObject();
        Records::SessionEventRecord r;
        r.eventType   = o["event_type"].toString();
        r.occurredAt  = o["occurred_at"].toString();
        r.charName    = o["char_name"].toString();
        r.charClass   = o["char_class"].toString();
        r.installPath = o["install_path"].toString();
        r.activeSecs  = o["active_secs"].toInt(-1);
        r.totalSecs   = o["total_secs"].toInt(-1);
        data.sessionEvents.append(r);
      }

      const QJsonArray cseArr = payload["client_screen_events"].toArray();
      for (const QJsonValue &v : cseArr) {
        const QJsonObject o = v.toObject();
        Records::ClientScreenEventRecord r;
        r.eventType  = o["event_type"].toString();
        r.occurredAt = o["occurred_at"].toString();
        data.clientScreenEvents.append(r);
      }

      self->applyCurrentPageData(data, runningGames, detectedAt, distFromBottom);
      if (self->m_dirty) self->rebuildDbZones();
    });
}

void SessionViewPage::applyCurrentPageData(const PageData &data,
                                       const QList<WindowState> &runningGames,
                                       const QMap<quint32, QString> &detectedAt,
                                       int distFromBottom)
{
  const auto &sessionEvents = data.sessionEvents;
  const auto &zones = data.zones;

  m_loadingOverlay->hide();
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
  setLiveAfkState(QString(), 0, false);
  m_dbZoneOffset = 0;

  setLoadMoreVisible(false);

  // --- Session-running card(s) at the top (live mode only) ---
  if (!runningGames.isEmpty())
  {
    const bool singleClient = (runningGames.size() == 1);

    auto *container = new QWidget(m_content);
    auto *cl = new QVBoxLayout(container);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(6);

    auto findStartEvent = [&](const WindowState &g, const QString &detected)
        -> const Records::SessionEventRecord *
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
      const QString detected = detectedAt.value(g.pid);
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
    QList<Records::SessionEventRecord> sessionStarts;
    for (const auto &ev : sessionEvents)
    {
      if (ev.eventType == QLatin1String("start"))
        sessionStarts.append(ev);
    }

    NotificationWidget *lastZoneCard = nullptr;
    int zi   = zones.size() - 1;
    int si   = 0;

    while (zi >= 0 || si < (int)sessionStarts.size())
    {
      const QString zTs  = (zi  >= 0) ? zones[zi].enteredAt                              : QString{};
      const QString sTs  = (si  < (int)sessionStarts.size()) ? sessionStarts[si].occurredAt : QString{};

      const bool takeZone = !zTs.isEmpty() && (sTs.isEmpty() || zTs <= sTs);

      if (takeZone)
      {
        const auto &z = zones[zi];
        auto *card = makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype,
                                  z.areaLevel, z.enteredAt.mid(11, 5), z.durationSecs,
                                  z.afkSecs, !z.afkOpenSince.isEmpty());
        m_contentLayout->addWidget(card);
        m_dbZoneWidgets.append(card);
        lastZoneCard = card;
        --zi;
      }
      else
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
    }

    if (!zones.isEmpty() && zones[0].durationSecs < 0)
    {
      m_prevZoneCard = lastZoneCard;
      if (m_targetSessionId < 0)
        setLiveAfkState(zones[0].afkOpenSince, zones[0].afkSecs, !zones[0].areaType.isEmpty());
    }
  }
  else
  {
    const auto &cse = data.clientScreenEvents;
    NotificationWidget *lastZoneCard = nullptr;
    int zi  = zones.size() - 1;
    int ci  = cse.size() - 1;

    while (zi >= 0 || ci >= 0)
    {
      const QString zTs = (zi >= 0) ? zones[zi].enteredAt : QString{};
      const QString cTs = (ci >= 0) ? cse[ci].occurredAt  : QString{};

      const bool takeZone = !zTs.isEmpty() && (cTs.isEmpty() || zTs <= cTs);

      if (takeZone)
      {
        const auto &z = zones[zi];
        auto *card = makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype,
                                  z.areaLevel, z.enteredAt.mid(11, 5), z.durationSecs,
                                  z.afkSecs, !z.afkOpenSince.isEmpty());
        appendDbZone(card);
        lastZoneCard = card;
        --zi;
      }
      else
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
    }

    if (!zones.isEmpty() && zones[0].durationSecs < 0)
    {
      m_prevZoneCard = lastZoneCard;
      if (m_targetSessionId < 0)
        setLiveAfkState(zones[0].afkOpenSince, zones[0].afkSecs, !zones[0].areaType.isEmpty());
    }
  }

  m_pendingScrollTo = (distFromBottom <= 4) ? 0 : distFromBottom;
  m_contentLayout->activate();
  m_scroll->setUpdatesEnabled(true);
  emit dataLoaded();
}

void SessionViewPage::onLoadMore()
{
  if (!m_poeInfoClient || !m_poeInfoClient->isConnected() || m_loadMoreInFlight)
    return;

  const int prevMax    = m_scroll->verticalScrollBar()->maximum();
  const int prevValue  = m_scroll->verticalScrollBar()->value();
  const int fetchOffset = m_dbZoneOffset;

  m_loadMoreInFlight = true;
  m_loadMoreBtn->setEnabled(false);

  QJsonObject params;
  params["session_id"] = m_targetSessionId;
  params["limit"]      = kDbZoneLimit;
  params["offset"]     = fetchOffset;

  QPointer<SessionViewPage> self(this);
  m_poeInfoClient->request("log.zones", params,
    [self, prevMax, prevValue](QJsonObject payload, QString error) {
      if (!self) return;
      self->m_loadMoreInFlight = false;
      if (!error.isEmpty()) {
        self->m_loadMoreBtn->setEnabled(true);
        return;
      }

      QList<Records::ZoneTransitionRecord> zones;
      const QJsonArray arr = payload["zones"].toArray();
      for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Records::ZoneTransitionRecord r;
        r.areaName     = o["area_name"].toString();
        r.areaCode     = o["area_code"].toString();
        r.areaType     = o["area_type"].toString();
        r.areaSubtype  = o["area_subtype"].toString();
        r.areaLevel    = o["area_level"].toInt(0);
        r.enteredAt    = o["entered_at"].toString();
        r.durationSecs = o["duration_secs"].toInt(-1);
        r.afkSecs      = o["afk_secs"].toInt(0);
        r.afkOpenSince = o["afk_open_since"].toString();
        zones.append(r);
      }

      self->m_dbZoneOffset += zones.size();

      const int btnIdx   = self->m_contentLayout->indexOf(self->m_loadMoreBtn);
      const int basePos  = self->m_sessionStartCard ? 2 : 1;
      const int insertPos = btnIdx >= 0 ? btnIdx + 1 : basePos;
      for (const auto &z : zones) {
        const QString ts = z.enteredAt.mid(11, 5);
        auto *card = self->makeZoneCard(z.areaName, z.areaCode, z.areaType, z.areaSubtype,
                                        z.areaLevel, ts, z.durationSecs,
                                        z.afkSecs, !z.afkOpenSince.isEmpty());
        self->m_contentLayout->insertWidget(insertPos, card);
        self->m_dbZoneWidgets.append(card);
      }

      self->setLoadMoreVisible(zones.size() == kDbZoneLimit);
      if (zones.size() == kDbZoneLimit)
        self->m_loadMoreBtn->setEnabled(true);

      QTimer::singleShot(0, self.data(), [self, prevMax, prevValue] {
        if (!self) return;
        const int delta = self->m_scroll->verticalScrollBar()->maximum() - prevMax;
        self->m_scroll->verticalScrollBar()->setValue(prevValue + delta);
      });
    });
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static QColor zoneAccent(const QString &areaType, const QString &areaSubtype, const QString &areaCode)
{
    if (areaType == QLatin1String("Hideout"))                               return {100, 170, 215};
    if (areaSubtype == QLatin1String("Town"))                               return {215, 210, 70};
    if (areaType.contains(QLatin1String("Vaal side area"))) {
        if (areaType.startsWith(QLatin1String("Map")))                      return {100, 70,  150};
        return {150, 120, 45};
    }
    if (areaType.startsWith(QLatin1String("Act")))                          return {210, 172, 65};
    if (areaType == QLatin1String("Map"))                                   return {155, 110, 210};
    if (areaType == QLatin1String("Heist"))                                 return {75,  195, 185};
    if (areaType == QLatin1String("Lab"))                                   return {90,  190, 115};
    if (areaType == QLatin1String("Boss Arena"))                            return {210, 110, 80};
    if (areaType == QLatin1String("PvP"))                                   return {210, 145, 75};
    if (areaType == QLatin1String("Mechanic")) {
        if (areaCode.startsWith(QLatin1String("Sanctum")))                  return {170, 135, 40};
        if (areaCode == QLatin1String("Delve_Main"))                        return {40,  90,  160};
        if (areaCode == QLatin1String("ChayulaLeague"))                     return {210, 110, 155};
        if (areaCode == QLatin1String("HeistHub"))                          return {200, 152, 58};
        if (areaCode == QLatin1String("Menagerie_Hub"))                     return {205, 75,  110};
        if (areaCode == QLatin1String("KalguuranSettlersLeague"))           return {145, 158, 172};
        if (areaCode == QLatin1String("Labyrinth_Airlock"))                 return {90,  195, 110};
        return {135, 135, 155};
    }
    return {100, 170, 215};
}

NotificationWidget *SessionViewPage::makeZoneCard(const QString &areaName, const QString &areaCode,
                                              const QString &areaType, const QString &areaSubtype,
                                              int areaLevel, const QString &timestamp,
                                              int durationSecs, int afkSecs, bool afkOngoing)
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
    card->setHeaderSuffix(buildZoneSuffix(true, durationSecs, afkSecs, afkOngoing));
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
  const QString suffix = buildZoneSuffix(false, durationSecs, afkSecs, afkOngoing);
  if (!suffix.isEmpty())
    card->setHeaderSuffix(suffix);
  card->setSource(docSource("Client.txt", "sources/zone-transition"));
  return card;
}

void SessionViewPage::appendDbZone(NotificationWidget *card)
{
  m_contentLayout->addWidget(card);
  m_dbZoneWidgets.append(card);
}

// setLiveAfkState updates the tracked AFK baseline for m_prevZoneCard (the
// current, still-open zone) and starts/stops the 1s tick that keeps an
// ongoing AFK's displayed duration counting up. afkOnAt empty means "not
// currently AFK"; baseAfkSecs is the closed AFK time already in this span,
// to which updateLiveAfkSuffix adds elapsed-since-afkOnAt when ongoing.
//
// Called both when a live afk_on/afk_off event arrives and when a zone
// transition happens mid-AFK — in the latter case afkOnAt is reset to the
// transition timestamp and baseAfkSecs to 0, mirroring how the server splits
// the interval into a fresh row bound to the new span (writer.closeSpan).
void SessionViewPage::setLiveAfkState(const QString &afkOnAt, int baseAfkSecs, bool hasAreaType)
{
  m_liveAfkOnAt = afkOnAt;
  m_liveAfkBaseSecs = baseAfkSecs;
  m_liveZoneHasAreaType = hasAreaType;

  if (!m_afkTickTimer)
  {
    m_afkTickTimer = new QTimer(this);
    m_afkTickTimer->setInterval(1000);
    connect(m_afkTickTimer, &QTimer::timeout, this, &SessionViewPage::updateLiveAfkSuffix);
  }
  if (!afkOnAt.isEmpty())
    m_afkTickTimer->start();
  else
    m_afkTickTimer->stop();

  updateLiveAfkSuffix();
}

void SessionViewPage::updateLiveAfkSuffix()
{
  if (!m_prevZoneCard) return;

  int afkSecs = m_liveAfkBaseSecs;
  const bool ongoing = !m_liveAfkOnAt.isEmpty();
  if (ongoing)
  {
    const QDateTime onDt = QDateTime::fromString(m_liveAfkOnAt, "yyyy-MM-dd HH:mm:ss");
    if (onDt.isValid())
      afkSecs += static_cast<int>(qMax<qint64>(0, onDt.secsTo(QDateTime::currentDateTime())));
  }
  m_prevZoneCard->setHeaderSuffix(buildZoneSuffix(m_liveZoneHasAreaType, -1, afkSecs, ongoing));
}

void SessionViewPage::setLoadMoreVisible(bool visible)
{
  const int idx = m_contentLayout->indexOf(m_loadMoreBtn);
  if (visible && idx < 0)
  {
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

void SessionViewPage::appendLiveWidget(QWidget *w)
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
    QTimer::singleShot(0, this, &SessionViewPage::scrollToBottom);
}

void SessionViewPage::scrollToBottom()
{
  m_scroll->verticalScrollBar()->setValue(m_scroll->verticalScrollBar()->maximum());
}

void SessionViewPage::onScrollRangeChanged(int, int max)
{
  if (m_pendingScrollTo >= 0 && max > 0)
  {
    const int target = (m_pendingScrollTo == 0)
                           ? max
                           : qMax(0, max - m_pendingScrollTo);
    m_scroll->verticalScrollBar()->setValue(target);
    m_scrollSettleTimer->start();
  }
  updateScrollDownBtn();
}

void SessionViewPage::updateScrollDownBtn()
{
  const auto *sb = m_scroll->verticalScrollBar();
  const bool atBottom = sb->value() >= sb->maximum() - 4;
  m_scrollDownBtn->setVisible(!atBottom);
}
