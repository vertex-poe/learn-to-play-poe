# ADR-003: Backward-Compatible Client API, Not Backward-Compatible Database Schema

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

Addon clients update for a short window after release, then commonly lock in unchanged for years. MercuryTrade — a real, widely-used PoE addon, nine years old, last updated seven years ago, still relied on for unique features today — is the reference case: no future coordination, migration, or awareness of this project's schema evolution can ever be assumed of a client like it. Any design that requires an old, abandoned addon binary to actively cooperate with a compatibility mechanism (participate in a sync protocol, run a downgrade migration, recognize a new API version) is unworkable, because that cooperation cannot be retrofitted into code nobody will ever touch again.

Because [ADR-001](001-single-shared-instance-lifecycle.md) makes the database exclusively owned by whichever service version is currently running, no old addon binary ever opens the database directly — old clients only ever reach poe-info-service over the WebSocket API. That confines the entire compatibility problem to one boundary instead of two.

Two mechanisms were evaluated and rejected before settling on API versioning alone:

- **Cross-schema replication** (e.g. Turso/libSQL-style syncing a primary database down to per-version replicas): rejected because embedded-replica technology of this kind performs page-level replication assuming an identical schema between primary and replica — it does not translate between schema versions. Achieving that would require hand-written, per-version translation logic maintained indefinitely, which is strictly more work than the alternative below for no additional benefit, plus a cloud/self-hosted sync dependency this system does not otherwise need.
- **Versioned SQL views with `INSTEAD OF` triggers** over a freely-evolving physical schema: a real, workable mechanism, but only worth its complexity once physical schema evolution genuinely cannot stay additive (e.g. a table split or column repurpose). It is recorded here as a reserve option, not built pre-emptively.

## Decision

- The WebSocket API is versioned (e.g. `/v1`, `/latest`) and negotiated at connection handshake. A client built against an older API version continues to be served in the shape it expects, computed from whatever the service's current physical data model actually is.
- Once an API version ships, its response shapes are permanent: fields are never removed, renamed, or repurposed in meaning. New capability arrives as new fields or a new version, never as a change to an existing version's contract. Repurposing a field's meaning while keeping its name and type is treated as a breaking change requiring a new version — it fails silently otherwise, and nothing structural catches it.
- The physical database schema evolves freely under the currently-running service version's control. Additive-only migration discipline (new tables/columns only; new columns nullable or defaulted) is still followed and enforced by a CI check — migrate a scratch database to head, then verify every still-supported API version's data-shaping logic still succeeds against it — but this is defense-in-depth, not an external contract, since no code outside the currently-running service ever depends on the schema's shape directly.
- Support for a given API version is retired only by deliberate, explicit deprecation policy, never by accident or by a migration that happens to break it.
- If physical schema evolution ever genuinely cannot stay additive, the versioned-views-with-triggers mechanism described above is the reserve option, built at that point rather than speculatively now.

## Consequences

- **Positive**: an addon frozen at whatever API version existed when it was last built keeps working indefinitely, without requiring any coordination, cooperation, or awareness from that addon's code, however old or abandoned.
- **Positive**: avoids building database-level compatibility machinery pre-emptively for a project that has had zero breaking schema reshapes to date.
- **Negative**: every shipped API version becomes permanent surface area that must keep being served correctly, indefinitely, by whatever the current implementation is — an ongoing engineering cost accepted deliberately, not a one-time decision.
- **Negative**: this protects against structural breaks but not semantic drift — a field that keeps its name and shape while quietly changing meaning is not caught by any mechanical check. This remains a review-discipline concern, named explicitly so it is not mistaken for something the CI gate already covers.
