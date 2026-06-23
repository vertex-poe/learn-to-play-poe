#include "AppConfig.h"

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
    QString ua;
    if (userAgentLabel == QLatin1String("Custom")) {
        ua = customUserAgent;
    } else {
        for (const auto &entry : kUserAgents) {
            if (userAgentLabel == QLatin1String(entry.label)) {
                ua = QLatin1String(entry.ua);
                break;
            }
        }
    }

    if (includeToolName && !ua.isEmpty()) {
        const QString token = QCoreApplication::applicationName().remove(u' ')
                              + u'/'
                              + QCoreApplication::applicationVersion();
        ua += u' ' + token;
    }

    return ua;
}

#include <toml++/toml.hpp>

#include <fstream>

QString AppConfig::configPath()
{
    // If the CWD contains a Justfile it's the project root (e.g. `just run` or an IDE
    // run with CWD set to the repo).  Prefer it so the TOML and DB land there instead
    // of deep inside the build tree, making dev ergonomics nicer.
    const QString cwd = QDir::currentPath();
    if (QFile::exists(cwd + "/Justfile"))
        return cwd + "/l2p-poe1.toml";

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
        cfg.debugLog              = tbl["debug_log"].value_or(false);
        if (auto v = tbl["user_agent_label"].value<std::string>())
            cfg.userAgentLabel = QString::fromStdString(*v);
        if (auto v = tbl["custom_user_agent"].value<std::string>())
            cfg.customUserAgent = QString::fromStdString(*v);
        cfg.includeToolName       = tbl["include_tool_name"].value_or(true);
        cfg.useGameOverlay        = tbl["use_game_overlay"].value_or(true);
        cfg.autoUpdate            = tbl["auto_update"].value_or(true);
        cfg.autoStartOnBoot       = tbl["auto_start_on_boot"].value_or(false);
        cfg.defaultTab            = (int)tbl["default_tab"].value_or<int64_t>(2) - 1;
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
    tbl.insert("debug_log",               debugLog);
    tbl.insert("user_agent_label",        userAgentLabel.toStdString());
    tbl.insert("custom_user_agent",       customUserAgent.toStdString());
    tbl.insert("include_tool_name",       includeToolName);
    tbl.insert("use_game_overlay",        useGameOverlay);
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
