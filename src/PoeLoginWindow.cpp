#include "PoeLoginWindow.h"
#include "AppConfig.h"

#include <QGuiApplication>
#include <QNetworkCookie>
#include <QScreen>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

PoeLoginWindow::PoeLoginWindow(const AppConfig &config, QWidget *parent)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle("PathOfExile.com — Login");

    auto *profile = new QWebEngineProfile(this);
    profile->setHttpUserAgent(config.effectiveUserAgent());

    connect(profile->cookieStore(), &QWebEngineCookieStore::cookieAdded,
            this, [this](const QNetworkCookie &cookie) {
        if (cookie.name() == "POESESSID" &&
            cookie.domain().contains(QLatin1String("pathofexile.com"))) {
            emit sessionCaptured(QString::fromLatin1(cookie.value()));
            close();
        }
    });

    auto *page = new QWebEnginePage(profile, this);
    auto *view = new QWebEngineView(this);
    view->setPage(page);
    view->load(QUrl(QStringLiteral("https://www.pathofexile.com/login")));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(view);

    QScreen *screen = parent ? parent->window()->screen() : QGuiApplication::primaryScreen();
    if (screen)
        setGeometry(QRect(screen->availableGeometry().topLeft(), QSize(1, 1)));
    showMaximized();
}
