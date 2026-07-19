#pragma once

#include "services/PoeInfoRecords.h"
#include <QShowEvent>
#include <QWidget>

class PoeInfoClient;
class PoeOAuthStore;
class QComboBox;
class QFrame;

// Stash screen — currently just the league selector (see ROADMAP.md's
// "Stash screen" entry for the rest: browsing/searching stash items across
// tabs isn't possible yet since poe-info-service doesn't expose a stash data
// endpoint — see its "PoE OAuth data endpoints (characters, stash)" entry).
// All league data comes from poe-info-service: poe.leagues.list (the
// account-scoped leagues list, private leagues included) for the selectable
// set, poe.league for which one to default to. poe.leagues.list requires
// being signed in to PoE OAuth, so this page shows an auth banner (with a
// button that jumps to Settings > Accounts, via loginRequested()) instead of
// fetching leagues whenever the user isn't authenticated.
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

    // Emitted when the user clicks the auth banner's "sign in" button —
    // MainWindow is expected to navigate to Settings > Accounts.
    void loginRequested();

protected:
    void showEvent(QShowEvent *e) override;

private:
    void triggerLoadIfNeeded();
    void rebuild();
    void applyLeagues(const QList<Records::LeagueSummary> &leagues, const QString &defaultLeague);
    void setAuthorized(bool authorized);

    PoeInfoClient  *m_poeInfoClient{};
    PoeOAuthStore  *m_oauthStore{};
    QComboBox      *m_leagueCombo{};
    QFrame         *m_authNotice{};
    bool            m_dirty{true};
    bool            m_rebuildInFlight{false};
    bool            m_authorized{false};
};
