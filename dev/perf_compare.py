#!/usr/bin/env python3
"""Compare two perf_results JSON files and report deltas.

Usage:
    python3 dev/perf_compare.py tests/perf_baseline.json build/<preset>/perf_results.json

Exits non-zero if any metric's current value exceeds its absolute threshold
(threshold_ms) or if any metric has regressed more than max_regression_ms
relative to the baseline. Thresholds are read from tests/perf_targets.json.
"""

import json
import sys
from pathlib import Path


def load(path: str) -> dict | None:
    p = Path(path)
    if not p.exists():
        return None
    with open(p) as f:
        return json.load(f)


def median_of(entry) -> int | None:
    if isinstance(entry, dict):
        return entry.get("median")
    if isinstance(entry, (int, float)):
        return int(entry)
    return None


def main(baseline_path: str, current_path: str) -> int:
    targets_path = Path(__file__).parent.parent / "tests" / "perf_targets.json"
    targets = load(str(targets_path)) or {}

    current = load(current_path)
    if current is None:
        print(f"No perf results at {current_path} — run `just test-perf` first.")
        return 0

    baseline = load(baseline_path)
    if baseline is None:
        print(f"\nNo baseline at {baseline_path} — run `just perf-accept` to record one.")
        c_commit = current.get("commit", "unknown")
        print(f"Current results ({c_commit}):")
        for k, v in current.get("metrics", {}).items():
            val = median_of(v)
            print(f"  {k}: {val} ms" if val is not None else f"  {k}: —")
        return 0

    b_commit = baseline.get("commit", "?")[:8]
    c_commit = current.get("commit", "?")[:8]
    b_ts = baseline.get("timestamp", "")[:10]
    c_ts = current.get("timestamp", "")[:10]

    print(f"\nPerf delta  {b_commit} {b_ts} (baseline)  →  {c_commit} {c_ts} (current)")
    print(f"  {'Metric':<45} {'Baseline':>10} {'Current':>10} {'Delta':>10} {'Change':>9}  Status")
    print("  " + "-" * 97)

    b_metrics = baseline.get("metrics", {})
    c_metrics = current.get("metrics", {})
    all_keys = sorted(set(b_metrics) | set(c_metrics))

    failures: list[str] = []

    for key in all_keys:
        b_val = median_of(b_metrics.get(key))
        c_val = median_of(c_metrics.get(key))
        t = targets.get(key, {})
        threshold = t.get("threshold_ms")
        max_reg   = t.get("max_regression_ms")

        b_str = f"{b_val} ms" if b_val is not None else "—"
        c_str = f"{c_val} ms" if c_val is not None else "—"

        if b_val is not None and c_val is not None:
            delta = c_val - b_val
            pct   = delta / b_val * 100 if b_val else 0
            sign  = "+" if delta > 0 else ""
            delta_str = f"{sign}{delta} ms"
            pct_str   = f"{sign}{pct:.1f}%"
        else:
            delta = None
            delta_str = pct_str = "—"

        status = "ok"
        if c_val is not None and threshold is not None and c_val > threshold:
            status = f"FAIL (>{threshold} ms)"
            failures.append(f"{key}: {c_val} ms exceeds threshold {threshold} ms")
        elif delta is not None and max_reg is not None and delta > max_reg:
            status = f"FAIL (regression >{max_reg} ms)"
            failures.append(f"{key}: regressed {delta} ms, limit is {max_reg} ms")

        print(f"  {key:<45} {b_str:>10} {c_str:>10} {delta_str:>10} {pct_str:>9}  {status}")

    print()

    if failures:
        print("Perf regressions detected:")
        for f in failures:
            print(f"  • {f}")
        print()
        return 1

    return 0


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} <baseline.json> <current.json>", file=sys.stderr)
        sys.exit(1)
    sys.exit(main(sys.argv[1], sys.argv[2]))
