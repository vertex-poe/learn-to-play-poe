# ADR-006: PoE Information Service — Shared Cache and Priority Queue

**Status**: Decided  
**Date**: 2026-06-30  
**Deciders**: MovingCairn

---

## Context

This application monitors several data sources on behalf of the user: the game's `Client.log`, rate-limited web APIs (character data, stash contents, economy prices), and potentially others. As the PoE addon ecosystem grows, other tools on the same machine face the same problem — each independently tails the same log file, maintains its own API cache, and manages its own request queue. This creates redundant work, redundant rate-limit consumption, and no coordination between addons.

The immediate need is internal: the app's own subsystems (overlay, session tracker, trade monitor, etc.) need a clean way to share fetched data and queue requests without coupling directly to each other. The longer-term opportunity is to extract this into a standalone service that any PoE addon on the machine can use.

## Decision

Introduce a **PoE Information Service** — a lightweight background process that centralises:

1. **A shared cache** for all expensive data sources (web API responses, parsed log state)
2. **A priority request queue** that all addon clients share, preventing redundant fetches and coordinating rate limits across consumers

The service is elected via a singleton protocol: each addon ships a versioned copy of the service binary, and at startup they negotiate — highest version wins, earliest start time breaks ties — so exactly one process runs the server and the rest connect as clients.

During alpha, the service lives as a `poe-info-service/` subdirectory of this repo (tightly coupled, co-evolved). It will be extracted into its own repository when its API has stabilised and a second addon consumer exists.

### Shared cache

Every addon independently hitting the same PoE web endpoints wastes API quota and risks rate-limit bans. The service maintains a single SQLite cache (WAL mode) with TTL-based expiry. When any client requests a resource:

- **Cache hit within TTL**: returned immediately from the DB, no network request.
- **Cache miss or expired**: the service fetches, caches, and returns the result.

All addons benefit from cache entries populated by any other addon. A trade tool warming the economy price cache means this app gets those prices for free, and vice versa.

### Priority request queue

Web API sources are rate-limited and slow. Multiple addons naively issuing concurrent requests for similar data would exhaust the rate limit and produce redundant work. The service serialises outbound requests through a single priority queue:

- Clients attach a priority when submitting a request.
- The queue deduplicates requests for the same resource (only one in-flight fetch per resource at a time; subsequent requests for the same key wait for the result).
- The elected server's rate limiter state persists across restarts (stored in SQLite), so a version handoff does not reset the rate limit budget and cause a burst.

`Client.log` is not rate-limited and is handled separately: a single tailer goroutine follows the file and fans events out to all subscribed clients via per-subscriber buffered channels.

## Transport

WebSocket on `127.0.0.1` (fixed port). One protocol for both request/response (correlation IDs) and push subscriptions (topic-based). AHK is the binding constraint on transport choice — it rules out gRPC and makes named pipes painful. Every target language (Qt/C++, Python, AHK, TypeScript/Node) has a usable WebSocket client.

The port bind is the singleton lock. No separate lockfile or named mutex.

## Consequences

- **Positive**: Web API quota is shared across all addons, not divided. A single rate limiter means no addon can inadvertently burn quota that another needs.
- **Positive**: `Client.log` is tailed once regardless of how many addons are running.
- **Positive**: Cache warm-up by any one addon benefits all others immediately.
- **Positive**: The priority queue gives this app (or any addon) a way to express urgency — user-triggered lookups can jump ahead of background prefetches.
- **Negative**: Introduces a service lifecycle dependency. If the elected server crashes and a client is mid-request, clients must handle reconnection and retry. This complexity is bounded by the service's reconnect/backoff protocol.
- **Negative**: Adds a Go toolchain as a build dependency (separate from the Qt/CMake build). Acceptable given Go's static binary output — the service binary is an artifact, not a linked library.
