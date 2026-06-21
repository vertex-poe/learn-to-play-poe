<!-- README.md (markdown) -->

# Learn to Play: Path of Exile 1

> [!NOTE]
> This project is not affiliated with, endorsed by, or in any way associated with Grinding Gear Games or Path of Exile. Path of Exile is a trademark of Grinding Gear Games Ltd.

A desktop companion for Path of Exile 1 that surfaces and guides the information the game already records — sessions, chat, events, whispers — in a clean, structured interface alongside your game. Nothing is read from game memory or sent to GGG's servers without a direct player action; the app is a passive observer of what the aPI and `Client.txt` already contains.


---

***[Issues](https://github.com/vertex-poe1/learn-to-play-poe1/issues)* are only for bugs with the intended functionality of an existing implemented feature, for everything else use *[Discussions](https://github.com/vertex-poe1/learn-to-play-poe1/discussions)*, including feature requests, installation and program support.**

---

## Features

**Session tracking.** Every play session is captured automatically — login time, characters played, areas visited, time per area, and AFK intervals. Level-ups, deaths, quest completions, achievements, and hideout discoveries are indexed and browsable.

**Chat history.** Public chat (Global, Trade, Party, Guild) and direct messages (whispers) are logged and searchable. A unified timeline shows all channels together with colored accents and channel badges; per-channel checkboxes and date-bucket navigation (Today, Yesterday, This Week, then individual dates) make it easy to find a conversation that was lost when the client restarted. Pagination keeps the initial load fast without discarding history.

**Direct messages.** The DM view presents whisper conversations in a threaded layout oriented toward reading a conversation, not skimming a log. With more trade traffic flowing through the Merchant Tab's passive listings, whisper history is cleaner than it used to be — making a purpose-built viewer for it more valuable.

**Game overlay.** A transparent, always-on-top window renders UI elements over the game without affecting gameplay. The overlay uses per-region hit-testing: background areas pass clicks through to the game; widget areas capture input normally. Requires windowed or borderless-windowed mode.

**Live event triggers.** A real-time event system fires as `Client.txt` is tailed. User-configurable rules map events (level-ups, area transitions, incoming whispers, deaths) to actions (notifications, sounds, overlay updates) without any polling.

---

## Installation

The project has not yet reached its first public release. Pre-built installers are not available yet.

Follow [ROADMAP.md](ROADMAP.md) for progress toward the initial public build.

---

## Platform support

| Platform | Status |
|---|---|
| Windows 11 | Active development |
| Steam Deck (SteamOS / KDE desktop mode) | Targeted |
| macOS | Planned |
| Linux (desktop) | Partial (window tracking implemented) |

---

## Documentation

We explain most of our [`docs/decisions/`](big decisions) — start with [ADR-004: Game–Addon Interaction Principles](docs/decisions/004-game-addon-interaction-principles.md), which sets out our commitment to respecting player agency and GGG's intent. We also cover the reasoning behind our features in [dedicated rationale documents](docs/rationale/), like our commitment to delivering balanced [community chat improvements](docs/rationale/chat.md). Upcoming aspirational work is tracked in [ROADMAP.md](ROADMAP.md).

---

## Contributing

[CONTRIBUTING.md](CONTRIBUTING.md) covers how the pipeline fits together, how to build the project, and the pre-implementation research behind key technical choices.
