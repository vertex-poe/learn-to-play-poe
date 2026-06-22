#pragma once

#include "Database.h"
#include <QResizeEvent>
#include <QShowEvent>
#include <QWidget>

struct LiveEvent;
class QueryService;
class QScrollArea;
class QVBoxLayout;
class ScrollJumpButton;

class PastPage : public QWidget
{
    Q_OBJECT
public:
    explicit PastPage(QWidget *parent = nullptr);
    void setQueryService(QueryService *qs);
    void markDirty();

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onLiveEvent(const LiveEvent &event, bool bulk);

private:
    void rebuild();
    void applySessionEvents(const QList<Database::SessionEventRecord> &events);
    void scrollToBottom();
    void jumpToLiveView();
    void updateScrollDownBtn();

    static constexpr int kInitialLimit = 100;
    static constexpr int kPageStep     = 50;
    static constexpr int kMaxWindow    = 300;

    QueryService *m_queryService{};
    QScrollArea  *m_scroll{};
    QWidget      *m_content{};
    QVBoxLayout  *m_contentLayout{};
    bool          m_dirty{true};
    bool          m_rebuildInFlight{false};
    int           m_limit{kInitialLimit};
    int           m_windowOffset{0};   // SQL OFFSET: skip this many newest items
    int           m_scrollRestoreMax{-1};
    int           m_scrollRestoreValue{0};

    ScrollJumpButton *m_scrollDownBtn{};
};
