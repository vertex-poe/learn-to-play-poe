#pragma once

#include "Database.h"
#include <QShowEvent>
#include <QStringList>
#include <QWidget>

class LiveEvent;
class QLabel;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;
class QVBoxLayout;

class DmPage : public QWidget
{
    Q_OBJECT
public:
    explicit DmPage(Database *db, QWidget *parent = nullptr);

    void setDatabase(Database *db);
    void setShowGuildTags(bool show);
    void reload();

public slots:
    void onLiveWhisper(const LiveEvent &event);

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onPlayerSelected(const QString &name);

private:
    void rebuild();
    void openFilterPanel();
    void refreshFilterPanel();
    void filterLeafSelected(const QString &name);
    void scrollToBottom();

    Database    *m_db{};
    QLabel      *m_conversationLabel{};
    QPushButton *m_filterBtn{};
    QString      m_filterPlayer;
    QScrollArea *m_scroll{};
    QWidget     *m_content{};
    QVBoxLayout *m_contentLayout{};
    QTimer      *m_liveRebuildTimer{};
    bool         m_liveRebuildScrollToBottom{false};
    bool         m_dirty{true};
    bool         m_showGuildTags{true};
    int          m_limit{100};

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
