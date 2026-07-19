#include "ui/stash/StashPage.h"
#include "services/PoeInfoClient.h"
#include "services/PoeInfoRecords.h"
#include "ui/Theme.h"
#include "util/PoeOAuthStore.h"

#include <QComboBox>
#include <QDebug>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

StashPage::StashPage(QWidget *parent)
    : QWidget(parent)
{
    m_authNotice = new QFrame(this);
    m_authNotice->setObjectName(QStringLiteral("stashAuthNotice"));
    m_authNotice->setFrameShape(QFrame::StyledPanel);
    m_authNotice->setVisible(false);
    {
        auto *box = new QHBoxLayout(m_authNotice);
        box->setContentsMargins(Theme::spacingBase, Theme::spacingSm, Theme::spacingBase, Theme::spacingSm);
        box->setSpacing(Theme::spacingSm);

        auto *label = new QLabel("Sign in to Path of Exile to view your leagues.", m_authNotice);
        QPalette pal = label->palette();
        pal.setColor(QPalette::WindowText, Theme::textPlaceholder);
        label->setPalette(pal);
        box->addWidget(label, 1);

        auto *loginBtn = new QPushButton("Sign in…", m_authNotice);
        loginBtn->setObjectName(QStringLiteral("stashAuthNoticeLoginBtn"));
        connect(loginBtn, &QPushButton::clicked, this, &StashPage::loginRequested);
        box->addWidget(loginBtn, 0);
    }

    auto *headerRow = new QWidget(this);
    auto *headerBox = new QHBoxLayout(headerRow);
    headerBox->setContentsMargins(Theme::spacingSm, Theme::spacingSm, Theme::spacingSm, Theme::spacingSm);
    headerBox->setSpacing(Theme::spacingSm);

    headerBox->addWidget(new QLabel("League:", headerRow));

    m_leagueCombo = new QComboBox(headerRow);
    m_leagueCombo->setMinimumWidth(200);
    m_leagueCombo->setEnabled(false);
    m_leagueCombo->addItem("Loading leagues...");
    connect(m_leagueCombo, &QComboBox::activated, this, [this](int index) {
        if (index >= 0) emit leagueChanged(m_leagueCombo->itemText(index));
    });
    headerBox->addWidget(m_leagueCombo);
    headerBox->addStretch(1);

    auto *vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(0, 0, 0, 0);
    vbox->setSpacing(0);
    vbox->addWidget(m_authNotice);
    vbox->addWidget(headerRow);
    vbox->addStretch(1);
}

void StashPage::setPoeInfoClient(PoeInfoClient *client)
{
    m_poeInfoClient = client;
    connect(client, &PoeInfoClient::connected, this, [this] {
        if (isVisible() && m_authorized) rebuild();
        else m_dirty = true;
    });
    m_dirty = true;

    if (!m_oauthStore) {
        m_oauthStore = new PoeOAuthStore(client, this);
        connect(m_oauthStore, &PoeOAuthStore::statusChanged, this,
                [this](bool authorized, bool /*inProgress*/, const QString & /*username*/,
                       const QString & /*scope*/, qint64 /*accessExpiration*/, const QString & /*error*/) {
            setAuthorized(authorized);
        });
    }
    m_oauthStore->checkStatus();

    triggerLoadIfNeeded();
}

void StashPage::preload()
{
    if (!m_dirty || !m_poeInfoClient || !m_poeInfoClient->isConnected() || !m_authorized || m_rebuildInFlight) return;
    QTimer::singleShot(0, this, [this] {
        if (m_dirty && m_poeInfoClient && m_poeInfoClient->isConnected() && m_authorized && !isVisible()) rebuild();
    });
}

QString StashPage::currentLeague() const
{
    return (m_leagueCombo && m_leagueCombo->isEnabled()) ? m_leagueCombo->currentText() : QString();
}

void StashPage::triggerLoadIfNeeded()
{
    if (m_dirty && m_poeInfoClient && isVisible() && m_poeInfoClient->isConnected() && m_authorized) {
        QTimer::singleShot(0, this, [this] {
            if (m_dirty && m_poeInfoClient && m_poeInfoClient->isConnected() && m_authorized) rebuild();
        });
    }
}

void StashPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    triggerLoadIfNeeded();
}

void StashPage::setAuthorized(bool authorized)
{
    const bool wasAuthorized = m_authorized;
    m_authorized = authorized;
    m_authNotice->setVisible(!authorized);

    if (!authorized) {
        QSignalBlocker blocker(m_leagueCombo);
        m_leagueCombo->clear();
        m_leagueCombo->addItem("Sign in to view leagues");
        m_leagueCombo->setEnabled(false);
    } else if (!wasAuthorized) {
        m_dirty = true;
        triggerLoadIfNeeded();
    }
}

void StashPage::rebuild()
{
    if (!m_poeInfoClient || !m_poeInfoClient->isConnected() || !m_authorized) return;
    if (m_rebuildInFlight) { m_dirty = true; return; }
    m_dirty           = false;
    m_rebuildInFlight = true;

    QJsonObject params;
    params["wait"] = true;

    QPointer<StashPage> self(this);
    m_poeInfoClient->request("poe.leagues.list", params,
        [self](QJsonObject payload, QString error) {
            if (!self) return;
            if (!error.isEmpty()) {
                qDebug() << "StashPage: poe.leagues.list error:" << error;
                self->m_rebuildInFlight = false;
                self->m_dirty = true;
                QTimer::singleShot(500, self.data(), [self] {
                    if (self && self->m_dirty && self->m_poeInfoClient
                            && self->m_poeInfoClient->isConnected() && self->isVisible())
                        self->rebuild();
                });
                return;
            }

            QList<Records::LeagueSummary> leagues;
            const QJsonArray arr = payload["leagues"].toArray();
            for (const QJsonValue &v : arr) {
                const QJsonObject o = v.toObject();
                Records::LeagueSummary ls;
                ls.name        = o["name"].toString();
                ls.realm       = o["realm"].toString();
                ls.url         = o["url"].toString();
                ls.startAt     = o["startAt"].toString();
                ls.endAt       = o["endAt"].toString();
                ls.description = o["description"].toString();
                for (const QJsonValue &rv : o["rules"].toArray())
                    ls.rules << rv.toString();
                ls.event      = o["event"].toBool();
                ls.delveEvent = o["delveEvent"].toBool();
                leagues.append(ls);
            }

            // poe.league (free — parsed from Steam rich presence, not the PoE
            // OAuth API) says which league the player is actually in right now;
            // a separate request rather than piggybacked onto leagues.list since
            // it's a wholly different data source.
            self->m_poeInfoClient->request("poe.league", {},
                [self, leagues](QJsonObject payload2, QString error2) {
                    if (!self) return;
                    self->m_rebuildInFlight = false;
                    const QString current = error2.isEmpty() ? payload2["league"].toString() : QString();
                    self->applyLeagues(leagues, current);
                    if (self->m_dirty)
                        QTimer::singleShot(0, self.data(), [self] { if (self) self->rebuild(); });
                });
        });
}

void StashPage::applyLeagues(const QList<Records::LeagueSummary> &leagues, const QString &defaultLeague)
{
    QSignalBlocker blocker(m_leagueCombo);
    m_leagueCombo->clear();

    if (leagues.isEmpty()) {
        m_leagueCombo->addItem("No leagues available");
        m_leagueCombo->setEnabled(false);
        return;
    }

    int defaultIndex = 0;
    for (int i = 0; i < leagues.size(); ++i) {
        m_leagueCombo->addItem(leagues[i].name);
        if (!defaultLeague.isEmpty() && leagues[i].name == defaultLeague)
            defaultIndex = i;
    }
    m_leagueCombo->setCurrentIndex(defaultIndex);
    m_leagueCombo->setEnabled(true);
    blocker.unblock();

    emit leagueChanged(m_leagueCombo->currentText());
}
