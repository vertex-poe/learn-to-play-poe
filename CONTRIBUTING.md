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

The project uses C++17, Qt 6, CMake, and vcpkg. See [ADR-001](docs/decisions/001-technology-stack.md) for the full rationale behind this stack and the per-platform deployment approach (`windeployqt` on Windows, AppImage on Linux, `macdeployqt` on macOS).

Build instructions will be documented before the first public release.

## Database schema

[`docs/schema.md`](docs/schema.md) documents every table in the SQLite database — reference/lookup tables, sessions, movement, character progression, social/chat, game events, the app-state store, and the event history spine — along with the design patterns used throughout (install-scoping, reference normalization, idempotent ingest).

## Measurements

[`docs/measurements/`](docs/measurements/) holds pre-implementation research and benchmarks that inform technology choices in the project.

| Document | Summary |
|---|---|
| [Database engine evaluation](docs/measurements/database.md) | SQLite3 vs DuckDB for bulk ingest and filtered reads; why SQLite was chosen and when DuckDB would become the right upgrade |
| [Dev log filtering](docs/measurements/dev_client_log_filtering.md) | Design and benchmarks for `dev/refilter_logs.py`, a development tool for trimming `Client.txt` to lines worth examining |
