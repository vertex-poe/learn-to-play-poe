<!-- ROADMAP.md (markdown) -->

# Roadmap

- [ ] Public release (first public build shipped to users)
- [ ] Investigate `replace_object` log lines as a source of in-map events
- [ ] Investigate `window` "Lost focus" / "Gained focus" lines as AFK or session-pause signals
- [ ] Auto start on boot (Windows registry `HKCU\…\Run`; Linux `.desktop` autostart)
- [ ] Companion mode: web API only, no overlay, no PoE install required
- [ ] macOS overlay (`NSWindow` level + `ignoresMouseEvents`; needs PoE Mac client testing)
- [ ] Game overlay interactive content beyond proof-of-concept text
- [ ] Historical events panel: virtual scrolling via QListView + QAbstractItemModel + QStyledItemDelegate (replaces load-N-at-a-time approach; delegate ports existing custom-paint logic from NotificationWidget; enables millions of rows with no memory growth)
- [x] Chat/whisper panel: three views — whispers-only, chats-only, and a combined view (UNION ALL of both tables with a source column to distinguish them); chats are not in the events spine so the combined view is its own dedicated query, not a filter on events
- [ ] Chats tab — channel-number filtering: the Filter panel UI is built but "show only global #3" / "show only trade #2" can't be wired up until `chats` has a `channel_number INTEGER` column (schema migration to v4) and `LogIngestWorker` tracks the current channel join per install so new rows get the right number on ingest
- [ ] Copy support for chat/DM excerpts: select one or more message rows in the chat or DM view and copy them as plain text so conversations can be shared on forums or Discord without combing the raw log
- [ ] Local chat capture: parse and store local (area) chat lines from `Client.txt` so the Local checkbox in the chat filter panel becomes functional; requires identifying the log line format and adding a `local` channel variant to the ingest worker
- [ ] DM/whisper push notification while tabbed out: fire a system-tray or OS notification when an incoming whisper arrives and the game does not hold focus; hooks into the live event bus whisper emission so no separate polling is needed
- [ ] Tab-out chat client: compose and send a single message to the game's chat box via keystroke injection while the player is out-of-game; one typed message → one keystroke sequence delivered to the client's input box → one in-game send; limited to one message at a time per ADR-004 (one outside action maps to one inside action); depends on the game window being open and the player being logged in
- [ ] Game-overlay corner widget: render a compact DM/alert panel inside the overlay window so the player can tuck it into a corner of the game screen; requires the panel to look good at small sizes first (already mobile-friendly after DM drill-down redesign)
- [ ] Mobile companion app (iOS/Android): UI design can be ported from the current mobile-style layouts; real-time features would use a native-app-to-server API where the desktop companion app exposes a local server, with Client.txt events relayed to the mobile device over LAN or via a relay
