# ADR-001: Single Shared Instance, Ephemeral Lifecycle

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

`poe-info-service` exists so addons on the same machine share a single cache and API rate-limit budget instead of each duplicating requests against GGG's rate-limited endpoints — see the client-facing rationale in root [ADR-006](../../../docs/decisions/006-poe-info-service.md) for why this app depends on the service at all. Achieving that sharing requires exactly one process to be doing the fetching and caching at any given time; each addon ships a versioned copy of the binary, and at startup they negotiate so exactly one process runs the server. Two things are unresolved by that election alone, and matter once multiple independently-authored addons, on independent release schedules, are relying on the same running instance over years: which physical binary is actually executed, and what keeps the process alive or lets it die.

If each addon executed its own bundled copy directly, a session with several addons open would involve multiple independent processes, any of which might consider itself the writer — requiring live leader election and handoff as addons open and close mid-session, not just a one-time startup race. That is a materially harder problem than this system needs, and it reintroduces exactly the duplicate-work/duplicate-rate-limit-consumption problem this service exists to prevent.

Separately, tying the service's process lifetime to whichever addon happened to spawn it (Job Object on Windows, `PR_SET_PDEATHSIG` on Linux) means the service dies when *that specific* addon exits, even if other addons are still connected and depending on it.

## Decision

- Exactly one poe-info-service process runs at a time, system-wide, per user. It is the copy resident in a shared, addon-agnostic install location — never a copy executed directly out of an individual addon's install directory.
- That single running instance is the sole reader and writer of the shared SQLite database (see [ADR-003](003-client-api-versioning.md) for how this keeps old clients compatible without database-level compatibility machinery). No addon, and no other copy of the service binary, opens the database directly.
- Lifecycle responsibility is distributed across clients, not owned by any one addon or the OS:
  - Any addon starts the shared instance if nothing is currently listening on its port.
  - Any connected addon restarts it if it stops responding.
  - The service shuts itself down after an interval with no active keep-alive from any connected client. Because the service already tails `Client.log`, the PoE game process being open counts as an implicit keep-alive even with zero addon clients connected — this keeps session-history ingestion current for as long as the user is playing, without requiring any addon UI to be open.
- The service is never registered as a Windows Service, a launchd/systemd daemon, or a start-with-boot entry. It runs only while something is actively using it or the game itself is running.

## Consequences

- **Positive**: single-writer simplicity for the database. There is never more than one process to coordinate with, so no live leader-election or handoff protocol is needed — only the one-time, startup-time election.
- **Positive**: closes the gap in the original spawn-tied supervision model (Job Object / `PR_SET_PDEATHSIG`). Tracking keep-alives from every connected client, not just the process that happened to spawn the service, means the service survives its original launcher exiting as long as other clients (or the game) still need it.
- **Positive**: bounds the "orphaned background process" risk. If every addon that ever used it is uninstalled, the worst case is inert files sitting on disk — not an unattended process that keeps running indefinitely, unmonitored, forever.
- **Negative**: self-update (see [ADR-002](002-distribution-and-self-update.md)) can only run opportunistically, while the service happens to be alive. Freshness is bounded by how often and how variously the service gets launched on a given machine, not by wall-clock time — a machine where the same single addon always launches it, with no other addon ever installed, can in principle run a stale binary indefinitely even during active use. This is accepted as a smaller, rarer version of the staleness problem this design otherwise solves.
