#include <QtTest/QtTest>

#include "FakePoeInfoServer.h"
#include "services/PoeInfoClient.h"
#include "ui/log/LogPage.h"

// Regression coverage for LogPage::rebuild's request-retry-on-error path: on a
// "log.sessions" error, the page marks itself dirty and retries via a 500ms
// QTimer::singleShot once still dirty/connected/visible — mirrors
// SessionViewPage::rebuildDbZones's retry (see test_sessionviewpage_retry.cpp).
// Driven through a real PoeInfoClient connected to an in-process
// FakePoeInfoServer rather than a mock, since PoeInfoClient itself isn't
// mockable.
class TestLogPageRetry : public QObject
{
    Q_OBJECT
private slots:
    void retriesAfterErrorAndSucceeds()
    {
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("log.sessions"), {}, QStringLiteral("boom"));
        server.queueResponse(QStringLiteral("log.sessions"),
                              QJsonObject{{"records", QJsonArray{}}});

        LogPage page;
        page.setPoeInfoClient(&client);
        page.setSessionsReady(true);
        QSignalSpy dataLoadedSpy(&page, &LogPage::dataLoaded);

        // Becoming visible triggers the initial (failing) request.
        page.show();

        // First attempt errors immediately; the 500ms retry then succeeds.
        QVERIFY(dataLoadedSpy.wait(3000));
        QCOMPARE(server.requestCount(QStringLiteral("log.sessions")), 2);
    }

    void doesNotRetryOnceHidden()
    {
        // Mirrors the retry guard's isVisible() check: if the page is hidden
        // again before the 500ms timer fires, the retry must not happen.
        FakePoeInfoServer server;
        PoeInfoClient client(QStringLiteral("127.0.0.1"), server.port());
        QTRY_VERIFY(client.isConnected());

        server.queueResponse(QStringLiteral("log.sessions"), {}, QStringLiteral("boom"));

        LogPage page;
        page.setPoeInfoClient(&client);
        page.setSessionsReady(true);
        page.show();

        QTRY_COMPARE(server.requestCount(QStringLiteral("log.sessions")), 1);
        page.hide();

        // Wait past the 500ms retry window; the guard should have suppressed it.
        QTest::qWait(800);
        QCOMPARE(server.requestCount(QStringLiteral("log.sessions")), 1);
    }
};

QTEST_MAIN(TestLogPageRetry)
#include "test_logpage_retry.moc"
