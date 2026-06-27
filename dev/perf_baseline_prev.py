#!/usr/bin/env python3
"""Build HEAD~1's app in an isolated worktree and run the perf test against it.

This records a true apples-to-apples baseline: same machine, same runner,
previous commit vs current commit — nothing else differs.

Usage (called automatically by `just test-perf` when no baseline exists):
    python3 dev/perf_baseline_prev.py <preset>

On Windows call via `just perf-baseline-prev` so MSVC is in the environment.
The worktree is placed inside build/ (already gitignored) so your working
tree, staged changes, and unstaged changes are completely untouched.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path


def run(cmd: list, check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    print(f"  $ {' '.join(str(c) for c in cmd)}")
    result = subprocess.run(cmd, **kwargs)
    if check and result.returncode != 0:
        print(f"  FAILED (exit {result.returncode})", file=sys.stderr)
        sys.exit(result.returncode)
    return result


def main(preset: str) -> int:
    root = Path(__file__).parent.parent.resolve()
    worktree  = root / "build" / "perf-prev-worktree"
    prev_build = root / "build" / f"{preset}-prev"
    prev_bin  = prev_build / "bin"
    exe_name  = "l2p-poe1.exe" if sys.platform == "win32" else "l2p-poe1"
    prev_app  = prev_bin / exe_name

    results_path  = root / "build" / preset / "perf_results.json"
    baseline_path = root / "build" / preset / "perf_baseline.json"

    # Find the current test binary (built by `just build` before we were called).
    test_name = "test_startup_timing.exe" if sys.platform == "win32" else "test_startup_timing"
    test_bin = root / "build" / preset / "src" / test_name
    if not test_bin.exists():
        # Some presets put all binaries in src/, others use the build root.
        for candidate in prev_build.rglob(test_name):
            test_bin = candidate
            break
    # Try the known output dir used by the windows-msvc / ci presets.
    if not test_bin.exists():
        test_bin = root / "build" / preset / test_name
    if not test_bin.exists():
        print(f"Error: test binary not found. Run `just build` first.", file=sys.stderr)
        return 1

    # Verify HEAD~1 exists.
    result = run(["git", "rev-parse", "HEAD~1"], cwd=root, capture_output=True, text=True)
    prev_sha = result.stdout.strip()
    cur_sha = run(["git", "rev-parse", "--short", "HEAD"],
                  cwd=root, capture_output=True, text=True).stdout.strip()
    print(f"\n[perf-baseline-prev] HEAD = {cur_sha}, measuring HEAD~1 = {prev_sha[:8]}")

    # Clean up any leftover worktree from a previous interrupted run.
    if worktree.exists():
        print(f"\n[perf-baseline-prev] Removing stale worktree {worktree}")
        run(["git", "worktree", "remove", str(worktree), "--force"], cwd=root, check=False)
        shutil.rmtree(worktree, ignore_errors=True)

    print(f"\n[perf-baseline-prev] Creating worktree at {worktree}")
    run(["git", "worktree", "add", str(worktree), "HEAD~1"], cwd=root)

    try:
        print(f"\n[perf-baseline-prev] Configuring HEAD~1 app ({preset})...")
        run([
            "cmake", f"--preset={preset}",
            f"-S={worktree}",
            f"-B={prev_build}",
            # Force the exe into a known location regardless of preset overrides.
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY={prev_bin}",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG={prev_bin}",
            f"-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE={prev_bin}",
        ], cwd=root)

        print(f"\n[perf-baseline-prev] Building l2p-poe1 from HEAD~1...")
        run(["cmake", "--build", str(prev_build), "--target", "l2p-poe1"], cwd=root)

        if not prev_app.exists():
            print(f"Error: expected app at {prev_app} after build", file=sys.stderr)
            return 1

        print(f"\n[perf-baseline-prev] Running perf test against HEAD~1 app...")
        env = os.environ.copy()
        env["L2P_STARTUP_TIMING_EXE"] = str(prev_app)
        run([str(test_bin)], env=env, cwd=root)

        if not results_path.exists():
            print(f"Error: test did not write results to {results_path}", file=sys.stderr)
            return 1

        shutil.copy(results_path, baseline_path)
        print(f"\n[perf-baseline-prev] Baseline saved ({prev_sha[:8]} → {baseline_path.name})")

    finally:
        print(f"\n[perf-baseline-prev] Cleaning up...")
        shutil.rmtree(prev_build, ignore_errors=True)
        run(["git", "worktree", "remove", str(worktree), "--force"], cwd=root, check=False)

    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} <preset>", file=sys.stderr)
        sys.exit(1)
    sys.exit(main(sys.argv[1]))
