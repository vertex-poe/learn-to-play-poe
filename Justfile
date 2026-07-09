# Learn to Play PoE — task runner
# Install: winget install Casey.Just  |  scoop install just  |  cargo install just

set windows-shell := ["powershell", "-NoLogo", "-Command"]

default_preset := if os() == "windows" { "windows-msvc" } else { "debug" }

default:
    @just --list

# Configure cmake
configure preset=default_preset:
    cmake --preset {{preset}}

# ── GUI (C++/Qt) ─────────────────────────────────────────────────────────────

# Configure + build the GUI via cmake. Note: CMakeLists.txt wires the Go
# service in as a build dependency of l2p-poe too (it's needed alongside the
# GUI binary at runtime and in ctest), so this also produces
# build/{{preset}}/src/poe-info-service(.exe) as a side effect.
[windows]
gui-build preset=default_preset:
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"; \
    $vs = & $vswhere -latest -products '*' -property installationPath; \
    $vcvars = "$vs\VC\Auxiliary\Build\vcvarsall.bat"; \
    cmd /c "`"$vcvars`" x64 >NUL 2>&1 && cmake --preset {{preset}} && cmake --build --preset {{preset}}"; \
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

[unix]
gui-build preset=default_preset:
    cmake --preset {{preset}}
    cmake --build --preset {{preset}}

# Run the GUI's ctest suite (builds first), excluding perf tests
gui-test preset=default_preset: (gui-build preset)
    ctest --preset {{preset}} --output-on-failure -LE perf

# ── Combined ─────────────────────────────────────────────────────────────────

# Build both the GUI (gui-build) and the service (service-build), then stage
# both binaries in bin/
[windows]
build preset=default_preset: (gui-build preset) (service-build)
    Remove-Item -Path "bin/l2p-poe.exe" -ErrorAction SilentlyContinue; \
    New-Item -ItemType Directory -Force -Path "bin" | Out-Null; \
    Copy-Item -Path "build/{{preset}}/src/l2p-poe.exe" -Destination "bin/" -Force -ErrorAction SilentlyContinue; \
    Copy-Item -Path "build/{{preset}}/src/*.dll" -Destination "bin/" -Force -ErrorAction SilentlyContinue

[unix]
build preset=default_preset: (gui-build preset) (service-build)
    mkdir -p bin
    cp build/{{preset}}/src/l2p-poe bin/

# Run tests for both the GUI (gui-test/ctest) and the service (service-test/go test)
test preset=default_preset: (gui-test preset) (service-test)

# Build HEAD~1's app in an isolated worktree and record it as the perf baseline.
# Called automatically by test-perf when no baseline exists.
# Worktree lives in build/ (gitignored) — your staged/unstaged changes are untouched.
[windows]
perf-baseline-prev preset=default_preset: (build preset)
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"; \
    $vs = & $vswhere -latest -products '*' -property installationPath; \
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

# Build and run the app (fails hard if either binary is missing from bin/)
[windows]
run preset=default_preset: (build preset)
    New-Item -ItemType Directory -Force -Path "bin" | Out-Null; \
    Copy-Item -Path "build/{{preset}}/src/l2p-poe.exe" -Destination "bin/" -Force -ErrorAction Stop; \
    Copy-Item -Path "build/{{preset}}/src/poe-info-service.exe" -Destination "bin/" -Force -ErrorAction Stop; \
    Copy-Item -Path "build/{{preset}}/src/*.dll" -Destination "bin/" -Force -ErrorAction SilentlyContinue; \
    bin/l2p-poe.exe

[unix]
run preset=default_preset: (build preset)
    mkdir -p bin
    cp build/{{preset}}/src/l2p-poe bin/
    cp build/{{preset}}/src/poe-info-service bin/
    bin/l2p-poe

# Install to dist/ and run windeployqt / macdeployqt via cmake --install
package preset=default_preset: (build preset)
    cmake --install build/{{preset}}

# Linux: package as AppImage (requires linuxdeployqt on PATH)
package-linux preset=default_preset: (package preset)
    linuxdeployqt dist/{{preset}}/l2p-poe -appimage

# macOS: package .app bundle into a DMG (macdeployqt is included with Qt)
package-mac preset=default_preset: (build preset)
    macdeployqt build/{{preset}}/src/l2p-poe.app -dmg

# Windows: build Inno Setup installer (requires ISCC on PATH, run after `just package`)
installer preset=default_preset: (package preset)
    ISCC installer/windows.iss

# Remove all build and dist artifacts
clean:
    cmake -E rm -rf build dist

# ── poe-info-service (Go) ────────────────────────────────────────────────────

# Build poe-info-service binary into bin/
[windows]
service-build:
    New-Item -ItemType Directory -Force -Path "bin" | Out-Null; \
    go build -C poe-info-service -o ../bin/poe-info-service.exe .

[unix]
service-build:
    mkdir -p bin
    go build -C poe-info-service -o ../bin/poe-info-service .

# Run poe-info-service tests
service-test:
    go test -C poe-info-service ./...

# Build and run the service; pass flags after `--`, e.g. just service-run -- --log-path "C:\..."
[windows]
service-run *args: (service-build)
    bin/poe-info-service.exe {{args}}

[unix]
service-run *args: (service-build)
    bin/poe-info-service {{args}}
