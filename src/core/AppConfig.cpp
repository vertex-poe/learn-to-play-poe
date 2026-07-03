#include "core/AppConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>

namespace {
struct UaEntry { const char *label; const char *ua; };
static const UaEntry kUserAgents[] = {
    {"Brave (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36"},
    {"Chrome (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36"},
    {"Edge (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/149.0.0.0 Safari/537.36 Edg/149.0.0.0"},
    {"Firefox (win11)",
     "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:152.0) Gecko/20100101 Firefox/152.0"},
};
} // namespace

QString AppConfig::effectiveUserAgent() const
{
    // When debug mode is off all settings are at their defaults, which means
    // Auto (Chromium) — return empty so callers use the native engine UA.
    if (!debugMode)
        return QString();

    if (debugLegacyUserAgent.isEmpty() || debugLegacyUserAgent == QLatin1String("Auto (Chromium)"))
        return QString();

    QString ua;
    if (debugLegacyUserAgent == QLatin1String("Custom")) {
        ua = debugLegacyUserAgentCustom;
    } else {
        for (const auto &entry : kUserAgents) {
            if (debugLegacyUserAgent == QLatin1String(entry.label)) {
                ua = QLatin1String(entry.ua);
                break;
            }
        }
    }

    if (debugLegacyUserAgentApp && !ua.isEmpty()) {
        const QString token = QCoreApplication::applicationName().remove(u' ')
                              + u'/'
                              + QCoreApplication::applicationVersion();
        ua += u' ' + token;
    }

    return ua;
}

#include <toml++/toml.hpp>

#include <fstream>

#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

QString AppConfig::configPath()
{
    // --config overrides everything below with an exact file path.
    if (qApp) {
        const QStringList args = QCoreApplication::arguments();
        for (int i = 0; i < args.size(); ++i) {
            if (args[i] == QLatin1String("--config") && i + 1 < args.size())
                return args[i + 1];
        }
    }

    // If the CWD contains a Justfile it's the project root (e.g. `just run` or an IDE
    // run with CWD set to the repo).  Prefer it so the TOML and DB land there instead
    // of deep inside the build tree, making dev ergonomics nicer.
    const QString cwd = QDir::currentPath();
    if (QFile::exists(cwd + "/Justfile"))
        return cwd + "/l2p-poe.toml";

    if (qApp) {
        return QCoreApplication::applicationDirPath() + "/l2p-poe.toml";
    }

#ifdef Q_OS_WIN
    wchar_t path[MAX_PATH];
    if (GetModuleFileNameW(NULL, path, MAX_PATH)) {
        return QFileInfo(QString::fromWCharArray(path)).absolutePath() + "/l2p-poe.toml";
    }
#endif

    return QDir::currentPath() + "/l2p-poe.toml";
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
        cfg.debugMode             = tbl["debug_mode"].value_or(false);
        cfg.debugLog              = tbl["debug_log"].value_or(false);
        if (auto v = tbl["debug_legacy_user_agent"].value<std::string>())
            cfg.debugLegacyUserAgent = QString::fromStdString(*v);
        if (auto v = tbl["debug_legacy_user_agent_custom"].value<std::string>())
            cfg.debugLegacyUserAgentCustom = QString::fromStdString(*v);
        cfg.debugLegacyUserAgentApp = tbl["debug_legacy_user_agent_app"].value_or(AppConfig::kDefaultLegacyUserAgentApp);
        cfg.debugUserAgentQt        = tbl["debug_user_agent_qt"].value_or(AppConfig::kDefaultUserAgentQt);
        if (auto v = tbl["debug_info_service_host"].value<std::string>())
            cfg.debugInfoServiceHost = QString::fromStdString(*v);
        cfg.debugInfoServicePort  = (int)tbl["debug_info_service_port"].value_or<int64_t>(0);
        cfg.useGameOverlay        = tbl["use_game_overlay"].value_or(true);
        cfg.overlayColumns = tbl["overlay_columns"].value_or(1);
        cfg.overlayRows = tbl["overlay_rows"].value_or(0);
        if (cfg.overlayColumns == 0 && cfg.overlayRows == 0) {
            cfg.overlayColumns = 1;
            cfg.overlayRows = 0;
        }
        cfg.overlayShowHideout    = tbl["overlay_show_hideout"].value_or(true);
        cfg.overlayShowGuild      = tbl["overlay_show_guild"].value_or(false);
        cfg.overlayShowMenagerie  = tbl["overlay_show_menagerie"].value_or(false);
        cfg.overlayShowMonastery  = tbl["overlay_show_monastery"].value_or(false);
        cfg.overlayShowHeist      = tbl["overlay_show_heist"].value_or(false);
        cfg.overlayShowSanctum    = tbl["overlay_show_sanctum"].value_or(false);
        cfg.overlayShowLadder     = tbl["overlay_show_ladder"].value_or(false);
        cfg.overlayShowDelve      = tbl["overlay_show_delve"].value_or(false);
        cfg.overlayShowKingsmarch = tbl["overlay_show_kingsmarch"].value_or(false);
        cfg.overlayShowTimePlayed = tbl["overlay_show_timeplayed"].value_or(false);
        cfg.overlayShowCharacterAge = tbl["overlay_show_characterage"].value_or(false);
        cfg.overlayShowPassives = tbl["overlay_show_passives"].value_or(false);
        cfg.overlayShowDeaths = tbl["overlay_show_deaths"].value_or(false);
        cfg.overlayShowMonstersRemaining = tbl["overlay_show_monstersremaining"].value_or(false);
        cfg.overlayShowAtlasPassives = tbl["overlay_show_atlaspassives"].value_or(false);
        cfg.overlayShowKills = tbl["overlay_show_kills"].value_or(false);
        cfg.overlayShowResetXP = tbl["overlay_show_resetxp"].value_or(false);
        cfg.overlayShowReloadItemFilter = tbl["overlay_show_reloaditemfilter"].value_or(false);
        cfg.overlayShowL2P        = tbl["overlay_show_l2p"].value_or(true);
        cfg.autoUpdate            = tbl["auto_update"].value_or(true);
        cfg.autoStartOnBoot       = tbl["auto_start_on_boot"].value_or(false);
        cfg.defaultTab            = qBound(0, (int)tbl["default_tab"].value_or<int64_t>(6) - 1, 6);
        cfg.startMinimized        = tbl["start_minimized"].value_or(false);
        cfg.minimizeToTray        = tbl["minimize_to_tray"].value_or(true);
        cfg.autoDetectInstallDir  = tbl["auto_detect_install_dir"].value_or(true);
        cfg.showGuildTags         = tbl["show_guild_tags"].value_or(true);
        if (const auto *arr = tbl["install_dirs"].as_array()) {
            for (const auto &node : *arr) {
                if (auto val = node.value<std::string>(); val && !val->empty())
                    cfg.installDirs << QString::fromStdString(*val);
            }
        }
        if (const auto *names = tbl["chat_channel_names"].as_table()) {
            for (const auto &[key, val] : *names) {
                bool ok;
                const std::string_view ks{key};
                const int num = QString::fromUtf8(ks.data(), (int)ks.size()).toInt(&ok);
                if (ok) {
                    if (auto v = val.value<std::string>())
                        cfg.channelNames[num] = QString::fromStdString(*v);
                }
            }
        }
        if (const auto *rules = tbl["live_alert_rules"].as_array()) {
            for (const auto &ruleNode : *rules) {
                const auto *rt = ruleNode.as_table();
                if (!rt) continue;
                LiveEventRule rule;
                if (auto v = (*rt)["id"].value<std::string>())          rule.id         = QString::fromStdString(*v);
                if (auto v = (*rt)["label"].value<std::string>())        rule.label      = QString::fromStdString(*v);
                if (auto v = (*rt)["event_type"].value<std::string>())   rule.eventType  = QString::fromStdString(*v);
                if (auto v = (*rt)["action_type"].value<std::string>())  rule.actionType = QString::fromStdString(*v);
                if (auto v = (*rt)["enabled"].value<bool>())             rule.enabled    = *v;
                if (const auto *df = (*rt)["data_filter"].as_table()) {
                    for (const auto &[k, v] : *df) {
                        if (auto s = v.value<std::string>()) {
                            const std::string_view ks{k};
                            rule.dataFilter[QString::fromUtf8(ks.data(), (int)ks.size())] = QString::fromStdString(*s);
                        }
                    }
                }
                if (const auto *ap = (*rt)["action_params"].as_table()) {
                    for (const auto &[k, v] : *ap) {
                        if (auto s = v.value<std::string>()) {
                            const std::string_view ks{k};
                            rule.actionParams[QString::fromUtf8(ks.data(), (int)ks.size())] = QString::fromStdString(*s);
                        }
                    }
                }
                if (!rule.id.isEmpty())
                    cfg.liveAlertRules.append(rule);
            }
        }
        if (const auto *win = tbl["window"].as_table()) {
            cfg.windowGeometry.x      = (int)(*win)["x"].value_or<int64_t>(0);
            cfg.windowGeometry.y      = (int)(*win)["y"].value_or<int64_t>(0);
            cfg.windowGeometry.width  = (int)(*win)["width"].value_or<int64_t>(720);
            cfg.windowGeometry.height = (int)(*win)["height"].value_or<int64_t>(480);
            if (auto v = (*win)["screen"].value<std::string>())
                cfg.windowGeometry.screen = QString::fromStdString(*v);
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
    tbl.insert("debug_mode",              debugMode);
    tbl.insert("debug_log",               debugLog);
    tbl.insert("debug_legacy_user_agent",        debugLegacyUserAgent.toStdString());
    tbl.insert("debug_legacy_user_agent_custom", debugLegacyUserAgentCustom.toStdString());
    tbl.insert("debug_legacy_user_agent_app", debugLegacyUserAgentApp);
    tbl.insert("debug_user_agent_qt",            debugUserAgentQt);
    tbl.insert("debug_info_service_host",        debugInfoServiceHost.toStdString());
    tbl.insert("debug_info_service_port",        (int64_t)debugInfoServicePort);
    tbl.insert("use_game_overlay",        useGameOverlay);
    tbl.insert("overlay_columns", overlayColumns);
    tbl.insert("overlay_rows", overlayRows);
    tbl.insert("overlay_show_hideout",    overlayShowHideout);
    tbl.insert("overlay_show_guild",      overlayShowGuild);
    tbl.insert("overlay_show_menagerie",  overlayShowMenagerie);
    tbl.insert("overlay_show_monastery",  overlayShowMonastery);
    tbl.insert("overlay_show_heist",      overlayShowHeist);
    tbl.insert("overlay_show_sanctum",    overlayShowSanctum);
    tbl.insert("overlay_show_ladder",     overlayShowLadder);
    tbl.insert("overlay_show_delve",      overlayShowDelve);
    tbl.insert("overlay_show_kingsmarch", overlayShowKingsmarch);
    tbl.insert("overlay_show_timeplayed", overlayShowTimePlayed);
    tbl.insert("overlay_show_characterage", overlayShowCharacterAge);
    tbl.insert("overlay_show_passives", overlayShowPassives);
    tbl.insert("overlay_show_deaths", overlayShowDeaths);
    tbl.insert("overlay_show_monstersremaining", overlayShowMonstersRemaining);
    tbl.insert("overlay_show_atlaspassives", overlayShowAtlasPassives);
    tbl.insert("overlay_show_kills", overlayShowKills);
    tbl.insert("overlay_show_resetxp", overlayShowResetXP);
    tbl.insert("overlay_show_reloaditemfilter", overlayShowReloadItemFilter);
    tbl.insert("overlay_show_l2p",        overlayShowL2P);
    tbl.insert("auto_update",             autoUpdate);
    tbl.insert("auto_start_on_boot",      autoStartOnBoot);
    tbl.insert("default_tab",             (int64_t)(defaultTab + 1));
    tbl.insert("start_minimized",         startMinimized);
    tbl.insert("minimize_to_tray",        minimizeToTray);
    tbl.insert("auto_detect_install_dir", autoDetectInstallDir);
    tbl.insert("show_guild_tags",         showGuildTags);
    toml::array dirsArr;
    for (const QString &dir : installDirs)
        dirsArr.push_back(dir.toStdString());
    tbl.insert("install_dirs", std::move(dirsArr));

    toml::table namesTable;
    for (auto it = channelNames.constBegin(); it != channelNames.constEnd(); ++it)
        namesTable.insert(std::to_string(it.key()), it.value().toStdString());
    tbl.insert("chat_channel_names", std::move(namesTable));

    toml::array rulesArr;
    for (const LiveEventRule &rule : liveAlertRules) {
        toml::table rt;
        rt.insert("id",          rule.id.toStdString());
        rt.insert("label",       rule.label.toStdString());
        rt.insert("event_type",  rule.eventType.toStdString());
        rt.insert("action_type", rule.actionType.toStdString());
        rt.insert("enabled",     rule.enabled);

        toml::table df;
        for (auto it = rule.dataFilter.constBegin(); it != rule.dataFilter.constEnd(); ++it)
            df.insert(it.key().toStdString(), it.value().toString().toStdString());
        rt.insert("data_filter", std::move(df));

        toml::table ap;
        for (auto it = rule.actionParams.constBegin(); it != rule.actionParams.constEnd(); ++it)
            ap.insert(it.key().toStdString(), it.value().toString().toStdString());
        rt.insert("action_params", std::move(ap));

        rulesArr.push_back(std::move(rt));
    }
    tbl.insert("live_alert_rules", std::move(rulesArr));

    if (!windowGeometry.screen.isEmpty()) {
        toml::table winTbl;
        winTbl.insert("x",      (int64_t)windowGeometry.x);
        winTbl.insert("y",      (int64_t)windowGeometry.y);
        winTbl.insert("width",  (int64_t)windowGeometry.width);
        winTbl.insert("height", (int64_t)windowGeometry.height);
        winTbl.insert("screen", windowGeometry.screen.toStdString());
        tbl.insert("window", std::move(winTbl));
    }

    std::ofstream ofs(path.toStdString());
    ofs << tbl;
}
