#include "AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

#include <toml++/toml.hpp>

#include <fstream>

QString AppConfig::configPath()
{
    // Dev workflow: prefer TOML in the current working directory (e.g. project root when running `just run`)
    const QString cwdPath = QDir::currentPath() + "/l2p-poe1.toml";
    if (QFile::exists(cwdPath))
        return cwdPath;

    return QCoreApplication::applicationDirPath() + "/l2p-poe1.toml";
}

AppConfig AppConfig::load()
{
    AppConfig cfg;
    const QString path = configPath();

    if (!QFile::exists(path)) {
        cfg.save();
        return cfg;
    }

    try {
        auto tbl = toml::parse_file(path.toStdString());
        cfg.windowsExecutableName = QString::fromStdString(tbl["windows_executable_name"].value_or(std::string("PathOfExile.exe")));
        cfg.linuxExecutableName   = QString::fromStdString(tbl["linux_executable_name"].value_or(std::string("PathOfExile")));
        cfg.useGameOverlay        = tbl["use_game_overlay"].value_or(true);
        cfg.autoStartOnBoot       = tbl["auto_start_on_boot"].value_or(false);
        cfg.startMinimized        = tbl["start_minimized"].value_or(false);
        cfg.minimizeToTray        = tbl["minimize_to_tray"].value_or(true);
        cfg.autoDetectInstallDir  = tbl["auto_detect_install_dir"].value_or(true);
        cfg.installDir            = QString::fromStdString(tbl["install_dir"].value_or(std::string("")));
    } catch (const toml::parse_error &) {
        // File exists but is invalid — use defaults silently
    }

    return cfg;
}

void AppConfig::save() const
{
    const QString path = configPath();

    toml::table tbl;
    tbl.insert("windows_executable_name", windowsExecutableName.toStdString());
    tbl.insert("linux_executable_name",   linuxExecutableName.toStdString());
    tbl.insert("use_game_overlay",        useGameOverlay);
    tbl.insert("auto_start_on_boot",      autoStartOnBoot);
    tbl.insert("start_minimized",         startMinimized);
    tbl.insert("minimize_to_tray",        minimizeToTray);
    tbl.insert("auto_detect_install_dir", autoDetectInstallDir);
    tbl.insert("install_dir",             installDir.toStdString());

    std::ofstream ofs(path.toStdString());
    ofs << tbl;
}
