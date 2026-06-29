# Learn to Play PoE1 — task runner
# Install: winget install Casey.Just  |  scoop install just  |  cargo install just

set windows-shell := ["pwsh", "-NoLogo", "-Command"]

default_preset := if os() == "windows" { "windows-msvc" } else { "debug" }

default:
    @just --list

# Configure cmake
configure preset=default_preset:
    cmake --preset {{preset}}

# Build (configures first if needed)
[windows]
build preset=default_preset:
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"; \
    $vs = & $vswhere -latest -property installationPath; \
    $vcvars = "$vs\VC\Auxiliary\Build\vcvarsall.bat"; \
    cmd /c "`"$vcvars`" x64 >NUL 2>&1 && cmake --preset {{preset}} && cmake --build --preset {{preset}}"; \
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; \
    Remove-Item -Path "bin/l2p-poe1.exe" -ErrorAction SilentlyContinue; \
    New-Item -ItemType Directory -Force -Path "bin" | Out-Null; \
    Copy-Item -Path "build/{{preset}}/src/l2p-poe1.exe" -Destination "bin/" -Force -ErrorAction SilentlyContinue; \
    Copy-Item -Path "build/{{preset}}/src/*.dll" -Destination "bin/" -Force -ErrorAction SilentlyContinue

[unix]
build preset=default_preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Run tests (builds first), excluding perf tests
test preset=default_preset: (build preset)
    ctest --preset {{preset}} --output-on-failure -LE perf

# Build HEAD~1's app in an isolated worktree and record it as the perf baseline.
# Called automatically by test-perf when no baseline exists.
# Worktree lives in build/ (gitignored) — your staged/unstaged changes are untouched.
[windows]
perf-baseline-prev preset=default_preset: (build preset)
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"; \
    $vs = & $vswhere -latest -property installationPath; \
    $vcvars = "$vs\VC\Auxiliary\Build\vcvarsall.bat"; \
    cmd /c "`"$vcvars`" x64 >NUL 2>&1 && python3 dev/perf_baseline_prev.py {{preset}}"

[unix]
perf-baseline-prev preset=default_preset: (build preset)
    python3 dev/perf_baseline_prev.py {{preset}}

# Run perf tests, compare against previous commit (builds first).
# Baseline is machine-local (build/<preset>/perf_baseline.json, never committed).
# If no baseline exists, HEAD~1 is built automatically to generate one.
# CI: restore the baseline from cache before running; save new results after.
[windows]
test-perf preset=default_preset: (build preset)
    if (-not (Test-Path "build/{{preset}}/perf_baseline.json")) { just perf-baseline-prev {{preset}} }; \
    ctest --preset {{preset}} --output-on-failure -L perf; \
    $rc = $LASTEXITCODE; \
    python3 dev/perf_compare.py "build/{{preset}}/perf_baseline.json" "build/{{preset}}/perf_results.json"; \
    if ($LASTEXITCODE -ne 0 -and $rc -eq 0) { $rc = $LASTEXITCODE }; \
    if (Test-Path "build/{{preset}}/perf_results.json") { cmake -E copy "build/{{preset}}/perf_results.json" "build/{{preset}}/perf_baseline.json" }; \
    exit $rc

[unix]
test-perf preset=default_preset: (build preset)
    [ -f build/{{preset}}/perf_baseline.json ] || just perf-baseline-prev {{preset}}; \
    ctest --preset {{preset}} --output-on-failure -L perf; rc=$?; \
    python3 dev/perf_compare.py build/{{preset}}/perf_baseline.json build/{{preset}}/perf_results.json; cmp_rc=$?; \
    [ $cmp_rc -ne 0 ] && [ $rc -eq 0 ] && rc=$cmp_rc; \
    [ -f build/{{preset}}/perf_results.json ] && cmake -E copy build/{{preset}}/perf_results.json build/{{preset}}/perf_baseline.json; \
    exit $rc

# Run all tests including perf (builds first)
test-all preset=default_preset: (build preset)
    ctest --preset {{preset}} --output-on-failure

# Run the reference basic performance test
test-ref-basic preset=default_preset: (build preset)
    ctest --preset {{preset}} -R test_ref_basic -V

# Run the reference data performance test
test-ref-data preset=default_preset: (build preset)
    ctest --preset {{preset}} -R test_ref_data -V

# Configure + build + test in one shot
all preset=default_preset: (test preset)

# Build and run the app
[windows]
run preset=default_preset: (build preset)
    Copy-Item -Path "build/{{preset}}/src/l2p-poe1.exe" -Destination "bin/" -Force -ErrorAction Stop; \
    Copy-Item -Path "build/{{preset}}/src/*.dll" -Destination "bin/" -Force -ErrorAction SilentlyContinue; \
    bin/l2p-poe1.exe

[unix]
run preset=default_preset: (build preset)
    build/{{preset}}/src/l2p-poe1

# Install to dist/ and run windeployqt / macdeployqt via cmake --install
package preset=default_preset: (build preset)
    cmake --install build/{{preset}}

# Linux: package as AppImage (requires linuxdeployqt on PATH)
package-linux preset=default_preset: (package preset)
    linuxdeployqt dist/{{preset}}/l2p-poe1 -appimage

# macOS: package .app bundle into a DMG (macdeployqt is included with Qt)
package-mac preset=default_preset: (build preset)
    macdeployqt build/{{preset}}/src/l2p-poe1.app -dmg

# Windows: build Inno Setup installer (requires ISCC on PATH, run after `just package`)
installer preset=default_preset: (package preset)
    ISCC installer/windows.iss

# Remove all build and dist artifacts
clean:
    cmake -E rm -rf build dist
