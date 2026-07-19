<!-- README.md (markdown) -->

# poe-info-service

`poe-info-service` is a standalone, shared Go service — not part of any one
client's codebase. It runs once per machine and acts as a unifying, caching,
and rate-limiting layer in front of the various Path of Exile APIs (the
official OAuth data API, the legacy web API, Steam presence, and whatever
else follows), so that every client tool talking to it gets a consistent,
already-cached, already-rate-limited view instead of each reimplementing its
own integration against GGG's (and others') raw APIs. See root
[ADR-006](../docs/decisions/006-poe-info-service.md) for why this exists as
a separate service rather than logic embedded in any one addon.

[`l2p-poe`](../) is this service's first consumer, developed alongside it —
but it is not the only one it's meant to serve. Any addon or tool on the
same machine can depend on it (see [`CONTRIBUTING.md`](CONTRIBUTING.md)).

## Design principle: aim for parity with the wrapped APIs

Because this service exists to serve any number of present and future
clients, not just whatever `l2p-poe`'s current UI happens to need today, it
generally aims for parity with the upstream APIs it wraps — exposing both an
unauthenticated and an authenticated variant of an endpoint when the
upstream API offers both, rather than collapsing down to only one.

The naming convention this follows: **the plain, unqualified method name is
always the complete-for-a-signed-in-user variant**, with an
explicitly-suffixed sibling for the deliberately narrower one — not the
other way around. A caller reaching for "the leagues list" without knowing
two similarly-named methods exist should get the complete one by default.
Concretely: `poe.leagues.list` calls the Bearer-authenticated `GET
/account/leagues` (private leagues included, for whichever account is
signed in), while `poe.leagues.public` calls the public, unauthenticated
`GET /leagues` for a caller that specifically wants the account-independent
catalogue (or has no signed-in account at all) — see
[`docs/architecture.md`](docs/architecture.md)'s "PoE Leagues" section for
the full rationale and both methods' details.

## Documentation

- [`CONTRIBUTING.md`](CONTRIBUTING.md) — how to build/test/contribute, and
  which doc to update for which kind of change.
- [`docs/architecture.md`](docs/architecture.md) — how the service actually
  works today.
- [`docs/api.md`](docs/api.md) — the wire-level WebSocket API reference.
- [`docs/schema.md`](docs/schema.md) — the SQLite database schema.
- [`docs/decisions/`](docs/decisions/) — this service's own ADRs.
