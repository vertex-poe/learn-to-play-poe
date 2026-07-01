<!-- CONTRIBUTING.md (markdown) -->

# Contributing

## Documentation

### Architecture decisions

[`docs/decisions/`](docs/decisions/) holds the architecture decision records (ADRs) that govern the project. Each ADR records the context, options considered, the decision taken, and its consequences.

[ADR-004: Game–Addon Interaction Principles](docs/decisions/004-game-addon-interaction-principles.md) is the most important one to read first: it defines the ethical and practical framework for how the addon interacts with Path of Exile and GGG infrastructure, including the feature decision checklist and the hard limits that are unconditional.

### Feature rationale

[`docs/rationales/`](docs/rationales/) holds longer-form rationale for features where the *why* is not obvious from the code. These are more discursive than ADRs — they describe the problem space and the reasoning behind specific design choices.

[Chat & Direct Messages](docs/rationales/chat.md) is the most substantive: it explains why a chat log viewer exists, the three-tier feature arc (log viewer → in-game overlay → tab-out send client), and how the shift of trade traffic to the Merchant Tab made whisper history more legible and therefore more worth building for.

## How it works

The core pipeline is short:

1. **`LogIngestWorker`** tails `Client.txt`, parsing new lines as they arrive and writing structured rows into SQLite via `Database`. Ingest is idempotent — re-reading the same file is always safe.
2. **`LiveEventBus`** receives parsed events in real time and routes them through the user's configured rules (`LiveEventRuleEngine`), firing notifications or overlay updates.
3. **UI pages** (`ChatPage`, `DmPage`, `NotificationsPanel`, `SettingsPage`) query the database directly or subscribe to the event bus for live updates.
4. **`GameOverlay`** renders a transparent Qt window positioned over the game. Its hit-test mask is updated whenever the overlay's layout changes, keeping non-widget areas click-through.
5. **`WindowTracker`** watches the game's window position so the overlay follows it across monitors and window moves.

All data originates from `Client.txt`. There is no network access, no GGG API calls, and no game-process interaction of any kind beyond reading a file the game writes itself.

## Building

The project uses C++20, Qt 6, CMake, and vcpkg. See [ADR-001](docs/decisions/001-technology-stack.md) for the full rationale behind this stack and the per-platform deployment approach (`windeployqt` on Windows, AppImage on Linux, `macdeployqt` on macOS).

### Windows setup

#### 1. Install Visual Studio 2022

Download and install [Visual Studio 2022](https://visualstudio.microsoft.com/) (Community edition is fine). During installation, select the **Desktop development with C++** workload.

#### 2. Install Qt 6.11.1

1. Create a free account at [qt.io](https://www.qt.io/)
2. Once logged in, download the Qt Online Installer from [my.qt.io/download](https://my.qt.io/download)
3. Run the installer. Under **Qt for Development → Qt → Qt 6.11.1**, check:
   - **MSVC 2022 64-bit** — the compiler kit (do not select the MinGW build)
   - Under **MSVC 2022 64-bit → Additional Libraries**: **Qt Positioning**
4. Still under **Qt 6.11.1**, expand **Extensions** and check **Qt WebEngine for Qt 6.11.1**
5. Under **Qt for Development → Qt → Build Tools**, check:
   - **MinGW 13.1.0 64-bit** (provides Unix-style shell tools)
   - **CMake**
   - **Ninja**

#### 3. Install vcpkg

Follow the [Microsoft vcpkg quickstart](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-powershell) to clone and bootstrap vcpkg. Note the folder you install it to.

CMake ships with Qt, but if you run into version problems you can also install it separately from [cmake.org/download](https://cmake.org/download/).

#### 4. Set environment variables

Open **System Properties → Environment Variables** and add the following **user variables** (replacing `<drive>\<folder>` with wherever you installed each tool):

| Variable | Value |
|----------|-------|
| `QT_ROOT_DIR` | `<drive>\<folder>\Qt\6.11.1\msvc2022_64` |
| `VCPKG_ROOT` | `<drive>\<folder>\vcpkg` |

Then edit the **`Path`** user variable and add these entries:

```
%VCPKG_ROOT%
<drive>\<folder>\Qt\Tools\Ninja
<drive>\<folder>\Qt\6.11.1\msvc2022_64\bin
<drive>\<folder>\Qt\Tools\mingw1310_64\bin
```

If you installed CMake separately, also add:

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

## Task runner

The project uses [`just`](https://just.systems/) as a task runner. Install it with `winget install Casey.Just`, `scoop install just`, or `cargo install just`.

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
| `just clean` | Delete all build and dist artifacts |

Run `just` with no arguments to list all available recipes.

## Database schema

[`docs/schema.md`](docs/schema.md) documents every table in the SQLite database — reference/lookup tables, sessions, movement, character progression, social/chat, game events, the app-state store, and the event history spine — along with the design patterns used throughout (install-scoping, reference normalization, idempotent ingest).

## Measurements

[`docs/measurements/`](docs/measurements/) holds pre-implementation research and benchmarks that inform technology choices in the project.

| Document | Summary |
|---|---|
| [Database engine evaluation](docs/measurements/database.md) | SQLite3 vs DuckDB for bulk ingest and filtered reads; why SQLite was chosen and when DuckDB would become the right upgrade |
| [Dev log filtering](docs/measurements/dev_client_log_filtering.md) | Design and benchmarks for `dev/refilter_logs.py`, a development tool for trimming `Client.txt` to lines worth examining |
