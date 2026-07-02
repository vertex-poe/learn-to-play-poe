# ADR-002: Distribution and Self-Update Model

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

[ADR-001](001-single-shared-instance-lifecycle.md) requires a shared, addon-agnostic install location that a single running instance is resident in. That location needs to be populated and kept current without imposing a separate install step — addon authors and users both expect to download one small thing and run it, not install a second, unrelated program to get an addon working.

The service's usefulness depends on its data-gathering logic (talking to PoE's web APIs, parsing `Client.log`) staying current with upstream changes. Because addon clients are largely immutable in practice — updated for a short window after release, then commonly locked in unchanged for years (see [ADR-003](003-client-api-versioning.md) for the MercuryTrade case this is modeled on) — freshness cannot depend on any specific addon being updated again.

## Decision

Three complementary paths deliver or refresh the shared instance, all converging on the same shared install location:

1. **Manual installer.** A standalone poe-info-service installer a user can run directly, which installs into the shared location only if what's there isn't already newer. This is primarily a troubleshooting/recovery path, not the expected common case.
2. **Self-update.** The running shared instance periodically checks a durable, third-party-hosted release feed (e.g. GitHub Releases) and updates itself in place. This is the primary freshness mechanism, and it can only act while the service happens to be running (see ADR-001's consequences). The game and every supporting web API other than `Client.log` are always-on-internet by nature, so network availability for self-update is assumed rather than treated as an edge case.
3. **Addon-bundled bootstrap.** Each addon may bundle whatever version of poe-info-service it ships with. On launch, it installs its bundled copy into the shared location only if that copy is newer than what's already present there, and otherwise defers entirely to self-update to reach the latest version. This is how most users acquire poe-info-service at all, with no separate download step. An addon may instead ship a minimal web-installer that only knows how to fetch and run path 1 — equivalent in effect, and available if a given addon author prefers not to bundle the full binary.

Downloaded and self-installed binaries are signed and checksum-verified before being written to the shared location or executed, regardless of which path delivered them.

## Consequences

- **Positive**: no addon author or user manages a separate install step under normal operation — distribution rides on infrastructure addons already have.
- **Positive**: relying on a durable, already-established third-party host for the release feed (rather than self-hosted update infrastructure) minimizes the risk of the update channel itself becoming unmaintainable years into the project's life, which would otherwise strand the same class of immutable, unmaintained client this design is trying to keep working.
- **Negative**: introduces an autonomous network-update dependency — the shared instance initiates outbound requests and can pull down and execute new code without a specific action from the user or an addon author. Signing and checksum verification are required, not optional, to avoid this becoming a supply-chain attack vector. See the forthcoming credential-storage ADR for how this interacts with the secrets the service holds.
- **Negative**: self-update freshness is opportunistic rather than scheduled, per ADR-001 — accepted as a bounded, rare-case risk rather than solved outright.
