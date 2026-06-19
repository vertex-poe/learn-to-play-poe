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
        if (const auto *arr = tbl["executable_names"].as_array()) {
            for (const auto &node : *arr) {
                if (auto val = node.value<std::string>(); val && !val->empty())
                    cfg.executableNames << QString::fromStdString(*val);
            }
        }
        cfg.useGameOverlay        = tbl["use_game_overlay"].value_or(true);
        cfg.autoUpdate            = tbl["auto_update"].value_or(true);
        cfg.autoStartOnBoot       = tbl["auto_start_on_boot"].value_or(false);
        cfg.startMinimized        = tbl["start_minimized"].value_or(false);
        cfg.minimizeToTray        = tbl["minimize_to_tray"].value_or(true);
        cfg.autoDetectInstallDir  = tbl["auto_detect_install_dir"].value_or(true);
        if (const auto *arr = tbl["install_dirs"].as_array()) {
            for (const auto &node : *arr) {
                if (auto val = node.value<std::string>(); val && !val->empty())
                    cfg.installDirs << QString::fromStdString(*val);
            }
        }
        if (const auto *names = tbl["chat_channel_names"].as_table()) {
            for (const auto &[key, val] : *names) {
                bool ok;
                const int num = QString::fromUtf8(key.data(), (int)key.size()).toInt(&ok);
                if (ok) {
                    if (auto v = val.value<std::string>())
                        cfg.channelNames[num] = QString::fromStdString(*v);
                }
            }
        }
    } catch (const toml::parse_error &) {
        // File exists but is invalid — use defaults silently
    }

    return cfg;
}

void AppConfig::save() const
{
    const QString path = configPath();

    toml::table tbl;
    toml::array exeArr;
    for (const QString &exe : executableNames)
        exeArr.push_back(exe.toStdString());
    tbl.insert("executable_names", std::move(exeArr));
    tbl.insert("use_game_overlay",        useGameOverlay);
    tbl.insert("auto_update",             autoUpdate);
    tbl.insert("auto_start_on_boot",      autoStartOnBoot);
    tbl.insert("start_minimized",         startMinimized);
    tbl.insert("minimize_to_tray",        minimizeToTray);
    tbl.insert("auto_detect_install_dir", autoDetectInstallDir);
    toml::array dirsArr;
    for (const QString &dir : installDirs)
        dirsArr.push_back(dir.toStdString());
    tbl.insert("install_dirs", std::move(dirsArr));

    toml::table namesTable;
    for (auto it = channelNames.constBegin(); it != channelNames.constEnd(); ++it)
        namesTable.insert(std::to_string(it.key()), it.value().toStdString());
    tbl.insert("chat_channel_names", std::move(namesTable));

    std::ofstream ofs(path.toStdString());
    ofs << tbl;
}
