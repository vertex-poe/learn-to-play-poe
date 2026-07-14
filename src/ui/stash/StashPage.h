#pragma once

#include "services/PoeInfoRecords.h"
#include <QShowEvent>
#include <QWidget>

class PoeInfoClient;
class QComboBox;

// Stash screen — currently just the league selector (see ROADMAP.md's
// "Stash screen" entry for the rest: browsing/searching stash items across
// tabs isn't possible yet since poe-info-service doesn't expose a stash data
// endpoint — see its "PoE OAuth data endpoints (characters, stash)" entry).
// All league data comes from poe-info-service: poe.leagues.list for the
// selectable set, poe.league for which one to default to.
class StashPage : public QWidget
{
    Q_OBJECT
public:
    explicit StashPage(QWidget *parent = nullptr);

    void setPoeInfoClient(PoeInfoClient *client);
    void preload();

    // The currently selected league's exact name (LeagueSummary.name), or
    // empty if leagues haven't loaded yet.
    QString currentLeague() const;

signals:
    void leagueChanged(const QString &league);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void triggerLoadIfNeeded();
    void rebuild();
    void applyLeagues(const QList<Records::LeagueSummary> &leagues, const QString &defaultLeague);

    PoeInfoClient *m_poeInfoClient{};
    QComboBox     *m_leagueCombo{};
    bool           m_dirty{true};
    bool           m_rebuildInFlight{false};
};
