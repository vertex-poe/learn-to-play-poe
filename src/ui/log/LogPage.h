#pragma once

#include "db/Database.h"
#include <QResizeEvent>
#include <QShowEvent>
#include <QWidget>

struct LiveEvent;
class QueryService;
class QLabel;
class QScrollArea;
class QVBoxLayout;
class ScrollJumpButton;

class LogPage : public QWidget
{
    Q_OBJECT
public:
    explicit LogPage(QWidget *parent = nullptr);
    void setQueryService(QueryService *qs);
    void markDirty();
    void preload();
    QLabel *loadingOverlay() const { return m_loadingOverlay; }

signals:
    void viewSessionRequested(qint64 sessionId, const QString &startedAt);
    void sessionPreviewRequested(qint64 sessionId, const QString &startedAt);
    void dataLoaded();

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onLiveEvent(const LiveEvent &event, bool bulk);

private:
    void triggerLoadIfNeeded();
    void rebuild();
    void applySessions(const QList<Database::SessionRecord> &sessions);
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
    bool          m_timingEmitted{false};
    int           m_limit{kInitialLimit};
    int           m_windowOffset{0};   // SQL OFFSET: skip this many newest items
    int           m_scrollRestoreMax{-1};
    int           m_scrollRestoreValue{0};
    int           m_scrollRestoreNthRecord{-1};

    QLabel           *m_loadingOverlay{};
    ScrollJumpButton *m_scrollDownBtn{};
};
