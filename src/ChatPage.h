#pragma once

#include "Database.h"
#include <QResizeEvent>
#include <QSet>
#include <QShowEvent>
#include <QStringList>
#include <QWidget>

struct LiveEvent;
class QueryService;
class QCheckBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class ScrollJumpButton;

class ChatPage : public QWidget
{
    Q_OBJECT
public:
    explicit ChatPage(QWidget *parent = nullptr);

    void setQueryService(QueryService *qs);
    void setShowGuildTags(bool show);
    void reload();

public slots:
    void onLiveChat(const LiveEvent &event, bool bulk);

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private:
    void rebuild();
    void applyChats(const QList<Database::ChatRecord> &records);
    void openFilterPanel();
    void refreshFilterPanel();
    void scrollToBottom();
    void jumpToLiveView();
    void updateScrollDownBtn();
    QSet<QChar> activeChannels() const;
    void updateFilterLabel();

    static constexpr int kInitialLimit = 100;
    static constexpr int kPageStep     = 50;
    static constexpr int kMaxWindow    = 300;

    QueryService *m_queryService{};
    QScrollArea  *m_scroll{};
    QWidget      *m_content{};
    QVBoxLayout  *m_contentLayout{};
    QTimer       *m_liveRebuildTimer{};
    bool          m_liveRebuildScrollToBottom{false};
    bool          m_dirty{true};
    bool          m_rebuildInFlight{false};
    bool          m_showGuildTags{true};
    int           m_limit{kInitialLimit};
    int           m_windowOffset{0};
    int           m_scrollRestoreMax{-1};
    int           m_scrollRestoreValue{0};
    int           m_scrollRestoreNthRecord{-1};

    QString m_fromDate;
    QString m_toDate;

    QCheckBox   *m_cbLocal{};
    QCheckBox   *m_cbGlobal{};
    QCheckBox   *m_cbParty{};
    QCheckBox   *m_cbDm{};
    QCheckBox   *m_cbTrade{};
    QCheckBox   *m_cbGuild{};
    QPushButton *m_filterBtn{};

    ScrollJumpButton *m_scrollDownBtn{};
    QStackedWidget   *m_view{};
    QWidget        *m_filterPanel{};
    QScrollArea    *m_filterScroll{};
    QLabel         *m_filterTitle{};
    QPushButton    *m_backBtn{};
    QStringList     m_filterPath;
    QStringList     m_cachedDates;
};
