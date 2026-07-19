<!-- ROADMAP.md (markdown) -->

# Roadmap

This file is a point-form index of headlines only — no details, no rationale. Full descriptions live in `ROADMAP_DETAILS.md`, under the same `## Goal:` headings in the same order.

**The two files are 1:1.** Every headline here must have exactly one matching entry there, and vice versa. When you add, remove, or reword an item, update both files together in the same change — removing an item from one without removing it from the other leaves them out of sync.

Everything here can be considered aspirational and will likely never see the light of day. Item ideas may not be fleshed out and change drastically or be considered an actual bad idea the morning after we wrote it down in the middle of the night.

This file tracks unimplemented work only — once an item is done, remove it from both files rather than checking it off.

## Goal: Maintenance

- [ ] Switch CI aqtinstall from pinned hash to a stable release once Qt 6.11 is supported

## Goal: poe-info-service

- [ ] Shared, addon-agnostic install location + bootstrap-if-newer
- [ ] Replace spawn-tied process lifecycle with a keep-alive model
- [ ] Versioned WebSocket API
- [ ] CI schema-compatibility gate
- [ ] Self-update mechanism
- [ ] Manual installer
- [ ] Binary signing + checksum verification
- [ ] Credential storage package — macOS/Linux backends
- [ ] Credential expiry/staleness policy
- [ ] Register poe-info-service's own PoE OAuth client_id with GGG
- [ ] PoE OAuth data endpoints (characters, stash)
- [ ] Reusable rate-limited priority request queue (PoE OAuth API now, PoE Legacy API later)
- [ ] poe-info-service tunable-constants file (cache TTLs, queue/rate-limit settings)
- [ ] Multi-account PoE OAuth support
- [ ] Account identity: key `accounts` by both `name` and `poe_uuid`; reconcile renames on OAuth login
- [ ] Account selector (name or uuid) on account-attributable WebSocket requests
- [ ] "Scan filesystem for install directories" button on the Game settings page
- [ ] Steam presence: richer retry/backoff for outbound Steam requests
- [ ] Steam presence: resolve Steam vanity URLs to steamid64 server-side
- [ ] Steam presence: Steam OpenID webview login to auto-fill the user's own steamid64
- [ ] Steam presence: distinguish a private Steam profile from "not currently playing anything"
- [ ] Steam presence: support tracking friends (additional steamids) for group play
- [ ] Steam presence: investigate rich presence as a primary league/level/zone source for detecting game state on a different computer (e.g. a Steam Deck) than the one running poe-info-service

## Goal: Basic Features

- [ ] Log screen UI: flesh out the session list
- [ ] Guide screen
- [ ] Stash screen
- [ ] Profile screen
- [ ] Universal Search

## Goal: Public release

- [ ] Public release (first public build shipped to users)


## Goal: Event Detection

- [ ] Multi-client detection
- [ ] Investigate `replace_object` log lines as a source of in-map events


## Goal: Companion

- [ ] Log screen session detail scroll-to-bottom
- [ ] Log screen session detail flashing and slow updates
- [ ] Historical events panel: virtual scrolling
- [ ] Pagination prev/next scroll feel
- [ ] Auto start on boot
- [ ] Companion mode: web API only

## Goal: Overlay

- [ ] Game overlay interactive content beyond proof-of-concept text
- [ ] Overlay settings: distinct icons for rows sharing a placeholder

## Goal: Chat
- [ ] Chats tab — channel-number filtering
- [ ] Copy support for chat/DM excerpts
- [ ] Local chat capture
- [ ] DM/whisper push notification while tabbed out
- [ ] Tab-out chat client
- [ ] Optional unified chat view across concurrently-logged-in accounts

## Goal: Reminders

- [ ] Kirac mission refresh reminder (SSF)


## Goal: Companion as overlay widget

- [ ] Game-overlay corner widget


## Goal: Mobile

- [ ] Mobile companion app (iOS/Android)


## Goal: Native cross-platform (Mac, Linux)

- [ ] macOS overlay
