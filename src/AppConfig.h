#pragma once

#include "LiveEventRule.h"

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
    bool debugLog{false};
    bool useGameOverlay{true};
    bool autoUpdate{true};
    bool autoStartOnBoot{false};
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
};
