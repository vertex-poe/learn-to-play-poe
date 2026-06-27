#pragma once

// UI responsiveness budgets for learn-to-play-poe1.
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

} // namespace PerfTargets
