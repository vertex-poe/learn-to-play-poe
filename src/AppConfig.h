#pragma once

#include <QString>

struct AppConfig {
    static constexpr const char *defaultWindowsExe = "PathOfExile.exe";
    static constexpr const char *defaultLinuxExe    = "PathOfExile";

    QString windowsExecutableName;
    QString linuxExecutableName;
    bool useGameOverlay{true};
    bool autoUpdate{true};
    bool autoStartOnBoot{false};
    bool startMinimized{false};
    bool minimizeToTray{true};
    bool autoDetectInstallDir{true};
    QString installDir;

    static AppConfig load();
    void save() const;
    static QString configPath();
};
