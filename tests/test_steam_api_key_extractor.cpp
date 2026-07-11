#include <QtTest/QtTest>

#include "util/SteamApiKeyExtractor.h"

// Regression coverage for scraping a Steam Web API key out of the rendered
// text of https://steamcommunity.com/dev/apikey (see SteamKeyLoginWindow).
class TestSteamApiKeyExtractor : public QObject
{
    Q_OBJECT
private slots:
    void registeredKey_isExtracted()
    {
        const QString text = QStringLiteral(
            "Steam Web API Key\n"
            "Key: 1A2B3C4D5E6F7890ABCDEF1234567890\n"
            "Domain: localhost\n"
            "Revoke My Steam Web API Key");
        QCOMPARE(extractSteamApiKey(text), QStringLiteral("1A2B3C4D5E6F7890ABCDEF1234567890"));
    }

    void lowercaseHexKey_isExtracted()
    {
        const QString text = QStringLiteral("Key: 1a2b3c4d5e6f7890abcdef1234567890");
        QCOMPARE(extractSteamApiKey(text), QStringLiteral("1a2b3c4d5e6f7890abcdef1234567890"));
    }

    void registrationForm_noKeyYet_returnsEmpty()
    {
        const QString text = QStringLiteral(
            "You must have a validated email address to create a Steam Web API key.\n"
            "Register for a Steam Web API Key\n"
            "Domain: [___________]\n"
            "I agree to the Steam API Terms of Use.");
        QVERIFY(extractSteamApiKey(text).isEmpty());
    }

    void loginPage_returnsEmpty()
    {
        QVERIFY(extractSteamApiKey(QStringLiteral("Sign in to your Steam account")).isEmpty());
    }

    void shortHexRun_doesNotFalsePositive()
    {
        // A hex-like run that isn't actually 32 characters (e.g. some other
        // token on the page) must not be mistaken for the key.
        const QString text = QStringLiteral("Key: deadbeef");
        QVERIFY(extractSteamApiKey(text).isEmpty());
    }
};

QTEST_MAIN(TestSteamApiKeyExtractor)
#include "test_steam_api_key_extractor.moc"
