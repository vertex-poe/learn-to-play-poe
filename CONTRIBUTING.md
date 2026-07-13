<!-- CONTRIBUTING.md (markdown) -->

# Contributing

## Documentation

### Architecture decisions

[`docs/decisions/`](docs/decisions/) holds the architecture decision records (ADRs) that govern the project. Each ADR records the context, options considered, the decision taken, and its consequences.

[ADR-004: Game–Addon Interaction Principles](docs/decisions/004-game-addon-interaction-principles.md) is the most important one to read first: it defines the ethical and practical framework for how the addon interacts with Path of Exile and GGG infrastructure, including the feature decision checklist and the hard limits that are unconditional.

### Feature rationale

[`docs/rationales/`](docs/rationales/) holds longer-form rationale for features where the *why* is not obvious from the code. These are more discursive than ADRs — they describe the problem space and the reasoning behind specific design choices.

[Chat & Direct Messages](docs/rationales/chat.md) is the most substantive: it explains why a chat log viewer exists, the three-tier feature arc (log viewer → in-game overlay → tab-out send client), and how the shift of trade traffic to the Merchant Tab made whisper history more legible and therefore more worth building for.

### poe-info-service

[`poe-info-service/`](poe-info-service/) is a separate Go binary this app depends on for `Client.txt` parsing and GGG API access — see [ADR-006](docs/decisions/006-poe-info-service.md) for why it's a standalone service rather than app-internal logic. It has its own [CONTRIBUTING.md](poe-info-service/CONTRIBUTING.md) and its own ADRs under [`poe-info-service/docs/decisions/`](poe-info-service/docs/decisions/), since its process lifecycle, distribution, and API versioning are decided independently of any one addon that depends on it.

## How it works

`Client.txt` tailing/parsing and all database ownership live in
[`poe-info-service`](poe-info-service/), a separate Go binary this app
depends on rather than implements itself — see
[ADR-006](docs/decisions/006-poe-info-service.md) for why, and
[`poe-info-service/CONTRIBUTING.md`](poe-info-service/CONTRIBUTING.md) for how
that service works internally. This app's pipeline:

1. **`ServiceManager`** starts the shared `poe-info-service` instance (or
   connects to whichever instance already won the singleton election) and
   keeps it alive for as long as this app needs it.
2. **`PoeInfoClient`** is this app's WebSocket client to that service —
   `request()` for one-shot queries (chat/DM history, session data,
   credential storage) and `subscribe()` for live events.
3. **`LiveEventBus`** receives events forwarded from `PoeInfoClient`'s
   `clientlog` subscription and routes them through the user's configured
   rules (`LiveEventRuleEngine`), firing notifications or overlay updates.
4. **UI pages** (`ChatPage`, `DmPage`, `NotificationsPanel`, `SettingsPage`)
   query `poe-info-service` through `PoeInfoClient` or subscribe to the event
   bus for live updates. No page opens the database directly.
5. **`GameOverlay`** renders a transparent Qt window positioned over the game. Its hit-test mask is updated whenever the overlay's layout changes, keeping non-widget areas click-through.
6. **`WindowTracker`** watches the game's window position so the overlay follows it across monitors and window moves.

This app never calls GGG's web APIs or opens the SQLite database directly —
both go through `poe-info-service`, so the rate-limit budget and cache are
shared with any other addon using the same service on the machine.

## Building

The project uses C++20, Qt 6, CMake, and vcpkg. See [ADR-001](docs/decisions/001-technology-stack.md) for the full rationale behind this stack and the per-platform deployment approach (`windeployqt` on Windows, AppImage on Linux, `macdeployqt` on macOS).

### Windows setup

Run [`dev/check_setup.ps1`](dev/check_setup.ps1) at any point to check what's missing from the steps below and get the specific fix:

```powershell
powershell -File dev/check_setup.ps1
```

#### 1. Install the MSVC build tools

You only need the MSVC compiler toolchain, not the Visual Studio IDE — if you're developing in VS Code (as most contributors do), install Build Tools for Visual Studio 2022 with:

```
winget install Microsoft.VisualStudio.2022.BuildTools
```

(or download it from [visualstudio.microsoft.com/downloads](https://visualstudio.microsoft.com/downloads/), under "Tools for Visual Studio", if you don't use winget). This only installs the Visual Studio Installer itself — launch it afterward (Start menu → "Visual Studio Installer") and select the **Desktop development with C++** workload to install the actual toolchain.

If you'd rather have the full IDE too, installing [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition is fine) with the same workload works identically — both provide the same compiler, Windows SDK, and `vswhere.exe` that [`tools/msvc-env.sh`](tools/msvc-env.sh) relies on.

#### 2. Install Qt 6.11.1

1. Create a free account at [qt.io](https://www.qt.io/)
2. Once logged in, download the Qt Online Installer from [my.qt.io/download](https://my.qt.io/download)
3. Run the installer. Under **Qt for Development → Qt → Qt 6.11.1**, check:
   - **MSVC 2022 64-bit** — the compiler kit (do not select the MinGW build)
   - Under **MSVC 2022 64-bit → Additional Libraries**: **Qt WebChannel** (a required dependency of `WebEngineWidgets` — see [`CMakeLists.txt`](CMakeLists.txt)'s `find_package(Qt6 ... COMPONENTS ...)` call for the full required component list)
4. Still under **Qt 6.11.1**, expand **Extensions** and check **Qt WebEngine for Qt 6.11.1**
5. Under **Qt for Development → Qt → Build Tools**, check:
   - **MinGW 13.1.0 64-bit** (provides Unix-style shell tools)
   - **CMake**
   - **Ninja**

#### 3. Install vcpkg

Follow the [Microsoft vcpkg quickstart](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell) to clone and bootstrap vcpkg. Note the folder you install it to.

CMake ships with Qt, but if you run into version problems you can also install it separately from [cmake.org/download](https://cmake.org/download/).

#### 4. Set environment variables

Open **System Properties → Environment Variables** and add the following **user variables** (replacing `<drive>\<folder>` with wherever you installed each tool). These are all **user-level** — no system-level environment variable or `Path` change is required.

| Variable | Value |
|----------|-------|
| `QT_ROOT_DIR` | `<drive>\<folder>\Qt\6.11.1\msvc2022_64` |
| `VCPKG_ROOT` | `<drive>\<folder>\vcpkg` |

Then edit the **`Path`** user variable and add these entries:

```
%VCPKG_ROOT%
<drive>\<folder>\Qt\Tools\Ninja
<drive>\<folder>\Qt\6.11.1\msvc2022_64\bin
<drive>\<folder>\Qt\Tools\CMake_64\bin
<drive>\<folder>\Qt\Tools\mingw1310_64\bin
<drive>\<folder>\tools\_Coding\websocat
<drive>\<folder>\tools\sqlite-tools-win-x64
<drive>\Users\<you>\AppData\Local\Programs\Git\bin
```

`websocat` and the `sqlite-tools` bundle are used for manual debugging of `poe-info-service`'s WebSocket API and its SQLite database, respectively — see [poe-info-service/CONTRIBUTING.md](poe-info-service/CONTRIBUTING.md). The Git entry only needs adding if your Git for Windows install is per-user (under `AppData\Local\Programs\Git`) rather than system-wide (under `Program Files\Git`).

If you installed CMake separately instead of using the one bundled with Qt, also add:

```
<drive>\<folder>\CMake\bin
```

Restart any open terminals (and VS Code) after changing environment variables.

#### 5. Build

Open this repository in VS Code. The default terminal profile is **Git Bash (MSVC x64)**, which automatically locates your VS installation and loads the MSVC compiler environment — no Developer Command Prompt required.

```bash
# First-time configure — vcpkg downloads and builds dependencies (takes a few minutes)
cmake --preset windows-msvc

# Build
cmake --build build/windows-msvc
```

Subsequent builds are fast: Ninja skips anything unchanged and exits immediately with `ninja: no work to do.` when nothing has changed.

#### 6. Run

```bash
./build/windows-msvc/src/l2p-poe.exe
```

### `bin/`

`just build` and `just run` stage the built binaries into `bin/` at the repo root — this is your personal acceptance-testing copy, separate from the build tree. It keeps its own config (`bin/l2p-poe.toml`) and database, so a `bin/` run never interferes with — or gets clobbered by — a build-tree run, a test run, or vice versa. Only run `bin/l2p-poe.exe` when you deliberately want to acceptance-test a build; for iterative dev/debugging, run straight from `build/<preset>/src/` instead.

## Task runner

The project uses [`just`](https://just.systems/) as a task runner. Install it with `winget install Casey.Just`, `scoop install just`, or `cargo install just` (requires [Rust/cargo](https://rustup.rs/) installed first).

If you install via `cargo install just`, the binary lands in cargo's bin directory, which the Rust installer normally adds to your user `Path` automatically. If it isn't there, add it manually:

```
<drive>\Users\<you>\.cargo\bin
```

| Command | What it does |
|---------|--------------|
| `just build` | Configure (if needed) and build the project |
| `just run` | Build and launch the app |
| `just test` | Build and run all tests, excluding perf tests |
| `just test-all` | Build and run all tests including perf tests |
| `just test-perf` | Build and run perf tests; compare timing against the previous-commit baseline. Auto-generates the baseline from `HEAD~1` if none exists. |
| `just perf-baseline-prev` | Build `HEAD~1` in an isolated git worktree and record its timing as the baseline (called automatically by `test-perf` when needed). |
| `just configure` | Run CMake configure only, without building |
| `just package` | Build and package for distribution (`windeployqt` / `macdeployqt`) |
| `just package-linux` | Package as AppImage (requires `linuxdeployqt` on PATH) |
| `just package-mac` | Package as a `.app` DMG (macOS) |
| `just installer` | Build an Inno Setup installer (Windows; requires `ISCC` on PATH) |
| `just service-build` | Build only `poe-info-service`, into `bin/` |
| `just service-test` | `go test ./...` for `poe-info-service` only |
| `just service-run -- <args>` | Build and run `poe-info-service` standalone, e.g. for log-tracing without the GUI |
| `just clean` | Delete all build and dist artifacts |

`just build`, `just run`, and `just test` already include `poe-info-service` — the `service-*` recipes above are for working on it in isolation. See [`poe-info-service/CONTRIBUTING.md`](poe-info-service/CONTRIBUTING.md) for details on that service.

Run `just` with no arguments to list all available recipes.

## Database schema and API

`poe-info-service` owns the database exclusively (see its own
[ADR-006](poe-info-service/docs/decisions/006-user-config-storage.md) and
root [ADR-006](docs/decisions/006-poe-info-service.md)) and is the sole
place `l2p-poe` reads/writes game data, over its WebSocket API. See
[`poe-info-service/docs/schema.md`](poe-info-service/docs/schema.md) for a
full table-by-table reference, and
[`poe-info-service/docs/api.md`](poe-info-service/docs/api.md) for the
WebSocket API this app consumes it through.

## Measurements

[`docs/measurements/`](docs/measurements/) holds pre-implementation research and benchmarks that inform technology choices in the project.

| Document | Summary |
|---|---|
| [Database engine evaluation](docs/measurements/database.md) | SQLite3 vs DuckDB for bulk ingest and filtered reads; why SQLite was chosen and when DuckDB would become the right upgrade |
| [Dev log filtering](docs/measurements/dev_client_log_filtering.md) | Design and benchmarks for `dev/refilter_logs.py`, a development tool for trimming `Client.txt` to lines worth examining |
