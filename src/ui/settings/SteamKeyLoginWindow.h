#pragma once

#include <QDialog>

// WebView capture of a Steam Web API key, mirroring PoeLoginWindow's capture
// of POESESSID. Unlike POESESSID (a cookie), the key is scraped out of the
// rendered text of https://steamcommunity.com/dev/apikey — see
// SteamApiKeyExtractor.h. A Steam Web API key never expires, so this is a
// one-time capture rather than a background-refreshed one.
class SteamKeyLoginWindow : public QDialog
{
    Q_OBJECT
public:
    explicit SteamKeyLoginWindow(QWidget *parent = nullptr);

signals:
    void keyCaptured(const QString &apiKey);
};
