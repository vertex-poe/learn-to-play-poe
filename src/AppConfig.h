#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

struct AppConfig {
    static QStringList knownExes()
    {
        return {"PathOfExile_x64Steam.exe", "PathOfExileSteam.exe",
                "PathOfExile_x64.exe",      "PathOfExile.exe",
                "PathOfExile_x64",          "PathOfExile"};
    }

    QStringList executableNames; // empty = use knownExes()
    bool useGameOverlay{true};
    bool autoUpdate{true};
    bool autoStartOnBoot{false};
    bool startMinimized{false};
    bool minimizeToTray{true};
    bool autoDetectInstallDir{true};
    QStringList installDirs;
    QHash<int, QString> channelNames; // channel number → user-defined label

    static AppConfig load();
    void save() const;
    static QString configPath();
};
