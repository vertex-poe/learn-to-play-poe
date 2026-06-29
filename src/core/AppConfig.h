#pragma once

#include "events/LiveEventRule.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

struct WindowGeometry {
    int     x{0}, y{0};
    int     width{720}, height{480};
    QString screen; // empty = no saved geometry
};

struct AppConfig {
    static QStringList knownExes()
    {
        return {"PathOfExile_x64Steam.exe", "PathOfExileSteam.exe",
                "PathOfExile_x64.exe",      "PathOfExile.exe",
                "PathOfExile_x64",          "PathOfExile"};
    }

    QStringList executableNames; // empty = use knownExes()
    bool    debugMode{false};
    bool    debugLog{false};
    static constexpr bool kDefaultLegacyUserAgentApp = true;
    static constexpr bool kDefaultUserAgentQt        = true;

    QString debugLegacyUserAgent{"Auto (Chromium)"};
    QString debugLegacyUserAgentCustom;
    bool    debugLegacyUserAgentApp{kDefaultLegacyUserAgentApp};
    bool    debugUserAgentQt{kDefaultUserAgentQt};
    bool useGameOverlay{true};
    int overlayColumns{1};
    int overlayRows{0};
    bool overlayShowHideout{true};
    bool overlayShowGuild{false};
    bool overlayShowMenagerie{false};
    bool overlayShowMonastery{false};
    bool overlayShowHeist{false};
    bool overlayShowSanctum{false};
    bool overlayShowLadder{false};
    bool overlayShowDelve{false};
    bool overlayShowKingsmarch{false};
    bool overlayShowTimePlayed{false};
    bool overlayShowCharacterAge{false};
    bool overlayShowPassives{false};
    bool overlayShowDeaths{false};
    bool overlayShowMonstersRemaining{false};
    bool overlayShowAtlasPassives{false};
    bool overlayShowKills{false};
    bool overlayShowResetXP{false};
    bool overlayShowReloadItemFilter{false};
    bool overlayShowL2P{true};
    bool autoUpdate{true};
    bool autoStartOnBoot{false};
    int  defaultTab{5};          // 0=Guide 1=Chat 2=DMs 3=Stash 4=Profile 5=CurrentLog 6=PastLogs
    bool startMinimized{false};
    bool minimizeToTray{true};
    bool autoDetectInstallDir{true};
    bool showGuildTags{true};
    QStringList installDirs;
    QHash<int, QString>    channelNames;    // channel number → user-defined label
    QVector<LiveEventRule> liveAlertRules;
    WindowGeometry         windowGeometry;

    static AppConfig load();
    void save() const;
    static QString configPath();
    QString effectiveUserAgent() const;
};
