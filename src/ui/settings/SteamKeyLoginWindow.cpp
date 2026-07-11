#include "ui/settings/SteamKeyLoginWindow.h"
#include "util/SteamApiKeyExtractor.h"

#include <memory>

#include <QGuiApplication>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineView>

SteamKeyLoginWindow::SteamKeyLoginWindow(QWidget *parent)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle("Steam \xe2\x80\x94 Web API Key");
    // WindowModal blocks input to the companion window so it can't steal
    // focus, but the event loop keeps running (we use show(), not exec())
    // so WebEngine can initialise normally — mirrors PoeLoginWindow.
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    constexpr int kWidth  = 1024;
    constexpr int kHeight = 800;
    QScreen *screen = parent ? parent->window()->screen()
                              : QGuiApplication::primaryScreen();
    const QRect avail = screen ? screen->availableGeometry() : QRect(0, 0, kWidth, kHeight);
    QRect geom(0, 0, qMin(kWidth, avail.width()), qMin(kHeight, avail.height()));
    geom.moveCenter(avail.center());
    setGeometry(geom);
    show();
    raise();
    activateWindow();

    // Defer WebEngine creation one tick so the window gets to paint first —
    // same rationale as PoeLoginWindow (a slow-to-spin-up Chromium renderer
    // otherwise delays first paint enough that Windows' hang detector flags
    // the window as "Not Responding" before any content appears).
    QTimer::singleShot(0, this, [this, layout]() {
        // Named persistent profile so a Steam login session (and "remember
        // me") survives across app launches, same rationale as
        // PoeLoginWindow's "l2p-poe-login" profile. Left parentless and
        // deleted only once view/page are gone (a QWebEnginePage keeps
        // using its profile until it is actually destroyed).
        auto *profile = new QWebEngineProfile(QStringLiteral("l2p-poe-steam-login"));
        {
            static const QRegularExpression kQtToken(
                QStringLiteral(R"(QtWebEngine/[\d.]+ )"));
            QString ua = profile->httpUserAgent();
            ua.remove(kQtToken);
            profile->setHttpUserAgent(ua.trimmed());
        }

        auto *view = new QWebEngineView(this);
        auto *page = new QWebEnginePage(profile, view); // child of view, not this
        connect(view, &QObject::destroyed, profile, &QObject::deleteLater);
        view->setPage(page);

        auto retried = std::make_shared<bool>(false);
        connect(page, &QWebEnginePage::loadFinished, this,
                [this, view, retried](bool ok) {
            if (!ok) {
                if (!*retried) {
                    *retried = true;
                    QTimer::singleShot(1500, view, [view]() { view->reload(); });
                }
                return;
            }
            // Checked on every load, not just the first: an account with no
            // key yet shows Steam's own registration form first, which this
            // window can't auto-submit (it requires agreeing to Steam's Web
            // API ToS on the user's behalf) — submitting it, or logging in,
            // triggers further loadFinished calls that get checked here too.
            // runJavaScript's callback is a plain std::function with no
            // receiver-context overload, so it isn't auto-disconnected if
            // this window is deleted before it fires (possible if an
            // earlier, still-pending callback from a previous load
            // resolves after a later one already found the key and closed
            // the window) — guard with a QPointer instead of raw `this`.
            QPointer<SteamKeyLoginWindow> self(this);
            view->page()->runJavaScript(
                QStringLiteral("document.body ? document.body.innerText : ''"),
                [self](const QVariant &result) {
                if (!self) return;
                const QString key = extractSteamApiKey(result.toString());
                if (!key.isEmpty()) {
                    emit self->keyCaptured(key);
                    self->close();
                }
            });
        });

        view->load(QUrl(QStringLiteral("https://steamcommunity.com/dev/apikey")));
        layout->addWidget(view);
    });
}
