#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "ui/log/SessionViewPage.h"

// Regression coverage for SessionViewPage::rebuildDbZones's request-retry-on-error
// path: on a "log.session" error, the page marks itself dirty and retries via a
// 500ms QTimer::singleShot once still dirty/connected/visible. Driven through a
// real PoeInfoClient connected to an in-process FakePoeInfoServer rather than a
// mock, since PoeInfoClient itself isn't mockable (see ROADMAP.md history).
class TestSessionViewPageRetry : public QObject
{
    Q_OBJECT
private slots:
    void retriesAfterErrorAndSucceeds()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("log.session"), {}, QStringLiteral("boom"));
        server.queueResponse(QStringLiteral("log.session"), QJsonObject{
            {"zones", QJsonArray{}},
            {"session_events", QJsonArray{}},
            {"client_screen_events", QJsonArray{}},
        });

        SessionViewPage page;
        page.setPoeInfoClient(&client);
        QSignalSpy dataLoadedSpy(&page, &SessionViewPage::dataLoaded);

        // Becoming visible triggers the initial (failing) request.
        page.show();

        // First attempt errors immediately; the 500ms retry then succeeds.
        QVERIFY(dataLoadedSpy.wait(3000));
        QCOMPARE(server.requestCount(QStringLiteral("log.session")), 2);
    }

    void doesNotRetryOnceHidden()
    {
        // Mirrors the retry guard's isVisible() check: if the page is hidden
        // again before the 500ms timer fires, the retry must not happen.
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("log.session"), {}, QStringLiteral("boom"));

        SessionViewPage page;
        page.setPoeInfoClient(&client);
        page.show();

        QTRY_COMPARE(server.requestCount(QStringLiteral("log.session")), 1);
        page.hide();

        // Wait past the 500ms retry window; the guard should have suppressed it.
        QTest::qWait(800);
        QCOMPARE(server.requestCount(QStringLiteral("log.session")), 1);
    }
};

QTEST_MAIN(TestSessionViewPageRetry)
#include "test_sessionviewpage_retry.moc"
