#pragma once

#include "db/Database.h"
#include <QResizeEvent>
#include <QShowEvent>
#include <QStringList>
#include <QWidget>

struct LiveEvent;
class PoeInfoClient;
class QComboBox;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class ScrollJumpButton;

class DmPage : public QWidget
{
    Q_OBJECT
public:
    explicit DmPage(QWidget *parent = nullptr);

    void setPoeInfoClient(PoeInfoClient *client);
    void setShowGuildTags(bool show);
    void reload();
    void preload();

signals:
    void dataLoaded();
    void backRequested();

public slots:
    void onLiveWhisper(const LiveEvent &event, bool bulk);

protected:
    void showEvent(QShowEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;

private slots:
    void onPlayerSelected(const QString &name);

private:
    void triggerLoadIfNeeded();
    void rebuild();
    void applyWhispers(const QList<Database::WhisperRecord> &whispers);
    void showError(const QString &msg);
    void openFilterPanel();
    void refreshFilterPanel();
    void filterLeafSelected(const QString &name);
    void scrollToBottom();
    void jumpToLiveView();
    void updateScrollDownBtn();

    static constexpr int kInitialLimit = 100;
    static constexpr int kPageStep     = 50;
    static constexpr int kMaxWindow    = 300;

    PoeInfoClient *m_poeInfoClient{};
    QComboBox    *m_conversationCombo{};
    QPushButton  *m_backToChatBtn{};
    QPushButton  *m_filterBtn{};
    QString       m_filterPlayer;
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

    QLabel                        *m_loadingOverlay{};
    ScrollJumpButton              *m_scrollDownBtn{};
    QStackedWidget                *m_view{};
    QWidget                       *m_filterPanel{};
    QScrollArea                   *m_filterScroll{};
    QWidget                       *m_filterListWidget{};
    QVBoxLayout                   *m_filterListLayout{};
    QLabel                        *m_filterTitle{};
    QPushButton                   *m_backBtn{};
    QStringList                    m_filterPath;
    QList<Database::PartnerRecord> m_cachedPartners;
};
