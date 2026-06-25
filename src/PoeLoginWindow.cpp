#include "PoeLoginWindow.h"
#include "AppConfig.h"

#include <memory>

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QNetworkCookie>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWebEngineCookieStore>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebEngineView>

// JS injected at DocumentCreation in MainWorld on every frame.
// Removes automation fingerprints that Cloudflare's Turnstile checks.
static const char kAntiBotScript[] = R"js(
// navigator.webdriver must be undefined (not false) — false is itself a tell.
Object.defineProperty(navigator, 'webdriver', { get: () => undefined });

// Real desktop Chrome exposes window.chrome.runtime; QtWebEngine does not.
if (!window.chrome) window.chrome = {};
if (!window.chrome.runtime) window.chrome.runtime = {};

// Patch the Notification permission inconsistency that headless Chrome exposes.
try {
    const _origQuery = navigator.permissions.query.bind(navigator.permissions);
    navigator.permissions.query = (p) =>
        (p && p.name === 'notifications')
            ? Promise.resolve({ state: 'default' })
            : _origQuery(p);
} catch (_) {}

// Agree with the Accept-Language header we set on the profile.
Object.defineProperty(navigator, 'languages', { get: () => ['en-US', 'en'] });
)js";

PoeLoginWindow::PoeLoginWindow(const AppConfig &config, QWidget *parent, Mode mode)
    : QDialog(parent, Qt::Window)
{
    setWindowTitle(mode == Mode::Browse ? "PathOfExile.com" : "PathOfExile.com — Login");
    // WindowModal blocks input to the companion window so it can't steal focus,
    // but the event loop keeps running (we use show(), not exec()) so WebEngine
    // can initialise normally and the overlay (a separate top-level) is unaffected.
    setWindowModality(Qt::WindowModal);
    setAttribute(Qt::WA_DeleteOnClose);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Show and raise BEFORE WebEngine spawns its renderer so the window is
    // visible immediately and Windows doesn't mark the app as "Not Responding".
    QScreen *screen = parent ? parent->window()->screen()
                              : QGuiApplication::primaryScreen();
    if (screen)
        setGeometry(screen->availableGeometry());
    showMaximized();
    raise();
    activateWindow();

    qDebug() << "[login] window opened, mode=" << (mode == Mode::Browse ? "browse" : "login")
             << "config ua=" << config.effectiveUserAgent();
    const bool includeAppId    = config.debugMode ? config.debugLegacyUserAgentApp
                                                 : AppConfig::kDefaultLegacyUserAgentApp;
    const bool includeQtToken = config.debugMode ? config.debugUserAgentQt
                                                 : AppConfig::kDefaultUserAgentQt;
    const bool browseMode     = (mode == Mode::Browse);

    // Defer WebEngine creation one tick so the window gets to paint first.
    QTimer::singleShot(0, this, [this, layout, includeAppId, includeQtToken, browseMode]() {
        qDebug() << "[login] WebEngine init";

        // Named persistent profile: Cloudflare's cf_clearance cookie and other
        // storage survive across app launches, so subsequent logins skip the
        // challenge entirely.
        // profile must outlive view+page; view is added after profile so Qt's
        // reverse-order child deletion destroys view→page before profile.
        auto *profile = new QWebEngineProfile(QStringLiteral("l2p-poe-login"), this);

        // Use the native Chromium UA with the QtWebEngine token stripped.
        // The declared Chrome version then matches actual engine capabilities,
        // which is critical — Cloudflare correlates declared version against
        // JS feature fingerprints and catches mismatches like Chrome/149.
        {
            static const QRegularExpression kQtToken(
                QStringLiteral(R"(QtWebEngine/[\d.]+ )"));
            QString ua = profile->httpUserAgent();
            if (!includeQtToken)
                ua.remove(kQtToken);
            ua = ua.trimmed();
            if (includeAppId) {
                const QString token = QCoreApplication::applicationName().remove(u' ')
                                      + u'/'
                                      + QCoreApplication::applicationVersion();
                ua += u' ' + token;
            }
            profile->setHttpUserAgent(ua);
        }
        // Keep Accept-Language consistent with the navigator.languages override.
        profile->setHttpAcceptLanguage(QStringLiteral("en-US,en;q=0.9"));

        qDebug() << "[login] ua=" << profile->httpUserAgent();

        auto *view = new QWebEngineView(this);
        auto *page = new QWebEnginePage(profile, view);  // child of view, not this

        // Inject anti-bot overrides before any page script runs.
        QWebEngineScript antiBot;
        antiBot.setName(QStringLiteral("l2p-anti-bot"));
        antiBot.setSourceCode(QString::fromLatin1(kAntiBotScript));
        antiBot.setInjectionPoint(QWebEngineScript::DocumentCreation);
        antiBot.setWorldId(QWebEngineScript::MainWorld);
        antiBot.setRunsOnSubFrames(true);
        page->scripts().insert(antiBot);

        view->setPage(page);

        // cookieReady gates the POESESSID listener: the persistent profile fires
        // cookieAdded for every cookie it loads from disk (including stale sessions
        // from previous runs). We only want cookies set after the page loads.
        auto retried     = std::make_shared<bool>(false);
        auto cookieReady = std::make_shared<bool>(false);
        connect(page, &QWebEnginePage::loadFinished, this,
                [view, retried, cookieReady](bool ok) {
            if (ok) {
                *cookieReady = true;
            } else if (!*retried) {
                *retried = true;
                qDebug() << "[login] load failed, retrying after 1.5s";
                QTimer::singleShot(1500, view, [view]() { view->reload(); });
            }
        });

        if (!browseMode) {
            connect(profile->cookieStore(), &QWebEngineCookieStore::cookieAdded,
                    this, [this, cookieReady](const QNetworkCookie &cookie) {
                if (!*cookieReady) return;
                if (cookie.name() == "POESESSID" &&
                    cookie.domain().contains(QLatin1String("pathofexile.com"))) {
                    qDebug() << "[login] POESESSID captured";
                    emit sessionCaptured(QString::fromLatin1(cookie.value()));
                    close();
                }
            });
        }

        const QString url = browseMode ? QStringLiteral("https://www.pathofexile.com/")
                                       : QStringLiteral("https://www.pathofexile.com/login");
        view->load(QUrl(url));
        layout->addWidget(view);
        qDebug() << "[login] loading" << url;
    });
}
