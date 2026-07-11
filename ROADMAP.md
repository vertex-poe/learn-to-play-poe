<!-- ROADMAP.md (markdown) -->

# Roadmap

Everything here can be considered aspirational and will likely never see the light of day. Item ideas may not be fleshed out and change drastically or be considered an actual bad idea the morning after we wrote it down in the middle of the night.

This file tracks unimplemented work only — once an item is done, remove it rather than checking it off.

## Goal: Maintenance

- [ ] Switch CI aqtinstall from pinned git hash to a stable release once Qt 6.11 is properly supported (currently using `bbfb1f7c` of miurahr/aqtinstall as a workaround; check after 2026-08-01; see `.github/workflows/ci-windows.yml`)

## Goal: poe-info-service

Work items derived from `poe-info-service/docs/decisions/` (ADR-001 through 005) and root `docs/decisions/006-poe-info-service.md`, comparing the decisions against the current implementation.

- [ ] Shared, addon-agnostic install location + bootstrap-if-newer: `ServiceManager::start()` (`src/services/ServiceManager.cpp`) currently launches `poe-info-service(.exe)` straight out of this app's own install directory; per ADR-001/ADR-002 the app must instead bootstrap its bundled copy into a shared location only if newer, then always launch the shared copy from there
- [ ] Replace spawn-tied process lifecycle: `ServiceManager` currently binds the service's life to this app via a Windows Job Object (`KILL_ON_JOB_CLOSE`) and `PR_SET_PDEATHSIG` on Linux; ADR-001 supersedes this with a keep-alive-based model — any client can start or restart the service, and it must outlive whichever addon happened to launch it
- [ ] Versioned WebSocket API: negotiate a client-facing API version (e.g. `/v1`, `/latest`) at connection handshake, separate from the existing peer singleton-election version check in `proto.go`; once shipped, a version's response shapes are permanent — fields are never removed, renamed, or repurposed (ADR-003)
- [ ] CI schema-compatibility gate: migrate a scratch DB to head and verify every still-supported API version's data-shaping logic still succeeds, as defense-in-depth for the additive-only migration discipline already assumed by the schema (ADR-003)
- [ ] Self-update mechanism: the running service periodically checks a durable release feed (e.g. GitHub Releases), verifies signature/checksum, and updates itself in place — not yet implemented anywhere in `poe-info-service` (ADR-002)
- [ ] Manual installer: standalone poe-info-service installer for troubleshooting/recovery, installs into the shared location only if what's there isn't already newer (ADR-002)
- [ ] Binary signing + checksum verification: required before any downloaded or self-installed binary is written to the shared location or executed, regardless of which of the three distribution paths delivered it (ADR-002)
- [ ] Credential storage package — macOS/Linux backends: `internal/creds` (`Store`/`Get`/`Delete`, build-tag-selected per platform) now has a Windows backend (danieljoos/wincred); still needs keybase/go-keychain (macOS) and godbus/dbus Secret Service (Linux), plus an in-memory backend for automated tests (ADR-005)
- [ ] OAuth PKCE flow: service-initiated OAuth token acquisition via the system's default browser plus a local loopback redirect listener, for providers whose flow allows it, so future data sources (PoE official API, Steam) don't require WebView capability (ADR-004)
- [ ] Credential expiry/staleness policy: explicitly left open by ADR-004/ADR-005 and not yet the subject of a dedicated ADR — needs its own design pass once the storage mechanism above lands
- [ ] Ingest status "waiting" vs. missing log file: the `status` WS method's `phase` field reports `"ingesting"` (with no percent) if `Client.txt` doesn't exist yet at startup, since `Tailer.poll` returns early on `os.Open` failure and `caughtUp` never flips — should report `"waiting"` in that case instead
- [ ] "Scan filesystem for install directories" button on the Game settings page: invokes an on-demand filesystem scan RPC on poe-info-service (Steam library folders, Program Files, GOG, etc.) to find PoE installs, independent of whether the game process is currently running — distinct from the process-based auto-detect (`internal/detect`) that watches for a running game process

## Goal: Basic Features

- [ ] Log screen UI: flesh out the session list — richer session cards (zone count, notable events, loot highlights), expandable inline detail, filtering by character/date/duration, and visual distinction between ongoing and completed sessions
- [ ] Guide screen: context-sensitive help panel designed for a side monitor; content auto-changes based on detected in-game activity (zone, boss, league mechanic); surfaces relevant tips, mechanics explanations, and checklists without the player having to search
- [ ] Stash screen: search and browse stash items across all tabs; identify gear upgrades already in the stash; surface crafting opportunities on existing items to close the gap to an upgrade; character gear review against current build
- [ ] Profile screen: account overview and splash screen; tracks goals (player-defined targets), accomplishments (unofficial achievements derived from session history), and a summary of playtime, characters, and milestones
- [ ] Universal Search: a single search bar that queries across all screens (sessions, stash, chat, DMs, goals, accomplishments) and surfaces ranked results; accessible via the search icon in the top navbar

## Goal: Public release

- [ ] Public release (first public build shipped to users)


## Goal: Event Detection

- [ ] Multi-client detection: investigate whether multiple game instances can run from the same install directory or require separate installs. If separate installs, each PID maps 1:1 to a Client.txt log file, enabling per-instance log tailing and accurate session-to-PID matching for the "Game is running" card timestamp enrichment.
- [ ] Investigate `replace_object` log lines as a source of in-map events


## Goal: Companion

- [ ] Log screen session detail scroll-to-bottom: the detail view doesn't reliably stay pinned to the bottom when new events are appended — investigate whether the scroll anchor logic elsewhere in SessionViewPage needs to be applied here
- [ ] Test coverage for `SessionViewPage`/`LogPage`'s request-retry-on-error paths (e.g. `SessionViewPage::rebuildDbZones`'s 500ms `QTimer::singleShot` retry, mirroring `LogPage::rebuild`'s): `PoeInfoClient` isn't mockable (concrete `QObject`, non-virtual `request()`, real `QWebSocket`) and no fake/in-process WS server test double exists yet — building one (or making `PoeInfoClient` mockable) is the prerequisite before these paths can get real unit coverage instead of relying on manual repro
- [ ] Log screen session detail flashing and slow updates: investigate UI flicker and sluggish refresh when viewing a session's detail on the Log screen — profile rebuild triggers, widget deletion timing, and whether the same deferred-clear fix applied elsewhere in SessionViewPage is needed here
- [ ] Historical events panel: virtual scrolling via QListView + QAbstractItemModel + QStyledItemDelegate (replaces load-N-at-a-time approach; delegate ports existing custom-paint logic from NotificationWidget; enables millions of rows with no memory growth)
- [ ] Pagination prev/next scroll feel: when prev/next 50 loads, the viewport should appear to stay put as content loads around you rather than snapping the first old record to the top of the screen; the button bar disappearing on load causes a visible scroll jump that should be absorbed so the experience feels like the page simply grew
- [ ] Auto start on boot (Windows registry `HKCU\…\Run`; Linux `.desktop` autostart)
- [ ] Companion mode: web API only, no overlay, no PoE install required

## Goal: Overlay

- [ ] Game overlay interactive content beyond proof-of-concept text
- [ ] Overlay settings: find distinct icons for rows that currently share a placeholder — Character Age reuses the same `stopwatch-fill.svg` as Time Played; source a dedicated SVG (e.g. a calendar or hourglass) so each row is visually distinct in the overlay icon grid

## Goal: Chat
- [ ] Chats tab — channel-number filtering: the Filter panel UI is built but "show only global #3" / "show only trade #2" can't be wired up until `chats` has a `channel_number INTEGER` column (schema migration to v4) and poe-info-service's ingest writer (`poe-info-service/internal/ingest/writer.go`) tracks the current channel join per install so new rows get the right number on ingest
- [ ] Copy support for chat/DM excerpts: select one or more message rows in the chat or DM view and copy them as plain text so conversations can be shared on forums or Discord without combing the raw log
- [ ] Local chat capture: parse and store local (area) chat lines from `Client.txt` so the Local checkbox in the chat filter panel becomes functional; requires identifying the log line format and adding a `local` channel variant to the ingest worker
- [ ] DM/whisper push notification while tabbed out: fire a system-tray or OS notification when an incoming whisper arrives and the game does not hold focus; hooks into the live event bus whisper emission so no separate polling is needed
- [ ] Tab-out chat client: compose and send a single message to the game's chat box via keystroke injection while the player is out-of-game; one typed message → one keystroke sequence delivered to the client's input box → one in-game send; limited to one message at a time per ADR-004 (one outside action maps to one inside action); depends on the game window being open and the player being logged in

## Goal: Reminders

- [ ] Kirac mission refresh reminder (SSF): notify the player when Kirac's daily missions have refreshed (midnight local time) and flag when a unique map is available in the mission pool; in SSF Kirac is the only reliable source of unique maps so knowing exactly when to check is high-value; needs a configurable alert in the live-alert rule engine or a dedicated daily-reset timer


## Goal: Companion as overlay widget

- [ ] Game-overlay corner widget: render a compact DM/alert panel inside the overlay window so the player can tuck it into a corner of the game screen; requires the panel to look good at small sizes first (already mobile-friendly after DM drill-down redesign)


## Goal: Mobile

- [ ] Mobile companion app (iOS/Android): UI design can be ported from the current mobile-style layouts; real-time features would use a native-app-to-server API where the desktop companion app exposes a local server, with Client.txt events relayed to the mobile device over LAN or via a relay


## Goal: Native cross-platform (Mac, Linux)

- [ ] macOS overlay (`NSWindow` level + `ignoresMouseEvents`; needs PoE Mac client testing)
