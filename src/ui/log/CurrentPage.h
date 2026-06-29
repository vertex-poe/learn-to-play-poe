#pragma once

#include "ui/widgets/NotificationWidget.h"
#include "db/QueryService.h"
#include "platform/WindowTracker.h"

#include <QList>
#include <QMap>
#include <QResizeEvent>
#include <QWidget>

struct LiveEvent;
class QueryService;
class QPushButton;
class QScrollArea;
class QVBoxLayout;
class ScrollJumpButton;

class CurrentPage : public QWidget
{
    Q_OBJECT
public:
    explicit CurrentPage(QWidget *parent = nullptr);

    void setQueryService(QueryService *qs);
    void markDirty();
    void setRunningGames(const QList<WindowState> &games);

    void addNotification(const QString &message, const NotificationStyle &style = {});
    void addNotification(const QString &title, const QString &tag,
                         const QString &message, const NotificationStyle &style = {});

public slots:
    void onLiveEvent(const LiveEvent &event, bool bulk);

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onLoadMore();

private:
    void triggerLoadIfNeeded();
    void rebuildDbZones();
    void applyCurrentPageData(const QueryService::CurrentPageData &data,
                              const QList<WindowState> &runningGames,
                              const QMap<quint32, QString> &detectedAt,
                              int distFromBottom);
    NotificationWidget *makeZoneCard(const QString &areaName, const QString &areaCode,
                                     const QString &areaType, const QString &areaSubtype,
                                     int areaLevel, const QString &timestamp, int durationSecs);
    void appendDbZone(NotificationWidget *card);
    void setLoadMoreVisible(bool visible);
    void appendLiveWidget(QWidget *w);
    void scrollToBottom();
    void updateScrollDownBtn();
    void onScrollRangeChanged(int min, int max);

    QScrollArea    *m_scroll{};
    QWidget        *m_content{};
    QVBoxLayout    *m_contentLayout{};
    QueryService   *m_queryService{};

    QPushButton              *m_loadMoreBtn{};
    QList<NotificationWidget *> m_dbZoneWidgets;
    QList<QWidget *>          m_liveEventWidgets;
    int                       m_dbZoneOffset{0};
    static constexpr int      kDbZoneLimit    = 50;
    static constexpr int      kLiveWidgetCap  = 100;

    QList<WindowState>    m_runningGames;
    QMap<quint32, QString> m_detectedAt;  // pid → HH:mm when first detected

    bool                      m_dirty{false};
    bool                      m_rebuildInFlight{false};
    bool                      m_loadMoreInFlight{false};
    // -1 = none; 0 = go to bottom; >0 = go to (max - value) to restore position
    int                       m_pendingScrollTo{-1};
    QTimer                   *m_scrollSettleTimer{};

    NotificationWidget  *m_prevZoneCard{};
    QWidget             *m_sessionStartCard{};
    QString              m_dbAltTabOutTs;  // out_at of the pending alt-tab record currently shown via DB
    ScrollJumpButton    *m_scrollDownBtn{};
};
