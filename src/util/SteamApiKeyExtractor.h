#pragma once

#include <QRegularExpression>
#include <QString>

// Pulls a Steam Web API key out of the rendered-text of
// https://steamcommunity.com/dev/apikey. That page has shown a plain
// "Key: <32 hex chars>" line for a registered key for well over a decade
// (used by other tools scraping the same page), so this matches against the
// page's visible text (e.g. QWebEnginePage::runJavaScript'd
// document.body.innerText) rather than raw HTML — robust to markup changes,
// and avoids depending on WebEngine so it's unit-testable on its own. Pulled
// out of SteamKeyLoginWindow so the extraction logic is testable without Qt
// Widget/WebEngine deps (mirrors ZoneAfkSuffix.h).
inline QString extractSteamApiKey(const QString &pageText)
{
    static const QRegularExpression kKeyPattern(
        QStringLiteral(R"(Key:\s*([0-9A-Fa-f]{32}))"));
    const auto match = kKeyPattern.match(pageText);
    return match.hasMatch() ? match.captured(1) : QString();
}
