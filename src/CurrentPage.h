#pragma once

#include "NotificationWidget.h"

#include <QList>
#include <QWidget>

class Database;
class LiveEvent;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

class CurrentPage : public QWidget
{
    Q_OBJECT
public:
    explicit CurrentPage(QWidget *parent = nullptr);

    void setDatabase(Database *db);

    void addNotification(const QString &message, const NotificationStyle &style = {});
    void addNotification(const QString &title, const QString &tag,
                         const QString &message, const NotificationStyle &style = {});

public slots:
    void onLiveEvent(const LiveEvent &event);

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onLoadMore();

private:
    void rebuildDbZones();
    NotificationWidget *makeZoneCard(const QString &areaName, int areaLevel,
                                     const QString &timestamp, int durationSecs);
    void insertDbZone(NotificationWidget *card);
    void setLoadMoreVisible(bool visible);
    void prependWidget(QWidget *w);

    QScrollArea    *m_scroll{};
    QWidget        *m_content{};
    QVBoxLayout    *m_contentLayout{};
    Database       *m_db{};

    QPushButton              *m_loadMoreBtn{};
    QList<NotificationWidget *> m_dbZoneWidgets;
    int                       m_dbZoneOffset{0};
    static constexpr int      kDbZoneLimit = 50;
    bool                      m_dirty{false};

    NotificationWidget *m_prevZoneCard{};
};
