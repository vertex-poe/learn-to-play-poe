<!-- docs/decisions/004-game-addon-interaction-principles.md (markdown) -->

# ADR-004: Game–Addon Interaction Principles

**Status**: Decided  
**Date**: 2026-06-21  
**Deciders**: MovingCairn

---

## Context

This addon interacts with Path of Exile 1, a live game operated by Grinding Gear Games (GGG). GGG has published informal but widely-understood guidelines about what third-party tools may and may not do. The most commonly cited formulation is:

> "One action outside the game should correspond to one action inside the game."

Often shortened to "1 click = 1 server action." The intent behind the guideline is twofold: prevent automation that bypasses player agency, and prevent tools that exert disproportionate load or unfair advantage. GGG staff have also expressed concern about players "automating the fun away."

The existing addon ecosystem has established well-accepted precedents — notably trade whisper macros — that technically chain several operations (copy item text, parse it, open a browser or whisper window, pre-fill a message) in response to one player action. These are tolerated because they remain in the spirit of the guideline: the player still initiates every meaningful decision and no outcome is produced without human intent.

**This document is itself an expression of good faith.** This project exists within an ecosystem that depends on GGG's continued goodwill. GGG has historically tolerated, and in some cases quietly endorsed, third-party tools that serve their players well. That tolerance is not a right, not codified, and not guaranteed — it persists because most of the community has acted as respectful guests rather than adversaries. This project intends to be firmly in that category. The principles below are not purely about TOS compliance; they are about acting in good faith within the systems and behaviours GGG has established with the community, and not being the tool that erodes the goodwill the entire ecosystem depends on.

This ADR establishes how this addon approaches that guideline and the broader question of what kinds of game interaction are in-scope.

---

## Decision

### Feature decision checklist

When building any feature that involves the game or GGG's infrastructure, ask these questions in order and stop at the first one that applies:

1. **Can this work with no game integration at all?** If yes, build it that way.
2. **Does the official GGG API cover it adequately — data, timeliness, and rate limits included?** If yes, use it.
3. **Can `Client.txt` provide it through passive observation?** If yes, use that.
4. **Is a legacy website endpoint the only viable path?** Only then, and only with the restraint described in Section 5.

### 1. Follow the spirit, not just the letter, of GGG's automation guideline

The "1 click = 1 server action" rule is a heuristic, not a specification. We treat it as an expression of intent: **a human player should remain in control of every meaningful game decision**. Following the letter while violating the intent (e.g., one button press that plays the game for you) is not acceptable. Violating the letter while respecting the intent (e.g., a trade macro that stages a whisper for the player to review and send) is acceptable.

We will not "automate the fun away." The addon exists to reduce friction in the parts of the game that are genuinely tedious — inventory management UI, trade negotiation boilerplate, information lookup — not to make decisions for the player or remove the gameplay itself.

We recognize that where exactly "friction reduction" ends and "automating the fun" begins is a judgment call that reasonable people disagree on. When the boundary is unclear we will bias toward less automation, more player decision, and explicit player confirmation before any consequential action.

### 2. Prefer features that operate entirely independently of the game

When a feature can be delivered without touching the game, its API, or its files, that is the first choice. Examples: a shortcut that displays a hint sheet, a local note editor, a currency value calculator that works from a static price snapshot. These features carry no compliance risk and impose no load on GGG's infrastructure.

### 3. Use the official GGG API when it genuinely serves the need

The official API is the sanctioned integration surface and the first place we look. We will use it when it provides what a feature requires — meaning the data is present, accurate, timely, and accessible within rate limits that allow the feature to function in a meaningful way.

"Covers the need" is not the same as "an endpoint nominally exists." If the information a feature depends on has not yet been added to the official API but is available through `Client.txt` or the website, or if rate limits make a feature non-viable through the API alone, then the official API does not cover that need and we will use whatever legitimate alternative does. The spirit of preferring the sanctioned surface is best served by actually building features that work, not by binding ourselves to an integration that cannot deliver them.

### 4. Use Client.txt for real-time game events

GGG generates `Client.txt` as a byproduct of the game client and has historically shown awareness of, and comfort with, addons reading it. GGG is entirely in control of what information appears in that file. If GGG removes or redacts information from the log, we treat that as a signal that the information is not intended for addon consumption and we will stop using it.

Reading `Client.txt` is **passive observation** — we open it in read-only mode, tail new lines, and parse only what GGG exposes. We do not influence what is written to it.

This passivity is an important distinction: the addon may observe game state continuously and react to it (displaying information, showing alerts, updating the UI) without that constituting "independent action." What it may not do is use an observed event as an automatic trigger for an **outbound request to GGG infrastructure** — those still require a discrete player action. Observation is passive; requests are not.

### 5. Use the legacy website API sparingly and with restraint

Some functionality — certain trade endpoints, forum data, league ladder information — GGG has not yet added to their official developer API, making it currently only accessible via GGG's website. We will use these endpoints only when:

- No official API equivalent exists.
- The feature materially serves the player.
- We cannot reasonably build the feature from data we already have.

When using legacy website endpoints we will:

- Identify the addon in the `User-Agent` header (name and version) so GGG can attribute and reach our traffic.
- Respect rate limits aggressively, erring on the side of fewer requests.
- Honor server-provided rate-limit headers and `Retry-After` responses; when the server asks us to wait, we wait.
- Cache responses locally and avoid redundant requests.
- Never make requests in the background without the player initiating the flow.
- Surface errors gracefully rather than retrying blindly.

The health of GGG's website infrastructure is a shared resource. We will not abuse it.

### 6. Hard limits — what we will never do

The following are bright lines we will not cross, regardless of what functionality they might enable:

| Action | Reason |
|---|---|
| Read game process memory | GGG never chose to expose this information; extracting it unilaterally is exactly the kind of behaviour that damages the relationship — beyond any TOS concern |
| Modify game files | Alters game integrity in ways GGG has not sanctioned; risks accounts and community standing |
| Intercept network packets | The game protocol is private; GGG did not intend it for addon consumption and we will not treat it as a data source |
| Impersonate the game client to GGG servers | Fraudulent; the kind of act that ends ecosystems |

These are not judgment calls. They are unconditional.

### 7. The player is always the decision-maker

The addon presents information, shows alerts, stages options, and prepares actions — but the player executes consequential decisions. Automated multi-step sequences in response to one player action (e.g., a trade macro) are acceptable when the steps are all direct, deterministic consequences of the player's expressed intent with no branching that substitutes the addon's judgment for the player's.

Automated *presentation* — surfacing information, highlighting something, sounding an alert in response to a game event — does not require a player action to trigger, because it produces no outcome. Automated *outbound action* toward GGG or any other service does.

At the end of the day, a human being is playing a game and taking pleasure in that interaction. The addon's job is to be a good tool in service of that, not to replace it.

---

## Consequences

- Features that require reading game memory or modifying game files are permanently out of scope and will not be designed around.
- Features that require the legacy website API will require justification, and their implementation will include request budgeting and caching from the start — not added later.
- Features that could run without any game integration — pure companion features — are prioritized and should not be blocked on or entangled with game-connected features.
- When a new integration point with the game is proposed, the default question is "what's the least invasive way to achieve this?" not "what's the most capable?"
- If GGG formalizes or updates its addon policy, this ADR is revisited against those changes. Informally-tolerated behaviour that is explicitly prohibited in a formal policy is removed regardless of community precedent.
- If GGG adds official API coverage that genuinely and adequately serves something the addon currently obtains from `Client.txt` or a legacy endpoint, we prefer the sanctioned surface.
