# ADR-006: Depend on the Shared PoE Information Service

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

This application monitors several data sources on behalf of the user: the game's `Client.log`, and rate-limited web APIs (character data, stash contents, economy prices, legacy trade endpoints). Two problems follow from building that entirely inside this app:

**Client.txt parsing complexity has no natural home.** The log format is unstructured, growing in the variety of events it needs to recognize, and consumed by several unrelated features (overlay, session tracker, trade monitor, and more to come). Left inside the app, parsing logic either gets duplicated per-consumer or becomes a shared internal module that every consumer couples to directly — in both cases the parsing logic and the features that use it stay entangled, and the app owns log-format churn as its own maintenance burden indefinitely.

**API rate limits are a shared, not private, resource.** GGG's web APIs are aggressively rate-limited. If this app maintains its own request budget, that budget is not actually private in practice — any other PoE addon running on the same machine is independently hitting the same endpoints for overlapping data (character info, prices), and GGG's limits are enforced per-account or per-IP, not per-addon. Every addon acting alone means the same data gets fetched redundantly, and the effective budget available to any one of them shrinks in proportion to how many others are also running. This app duplicating requests that another addon already made is pure waste, and vice versa.

Building a general-purpose, cross-addon service to solve this is out of scope for this app's own codebase to own outright — it's a shared-infrastructure problem, not an app feature. That service is `poe-info-service`, developed alongside this repo; its own design decisions (singleton election, database ownership, distribution and self-update, API versioning, credential custody) are recorded separately in [poe-info-service/docs/decisions/](../../poe-info-service/docs/decisions/), since they concern the service's own architecture rather than this app's.

## Decision

This app depends on `poe-info-service` rather than implementing `Client.log` parsing or PoE web API access as app-internal logic:

- **Client.txt parsing is centralized behind an event system.** `poe-info-service` owns log tailing and parsing, and publishes discrete, well-defined events over its WebSocket API. This app's features (overlay, session tracker, trade monitor) subscribe to the events they care about; none of them parse raw log lines or own log-format knowledge directly.
- **API rate limits are shared, not duplicated.** This app's web-API-dependent features go through `poe-info-service`'s shared cache and request queue instead of issuing requests directly. A cache warmed by another addon on the same machine benefits this app for free, and this app warming the cache benefits every other addon using the same service — the rate-limit budget is coordinated across whatever addons are actually running, instead of silently divided by however many happen to be installed.

As a consumer of a service this app does not solely own, this app is responsible for:

- Bundling a copy of `poe-info-service` and participating in bootstrapping it into the shared install location, per the service's own distribution model.
- Participating in the shared lifecycle: starting the service if nothing is listening, restarting it if it becomes unresponsive, and sending keep-alives while depending on it.
- Owning the one capability the service cannot provide itself — capturing `POESESSID` via this app's WebView-based login flow — and handing it to the service. This app never receives session credentials back from the service, and does not need to store them itself for the service's purposes.
- Building against a specific versioned WebSocket API and accepting that, like any other client, this app is expected to keep working against that version indefinitely if it is never updated to a newer one — a version bump is a deliberate compatibility decision, not something to do casually.

## Consequences

- **Positive**: `Client.log` parsing logic lives and evolves in one place, decoupled from any specific feature. This app's features consume events, not raw log lines, and gain new event types without needing their own parsing changes.
- **Positive**: this app's web-API-dependent features benefit from cache warm-up done by any other addon on the same machine, and rate-limit exhaustion becomes a coordinated problem instead of each addon silently competing for the same limited budget.
- **Negative**: introduces a runtime dependency whose lifecycle, update cadence, and compatibility guarantees this app does not fully control on its own — those are governed by `poe-info-service`'s own decisions, not this app's.
- **Negative**: any feature that needs data through `poe-info-service` is bounded by whatever its WebSocket API currently exposes. A feature needing something the service doesn't yet provide requires a change to `poe-info-service`, not just to this app.
