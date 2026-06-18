#pragma once

#include <QString>

struct AppConfig {
    QString windowsExecutableName{"PathOfExile.exe"};
    QString linuxExecutableName{"PathOfExile"};
    bool useGameOverlay{true};
    bool autoStartOnBoot{false};
    bool startMinimized{false};
    bool minimizeToTray{true};
    bool autoDetectInstallDir{true};
    QString installDir;

    static AppConfig load();
    void save() const;
    static QString configPath();
};
