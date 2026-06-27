#pragma once

#include <QElapsedTimer>
#include <QMap>
#include <QString>
#include <QStringList>

class QWidget;
class NavBar;

// Fine-grained startup performance probe.
// Enabled when L2P_PERF_MODE=1 is set. Publishes PERF: markers to stdout and
// writes a per-run JSON file at the path supplied via enable().
class PerfProbe
{
public:
    enum class Scenario { Baseline, SwapEarly };

    static PerfProbe &instance();

    // Enable and start the reference clock. Call once from main() after parsing
    // CLI flags, before QApplication is created.
    void enable(Scenario scenario, int defaultNavIdx, int swapNavIdx,
                const QString &runJsonPath);

    bool     enabled()       const { return m_enabled; }
    Scenario scenario()      const { return m_scenario; }
    int      defaultNavIdx() const { return m_defaultNavIdx; }
    int      swapNavIdx()    const { return m_swapNavIdx; }

    // Must be called by MainWindow before the window is shown. Publishes
    // PERF:hitbox:<navIdx>:<screenX>:<screenY>:<mainHwnd> lines plus
    // PERF:config:... lines so the test process knows where to click.
    void publishHitboxesAndConfig(NavBar *navBar, QWidget *mainWindow);

    // Widget hooks — called from page-widget paint events and data callbacks.
    void setDefaultPageWidget(QWidget *w) { m_defaultPageWidget = w; }
    // Placeholder pages have no async data load; first_load fires automatically
    // right after first_interaction instead of waiting for a dataLoaded signal.
    void setIsPlaceholderPage(bool p)    { m_isPlaceholder = p; }
    // When true, onDefaultPageLoaded() calls onDefaultPagePainted() directly
    // instead of relying on QEvent::Paint via PaintProbeFilter. Use for pages
    // where the widget hierarchy prevents paint events from reaching the widget
    // (e.g. SessionViewPage, where m_content covers the scroll viewport entirely).
    void setDirectFinalPaint(bool d)     { m_directFinalPaint = d; }

    void onNavBarFirstPaint();
    void onNavBarMousePress(int navTabIdx);
    void onDefaultPageLoaded();
    void onDefaultPagePainted();
    void onSwapPagePainted();

private:
    PerfProbe() = default;

    enum class State {
        WaitFirstPaint,
        WaitFirstInteract,
        WaitFirstLoad,
        WaitFinalPaint,
        WaitFinalInteract,
        WaitSwapPaint,
        Done,
    };

    void mark(const char *name);
    void writeResultsAndQuit();

    bool      m_enabled{false};
    Scenario  m_scenario{Scenario::Baseline};
    State     m_state{State::WaitFirstPaint};

    QElapsedTimer m_timer;
    int           m_defaultNavIdx{0};
    int           m_swapNavIdx{0};
    QString       m_runJsonPath;

    struct Milestone { qint64 absMs{-1}; qint64 deltaMs{-1}; };
    QMap<QString, Milestone> m_milestones;
    QStringList              m_order;
    qint64                   m_lastAbsMs{0};

    bool    m_navBarPainted{false};
    bool    m_isPlaceholder{false};
    bool    m_dataLoadedEarly{false};   // dataLoaded fired before first_interaction
    bool    m_directFinalPaint{false};  // bypass paint-event detection for final_paint
    QWidget *m_defaultPageWidget{nullptr};
};
