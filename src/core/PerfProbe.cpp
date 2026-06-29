#include "core/PerfProbe.h"
#include "ui/NavBar.h"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <QWidget>
#include <cstdio>
#include <cstdlib>

PerfProbe &PerfProbe::instance()
{
    static PerfProbe s;
    return s;
}

void PerfProbe::startClock()
{
    if (!m_timer.isValid()) {
        m_timer.start();
        m_lastAbsMs = 0;
    }
}

void PerfProbe::enable(Scenario scenario, int defaultNavIdx, int swapNavIdx,
                       const QString &runJsonPath)
{
    m_enabled       = true;
    m_scenario      = scenario;
    m_defaultNavIdx = defaultNavIdx;
    m_swapNavIdx    = swapNavIdx;
    m_runJsonPath   = runJsonPath;
    m_state            = State::WaitFirstPaint;
    m_dataLoadedEarly  = false;
    if (!m_timer.isValid()) {
        m_timer.start();
        m_lastAbsMs = 0;
    }
}

void PerfProbe::publishHitboxesAndConfig(NavBar *navBar, QWidget *mainWindow)
{
    if (!m_enabled) return;
    const quintptr hwnd = (quintptr)mainWindow->winId();
    for (int i = 0; i < navBar->labelCount(); ++i) {
        const QRect r = navBar->tabRect(i);
        const QPoint center = navBar->mapToGlobal(r.center());
        char buf[128];
        std::snprintf(buf, sizeof(buf), "PERF:hitbox:%d:%d:%d:%llu\n",
                      i, center.x(), center.y(),
                      static_cast<unsigned long long>(hwnd));
        fputs(buf, stdout);
    }
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "PERF:config:default_nav_idx:%d\nPERF:config:swap_nav_idx:%d\nPERF:config:scenario:%s\n",
                  m_defaultNavIdx, m_swapNavIdx,
                  m_scenario == Scenario::Baseline ? "baseline" : "swap_early");
    fputs(buf, stdout);
    fflush(stdout);
}

void PerfProbe::mark(const char *name)
{
    const qint64 absMs = m_timer.elapsed();
    const qint64 delta = absMs - m_lastAbsMs;
    m_lastAbsMs = absMs;

    if (qstrcmp(name, "first_paint") == 0) {
        m_firstPaintMs = absMs;
    }

    const qint64 deltaFromPaint = (m_firstPaintMs > 0) ? (absMs - m_firstPaintMs) : 0;

    m_milestones[QLatin1String(name)] = {absMs, delta, deltaFromPaint};
    m_order.append(QLatin1String(name));

    char buf[128];
    std::snprintf(buf, sizeof(buf), "PERF:%s:%lld:%lld:%lld\n", name,
                  static_cast<long long>(absMs), static_cast<long long>(delta), static_cast<long long>(deltaFromPaint));
    fputs(buf, stdout);
    fflush(stdout);
}

void PerfProbe::onNavBarFirstPaint()
{
    if (!m_enabled || m_navBarPainted) return;
    m_navBarPainted = true;
    if (m_state != State::WaitFirstPaint) return;

    mark("first_paint");
    m_state = State::WaitFirstInteract;
}

void PerfProbe::onNavBarMousePress(int navTabIdx)
{
    if (!m_enabled) return;

    if (m_state == State::WaitFirstInteract) {
        if (navTabIdx == m_defaultNavIdx) {
            mark("first_interaction");
            if (m_scenario == Scenario::SwapEarly) {
                m_state = State::WaitSwapPaint;
            } else {
                m_state = State::WaitFirstLoad;
                if (m_isPlaceholder || m_dataLoadedEarly) {
                    // Fire first_load on the next tick: placeholders have no async
                    // data fetch; content pages where data pre-loaded need the same.
                    QTimer::singleShot(0, [this]() { onDefaultPageLoaded(); });
                }
            }
        }
        return;
    }

    if (m_state == State::WaitFinalInteract) {
        if (navTabIdx == m_swapNavIdx) {
            mark("final_interaction");
            m_state = State::WaitSwapPaint;
        }
        return;
    }
}

void PerfProbe::onDefaultPageLoaded()
{
    if (!m_enabled) return;

    // Data arrived before the user clicked (or even before first paint) — remember
    // it so first_load fires immediately when first_interaction eventually arrives.
    if (m_state == State::WaitFirstPaint || m_state == State::WaitFirstInteract) {
        m_dataLoadedEarly = true;
        return;
    }

    if (m_state != State::WaitFirstLoad) return;

    mark("first_load");
    m_state = State::WaitFinalPaint;

    // For pages where opaque children cover the entire widget surface, Qt never
    // delivers QEvent::Paint to the widget itself — PaintProbeFilter won't fire.
    // In those cases (m_directFinalPaint=true), call onDefaultPagePainted() now;
    // the state is already WaitFinalPaint so it will record the milestone.
    if (m_directFinalPaint) {
        onDefaultPagePainted();
    } else if (m_defaultPageWidget) {
        m_defaultPageWidget->update();
    }
}

void PerfProbe::onDefaultPagePainted()
{
    if (!m_enabled || m_state != State::WaitFinalPaint) return;

    mark("final_paint");
    m_state = State::WaitFinalInteract;
}

void PerfProbe::onSwapPagePainted()
{
    if (!m_enabled || m_state != State::WaitSwapPaint) return;

    const char *name = (m_scenario == Scenario::SwapEarly)
        ? "menu_swap_early" : "menu_swap_final";
    mark(name);
    m_state = State::Done;

    writeResultsAndQuit();
}

void PerfProbe::writeResultsAndQuit()
{
    QJsonObject milestones;
    for (const QString &key : m_order) {
        const Milestone &ms = m_milestones[key];
        QJsonObject entry;
        entry["abs_ms"]   = ms.absMs;
        entry["delta_ms"] = ms.deltaMs;
        milestones[key] = entry;
    }

    QJsonObject root;
    root["scenario"]        = (m_scenario == Scenario::Baseline)
                              ? QLatin1String("baseline") : QLatin1String("swap_early");
    root["default_nav_idx"] = m_defaultNavIdx;
    root["swap_nav_idx"]    = m_swapNavIdx;
    root["milestones"]      = milestones;

    if (!m_runJsonPath.isEmpty()) {
        QFile f(m_runJsonPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(QJsonDocument(root).toJson());
        }
    }

    fputs("PERF:done\n", stdout);
    fflush(stdout);

    std::quick_exit(0);
}
