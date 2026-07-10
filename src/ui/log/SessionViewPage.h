#pragma once

#include "ui/widgets/NotificationWidget.h"
#include "services/PoeInfoRecords.h"
#include "platform/WindowTracker.h"

#include <QList>
#include <QMap>
#include <QResizeEvent>
#include <QWidget>

struct LiveEvent;
class PoeInfoClient;
class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class ScrollJumpButton;

class SessionViewPage : public QWidget
{
    Q_OBJECT
public:
    explicit SessionViewPage(QWidget *parent = nullptr);

    void setPoeInfoClient(PoeInfoClient *client);
    void markDirty();
    void setRunningGames(const QList<WindowState> &games);

    // Switch to displaying a specific session. Pass -1 for the live current session.
    // startedAt is shown in the sub-bar header (e.g. "2024-01-15 14:30:22").
    void viewSession(qint64 sessionId, const QString &startedAt);

    // Preload methods — fire background data fetches while the page is hidden.
    void preload();
    void preloadSession(qint64 sessionId, const QString &startedAt);

    void addNotification(const QString &message, const NotificationStyle &style = {});
    void addNotification(const QString &title, const QString &tag,
                         const QString &message, const NotificationStyle &style = {});

    // Returns m_scroll->viewport() — use this as the paint-probe target, because
    // m_scroll fully covers SessionViewPage and Qt won't deliver QEvent::Paint to
    // the parent when opaque children tile its entire rect.
    QWidget *scrollViewport() const;

signals:
    void backRequested();
    void dataLoaded();

public slots:
    void onLiveEvent(const LiveEvent &event, bool bulk);

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onLoadMore();

private:
    struct PageData {
        QList<Records::SessionEventRecord>        sessionEvents;
        QList<Records::ZoneTransitionRecord>      zones;
        QList<Records::ClientScreenEventRecord>   clientScreenEvents;
    };

    void triggerLoadIfNeeded();
    void rebuildDbZones();
    void applyCurrentPageData(const PageData &data,
                              const QList<WindowState> &runningGames,
                              const QMap<quint32, QString> &detectedAt,
                              int distFromBottom);
    NotificationWidget *makeZoneCard(const QString &areaName, const QString &areaCode,
                                     const QString &areaType, const QString &areaSubtype,
                                     int areaLevel, const QString &timestamp, int durationSecs,
                                     int afkSecs = 0, bool afkOngoing = false);
    void appendDbZone(NotificationWidget *card);
    void setLoadMoreVisible(bool visible);
    void appendLiveWidget(QWidget *w);
    void scrollToBottom();
    void updateScrollDownBtn();
    void onScrollRangeChanged(int min, int max);

    // Live AFK tracking for m_prevZoneCard (the current, still-open zone).
    // See setLiveAfkState's doc comment for the split-across-zone-transition
    // semantics this mirrors from the server (writer.closeSpan).
    void setLiveAfkState(const QString &afkOnAt, int baseAfkSecs, bool hasAreaType);
    void updateLiveAfkSuffix();

    QWidget        *m_headerBar{};
    QWidget        *m_headerSep{};
    QLabel         *m_sessionLabel{};

    QScrollArea    *m_scroll{};
    QWidget        *m_content{};
    QVBoxLayout    *m_contentLayout{};
    PoeInfoClient  *m_poeInfoClient{};

    QPushButton              *m_loadMoreBtn{};
    QList<NotificationWidget *> m_dbZoneWidgets;
    QList<QWidget *>          m_liveEventWidgets;
    int                       m_dbZoneOffset{0};
    static constexpr int      kDbZoneLimit    = 50;
    static constexpr int      kLiveWidgetCap  = 100;

    QList<WindowState>    m_runningGames;
    QMap<quint32, QString> m_detectedAt;  // pid → HH:mm when first detected

    // -1 = live mode (most recent open session); ≥0 = specific historical session ID
    qint64                    m_targetSessionId{-1};

    bool                      m_dirty{false};
    bool                      m_rebuildInFlight{false};
    bool                      m_loadMoreInFlight{false};
    // -1 = none; 0 = go to bottom; >0 = go to (max - value) to restore position
    int                       m_pendingScrollTo{-1};
    QTimer                   *m_scrollSettleTimer{};

    NotificationWidget  *m_prevZoneCard{};
    QWidget             *m_sessionStartCard{};
    QLabel              *m_loadingOverlay{};
    ScrollJumpButton    *m_scrollDownBtn{};

    // Live AFK state for m_prevZoneCard — see setLiveAfkState.
    QTimer               *m_afkTickTimer{};
    QString               m_liveAfkOnAt;          // empty = not currently AFK
    int                    m_liveAfkBaseSecs{0};   // closed AFK time already in this span
    bool                   m_liveZoneHasAreaType{false};
};
