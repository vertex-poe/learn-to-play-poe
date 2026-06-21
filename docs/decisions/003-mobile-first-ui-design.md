# ADR-003: Mobile-First UI Design Philosophy

**Status**: Decided  
**Date**: 2026-06-21  
**Deciders**: MovingCairn

---

## Context

This app is a companion to a running game session, not a standalone productivity tool. The physical context of use shapes every layout and sizing decision.

### Typical usage environments

**Side monitor** — the most common case. A second monitor beside the game monitor is usually smaller (1080p 24" vs 1440p 27", or even a repurposed 1080p portrait monitor). The companion app is one of several things on that monitor: a browser, a spreadsheet, a wiki. It may be resized down to make room. The user glances at it between game actions, not during.

**Alt-tab overlay on a single monitor** — the user switches to the app while the game remains running underneath. The window is semi-transparent or small enough that the game is visible through or around it. Visual density works against readability in this scenario.

**In-process overlay widget (future)** — if the app is ever embedded directly as a Qt overlay widget rendered on top of the game, it will share screen space with the game world. It must be compact and non-occluding by default.

**Mobile / tablet client (future)** — a phone or tablet running a companion client that queries this app's PC server over the network (for `Client.txt` access and live event data). The navigation model will need to work with thumbs on a touch screen, without a mouse.

### What users actually need at a glance

- Current status, active timers, recent events — not tables of raw data
- Large readable text — small fonts require stopping to focus, which disrupts play
- A clear primary action or piece of information per screen — not a dense dashboard

---

## Decision

Design for a **mobile look and feel**, not a desktop one. Concretely:

### Large text, low density

Text sizes should be comfortable to read in a peripheral glance. Prefer fewer, larger pieces of information per screen over many small ones. Dense tabular data is appropriate only on pages explicitly designed for reference lookups (not the primary views).

### Mobile-nav-first layout

Navigation is structured as a **tab bar or bottom nav** rather than a sidebar, menu bar, or multi-pane layout. Each tab owns the full window content area — no persistent side panels competing for horizontal space.

This maps naturally to both the compact side-monitor use case and to a future native mobile port: the same tab-based navigation model works on a phone screen without modification.

### Compact by design, not by compromise

The window should be usable and readable at small sizes — not just "it doesn't break at small sizes." Minimum viable window dimensions are a design constraint, not an afterthought.

### Overlay-ready proportions

Because the app may eventually be embedded as an overlay widget, each primary view should have a meaningful default that works within a ~400×600px tile. Views that are not overlay-appropriate can be marked as such and hidden in overlay mode.

### No multi-column desktop layouts

Do not use two-column or three-column layouts to pack more information in. Width is scarce (side monitor, partial viewport, overlay tile). Use vertical space instead.

---

## What this rules out

- Sidebar navigation (too wide, not touch-friendly)
- Dense data tables as primary views (too small to read at a glance)
- Menu bars or top toolbars with many entries (overkill for the feature surface)
- Layouts that require a large minimum window size to function

---

## Consequences

- The tab bar pattern (already in use) is the correct and permanent navigation model, not a temporary choice.
- Widget sizing — fonts, padding, button targets — should be validated at 150–200% of what feels "normal" for a desktop app.
- If a mobile / tablet port is built, it would act as a client of the PC server app (API calls for `Client.txt` data). The UI layer is the only thing that needs to port; the layout model already matches.
- If the overlay widget path is pursued, the mobile-first sizing assumptions mean most views are already close to overlay-appropriate dimensions.
- When adding new features, the question is "what is the one thing the user needs on this screen" rather than "how do we display all the data for this feature."
