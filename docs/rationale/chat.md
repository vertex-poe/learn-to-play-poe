<!-- docs/rationale/chat.md (markdown) -->

<`!-- docs/rationale/chat.md (markdown) -->

`# Chat & Direct Messages — Feature Rationale

## Why this feature exists

Path of Exile is, at its core, a solo game. It is not balanced around parties, it does not require cooperation to progress, and its most celebrated achievements — the deep build space, the economy, the seasonal league race — are things a player can pursue entirely alone. And yet it is unmistakably a social game. Much of the economy still runs on direct communication — even with the Merchant Tab offloading some trade into passive listings, negotiation, price checks, and the long tail of unlisted items all happen in whispers. Guild and community life lives in global chat. Rare drops get announced, milestones get shared, and the hard-won knowledge that keeps the game legible travels person to person through its channels. The social layer is not decoration on top of the game — it is part of how the game is actually played, even though the game never forces a player to take part.

That is exactly why the state of in-game chat matters. The interface has not meaningfully changed in the better part of a decade, and the friction it creates works directly against the social fabric the game depends on. Every conversation that gets dropped, every whisper that goes unseen, is a small severing of the connection between a player and the world they are playing in.

### The specific problem

Several routine in-game actions clear the chat window entirely: changing characters, leaving a party, restarting the client. The result is ordinary and disorienting — the player returns to an empty window with no indication of what they missed. Messages from friends, trade responses, guild announcements: gone. The single scrolling window makes it worse. Every channel mixes together, and a busy Trade channel at the wrong moment can bury the one whisper a player was actually waiting for.

GGG is aware of this. They simply have not prioritized it yet.

There is, however, a partial solution players have quietly leaned on for years: the `Client.txt` log file. The client writes it as a byproduct of normal play, and despite legal restrictions in some territories (South Korea among them) that prevent logging certain chat content, GGG continues to record what it lawfully can. Players have noticed. "Let me check the logs" is unremarkable phrasing in the community. The information is there. Reaching it is the hard part.

The file is dense with everything other than chat — connection events, area transitions, item filter triggers, engine diagnostics — with no filtering, no structure, and no copy affordances built for reading a conversation. It was never meant to be a chat client.

## What we build, and why

### Primary: historical log viewer

The first and most important thing we build is a structured, filterable view of the chat history that `Client.txt` already contains.

This is not a new capability. The capability is already in the player's hands and they are already using it. We make it even easier to interact with.

The viewer organizes messages by channel (Global, Trade, Party, Guild, DMs), filters by date, and makes any message or thread easy to select and copy. A player who was mid-conversation when the client restarted can come back and read what they missed. A player who wants to bring a conversation to the official forums for community discussion can lift it cleanly instead of combing a raw log.

We acknowledge the limitation plainly: not every message from every player is present. Local law means the log is an incomplete record in some configurations. We believe the subset that is present is still far more useful than the current default of losing the entire chat window on every restart — and that players are already going to the log file to recover it. We are improving something they already do.

The viewer is the feature. Everything else follows from it.

**Subfeatures:**
- Unified timeline combining all chat channels and DMs, with channel-colored accent bars and badge pills (Global, Trade, Party, Guild, and inbound/outbound DMs) so each line's source is readable at a glance
- Per-channel checkbox filters (Global, Trade, Party, Guild, DM) mirroring the in-game chat filter the player already knows, so narrowing the timeline feels immediately familiar
- Date-bucket navigation (Today, Yesterday, This Week, then individual dates) for jumping to a time period instead of scrolling to it
- Pagination via "Load previous 50" to keep the initial load fast without throwing history away
- Copy support for sharing conversation excerpts outside the game

### Secondary: in-game chat overlay

Once a modern chat interface exists, an obvious question follows: can it cover the deficiencies of the in-game one while the game is still open?

The overlay window (see [ADR-001](../decisions/001-technology-stack.md)) can place our chat widget directly over the in-game chat panel, giving the player history, filtering, and copy support without leaving the game. The cost is some visual opacity over what sits beneath it. For players who want the functionality, that is a fair trade — and it keeps the player looking at the same conversation the game is showing, rather than pulling them out of it.

### Tertiary: tab-out chat client

The other shortcoming of in-game chat is quieter: it only works while the game holds focus. A player who tabs out to check a trade site, a build guide, or Discord is cut off from in-game conversation for the duration — and in PoE, tabbing out is constant.

So we offer a narrow bridge. With the game running and the player logged in, a message typed into our interface is forwarded as a single keystroke sequence to the client, placed in the chat box and sent. One message in, one message out. This is exactly [ADR-004](../decisions/004-game-addon-interaction-principles.md)'s principle that "one action outside the game should correspond to one action inside the game." We are not automating anything. We are giving the player a second input surface for a single action they would otherwise have to switch windows to perform.

We do not take this lightly, because the obvious worry is real: a tool that keeps a player in chat while out of the game could, in theory, reduce the reason to be in the game at all. But it is worth being honest about who this feature is even for. The savvy already have it — Discord, Slack, the Steam overlay, phone chat clients, Telegram bots wired up for trade notifications. AI coding tools have only widened that gap, letting more players build their own bots. So the real question is not whether out-of-game chat is good or bad in the abstract; it already exists for a subset of players. The question is whether we extend it to the whole playerbase, and in which direction it pushes.

Direction is everything here. This is not about taking a player who was AFK or fully disconnected and giving them a reason to stay out. It is about in-game players more easily reaching out-of-game ones — and handing those players a reason to come back. Someone who learns they have a trade offer waiting, or that a friend just hit a great drop, is far more likely to tab back in and play than someone who never knew the conversation was happening at all. The game is already running — we simply make it easier to stay in the loop without switching windows every time.

But the notification only gets you partway. A player might return for a trade — if they believe it is real, if they believe the buyer will still be there when the client finally loads. The thing that sets the hook is the ability to answer while still out: "1 second, alt-tabbing back," or "that's cool, where'd you get that?" That small, real-time back-and-forth is what turns "I might come back later" into "I'm logging in right now." We believe it is the interaction, not the notification alone, that brings players back with any consistency — and keeps being in the game the easy, natural next step rather than a chore to be scheduled.

And the win is not a private one. More players returning to the game is a win for players, for the community, and for GGG and Path of Exile as a whole.

We hold this judgment lightly, and will revisit it if GGG delivers meaningful chat improvements or if community feedback points the other way.

## Direct Messages

The DMs page is a companion to the chat viewer, scoped to one-on-one whisper conversations. The rationale is the same: whispers are already logged, the log is already consulted, and doing so by hand is miserable.

One development is worth calling out. The premium Merchant Tab has moved a large share of trade whisper traffic off of general chat and into structured trade flows. The effect is that a player's whisper history is measurably less noisy than it used to be — more often genuine conversation than trade boilerplate. That makes whisper history more worth reading, and a dedicated viewer built for it correspondingly more valuable. The DMs page leans into that, with a layout oriented toward conversation threading rather than the multi-channel feed of the main chat page.

## What we are not building

We are not replacing in-game chat. Players talking to each other through GGG's systems, with other players who are actually in the game, is the entire point — that conversation is the social health of the game, and we want more of it, not a substitute for it. Discord and other chat clients are there if you want them. Our goal is to make in-game communication more valuable, more persistent, more legible, and harder to lose, so that more of it survives and more of it leads someone back into the game world.

If GGG delivers meaningful chat improvements, we defer to them. Our viewer complements the official client. It does not compete with it.
