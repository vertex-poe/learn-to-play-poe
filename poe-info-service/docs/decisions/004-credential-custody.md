# ADR-004: Credential Custody for POESESSID and OAuth Sessions

**Status**: Decided
**Date**: 2026-07-02
**Deciders**: MovingCairn

---

## Context

poe-info-service needs `POESESSID` to call PoE's legacy (cookie-authenticated) web API, and will need OAuth tokens for PoE's official API, Steam, and potentially other sources, in order to serve data that isn't available through the other. `POESESSID` is qualitatively different from the rest of the data this service handles: it is capable of destructive account actions (trading, messaging), whereas the read-only game data (characters, levels, session history) the service otherwise gathers and caches is low-sensitivity and local-only.

Capturing `POESESSID` requires observing the session cookie during an actual login, which requires a WebView — a capability poe-info-service itself does not have and, per [ADR-001](001-single-shared-instance-lifecycle.md), should not need, since it stays a lightweight Go process. Only some clients (e.g. this app) have a WebView available.

## Decision

- poe-info-service is responsible for these credentials being available to it, but is not necessarily their point of origin:
  - For `POESESSID`: a WebView-capable client captures it by observing the login cookie and hands it to poe-info-service.
  - For OAuth sessions: poe-info-service may initiate the flow itself, where the provider's flow allows it, via the system's default browser plus a local loopback redirect listener using PKCE — no client capability required for this case.
- poe-info-service never returns these credentials to any client, under any circumstance. Clients receive data derived from a credential, never the credential itself. This holds regardless of which addon is asking, whether or not it's the addon that originally supplied the credential.
- Where and how these credentials are physically stored — in-memory only for the life of the process, or persisted, and by what mechanism if persisted — is intentionally **not** decided by this ADR. It is the subject of a forthcoming, dedicated ADR.

## Consequences

- **Positive**: no addon, however many exist on a given machine or however trusted, can obtain the raw `POESESSID` or an OAuth token through the shared service. The blast radius of a compromised or malicious addon client is bounded to whatever the WebSocket API already exposes, not the underlying account credential.
- **Positive**: decouples credential acquisition from any specific client's capabilities. OAuth-based sources do not require a WebView at all — only `POESESSID` does, because it is the one credential type obtained by cookie observation rather than a standard redirect flow.
- **Resolved by** [ADR-005](005-credential-storage-mechanism.md): the physical storage mechanism and the decision to persist across restarts. Expiry/staleness policy for stored credentials remains open and is not yet the subject of a dedicated ADR.
