#pragma once

// UI responsiveness budgets for learn-to-play-poe.
//
// These are wall-clock milliseconds measured from a defined start event to a
// defined end event. Tests use the median of several runs to dampen noise.
// A test is considered "sluggish" — and must fail — if the median exceeds the
// threshold below. Thresholds are intentionally platform-agnostic: if a value
// passes on a slow CI runner it will pass everywhere.
//
// IMPORTANT: keep these values in sync with tests/perf_targets.json, which is
// read by dev/perf_compare.py for delta regression checks.
//
// When raising a threshold, document why it was raised and what the new
// baseline measurement is so future regressions are detectable.

namespace PerfTargets {

// From OS process creation (QProcess::start) to the session list being
// populated in LogPage and ready to paint.
// Baseline (2026-06-27, Windows 11, MSVC, empty DB): ~1 500 ms cold.
// perf_targets.json: threshold_ms=5000, max_regression_ms=1000
constexpr int kStartupToSessionListMs = 5'000;

// Fine-grained browser-style startup milestones (test_perf_metrics).
// All thresholds are absolute ms from the start of PerfProbe::enable() in main(),
// measured by the median of 3 runs per configuration.
//
// "content" tabs: Chats (dt=1), DMs (dt=2), Current (dt=5), Log (dt=6) — data
//   arrives via an async DB query; first_load fires when the query callback
//   populates the scroll widget.
// "placeholder" tabs: Guide (dt=0), Stash (dt=3), Profile (dt=4) — show a static
//   "Coming soon" label; first_load fires immediately after first_interaction.
//
// Loose initial values — tighten each one once its baseline is known.
namespace Perf {
    // Both tab types — NavBar must paint and accept a click.
    constexpr int kFirstPaint       = 8'000;
    constexpr int kFirstInteraction = 10'000;

    // Content tabs: time to fetch + render the first batch of records.
    constexpr int kFirstLoadContent = 12'000;

    // Placeholder tabs: first_load is near-instant (one event-loop tick).
    constexpr int kFirstLoadPlaceholder = 11'000;

    // After first_load: time for the page widget to process its forced repaint.
    constexpr int kFinalPaint       = 13'000;

    // final_interaction = the swap-target click landing in mousePressEvent.
    constexpr int kFinalInteraction = 14'000;

    // Time from launch to when the swap-target page first paints
    // (baseline scenario: click happens after final_paint).
    constexpr int kMenuSwapFinal    = 15'000;

    // Time from launch to when the swap-target page first paints
    // (swap_early scenario: click happens right after first_interaction).
    constexpr int kMenuSwapEarly    = 12'000;
} // namespace Perf

} // namespace PerfTargets
