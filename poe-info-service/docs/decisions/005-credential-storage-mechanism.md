# ADR-005: Credential Storage Mechanism

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

[ADR-004](004-credential-custody.md) established that poe-info-service is responsible for making `POESESSID` and future OAuth tokens available to itself, but explicitly deferred the physical storage mechanism to this ADR. Two things sharpen the requirement beyond "store a secret somewhere":

- [ADR-001](001-single-shared-instance-lifecycle.md) makes the service's process lifecycle ephemeral by design — it self-shuts-down after a keep-alive idle timeout and restarts whenever an addon or the game launches. If credentials were held in memory only, every idle-shutdown cycle would silently discard them, forcing re-authentication (a WebView-driven `POESESSID` recapture) far more often than a user would tolerate — plausibly several times a day, not once per install. Persisting credentials across process restarts is load-bearing for basic usability given the lifecycle already decided, not a durability nice-to-have.
- `POESESSID` specifically carries destructive account capability (trading, messaging), unlike the read-only data the rest of the service handles, so the mechanism needs to hold up to real scrutiny.

poe-info-service is a Go binary built independently of the Qt/CMake toolchain that builds this app (root [ADR-006](../../../docs/decisions/006-poe-info-service.md)). Avoiding cgo is not a constraint here — the project ships a distinct build per platform regardless, so cgo is available wherever it is the better tool, not something to route around.

### Alternatives considered and rejected

**1. Reuse QtKeychain (already used by this app) via cgo from poe-info-service.** Rejected. QtKeychain's public API is C++ (`QObject`-based jobs), not directly callable from `cgo`, which only binds `extern "C"` functions — reaching it would require writing and maintaining a C shim. More significantly, QtKeychain's jobs dispatch through Qt's signal/slot mechanism, so using it at all requires a live `QCoreApplication` event loop running inside the Go process — i.e., linking the Qt runtime into poe-info-service. That defeats the reason poe-info-service is a Go binary in the first place: an independent, lightweight build with its own toolchain, decoupled from Qt/CMake. QtKeychain itself does nothing on any platform except call the same OS credential-store APIs directly reachable from Go — routing through it buys no capability, only weight.

**2. A unified Go keyring wrapper library** (99designs/keyring or its maintained forks, lox/keyring and ByteNess/keyring). Rejected. The original 99designs/keyring is unmaintained (no release since December 2022). Both forks are active as of this writing — ByteNess/keyring released v1.11.0 on 2026-06-11; lox/keyring, maintained by 99designs/keyring's original author, shows ongoing commit and CI activity — but both are effectively single-maintainer, best-effort continuations of a project that has already gone stale once before. The actual requirement here — store, retrieve, and delete a handful of named credentials — is narrow enough that a wrapper's abstraction saves little integration effort, while adding a dependency whose long-term maintenance is less certain than the individually well-established libraries it wraps, plus an extra layer of indirection between poe-info-service and the OS APIs for a case that doesn't need one.

**3. Three separate, independently-established, actively-maintained platform libraries, composed behind our own small interface.** Chosen — see Decision.

## Decision

- Credentials persist across service restarts. The single shared service instance ([ADR-001](001-single-shared-instance-lifecycle.md)) is the sole owner and sole reader/writer of this storage — nothing else touches it directly, consistent with [ADR-004](004-credential-custody.md)'s rule that credentials are never handed back to clients.
- Storage is implemented as a small internal Go package (`Store(service, key, value string) error`, `Get`, `Delete`) with a platform-specific implementation selected by Go build tags, each backed directly by the native OS credential store:
  - **macOS** — [keybase/go-keychain](https://github.com/keybase/go-keychain), `cgo` bindings directly to Security.framework. Actively maintained (release February 2025). Chosen over shelling out to the `security` CLI — the common cgo-free workaround — because `cgo` is available here, and a native API call avoids the CLI-shelling risk of some unrelated local process being able to invoke the same `security` grant.
  - **Windows** — [danieljoos/wincred](https://github.com/danieljoos/wincred), pure Go syscalls directly against Credential Manager (`CredWrite`/`CredRead`). Actively maintained (release October 2025). `cgo` is not needed here regardless of policy — Credential Manager is a plain DLL export, reachable directly via Go's syscall mechanism.
  - **Linux** — [godbus/dbus](https://github.com/godbus/dbus), speaking the Secret Service D-Bus interface directly. `cgo` is not needed here either — D-Bus is a wire protocol, not a C API.
- Each credential is stored under a service-owned identifier (not a per-addon one), consistent with ADR-004: a fixed service name plus a per-credential-type key (e.g. `poesessid`, and future OAuth token keys as those are added).

## Consequences

- **Positive**: each platform's credential access goes through the most directly maintained, most narrowly-scoped library available for that specific OS mechanism, rather than through a wrapper's shared abstraction or QtKeychain's much heavier dependency chain.
- **Positive**: no Qt runtime, no embedded event loop, and no C++ shim layer inside poe-info-service — it remains a small, independently buildable Go binary, consistent with the reasoning in root ADR-006.
- **Positive**: because the actual requirement (three verbs, a handful of named credentials) is small, the internal interface wrapping these three libraries is easy to review and audit in full — appropriate given `POESESSID`'s destructive capability.
- **Negative**: three separate dependencies to track for security advisories and breaking changes instead of one. Mitigated by each being independently well-established and narrowly used — a small, auditable amount of our own glue code per platform, not a large adopted surface.
- **Negative**: no built-in mock/testing backend of the kind a wrapper library typically ships with. A test-only in-memory implementation of the same internal interface needs to be written and maintained separately for automated tests.
- **Revisit if**: the credential surface grows to need backends none of these three cover well (e.g., hardware-backed keys, enterprise SSO integration) — at that point a wrapper's broader backend coverage may outweigh the durability concerns raised here.
