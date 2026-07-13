<!-- docs/rationales/oauth-api.md (markdown) -->

# Official OAuth API — Feature Rationale

Path of Exile's official developer API (`api.pathofexile.com`) is GGG's
sanctioned, documented path for third-party tools — the one we prefer and use
wherever it can do the job (see [the Legacy API rationale](legacy-api.md) for
when and why we still fall back to the older website interface). Logging in
for it works differently from both `POESESSID` and the Steam Web API key.
This page explains what that difference is, why it's safer, and what
signing in here actually grants.

---

## In plain terms

### We don't touch your login at all — not even a browser we control

When you click Login, this app doesn't open its own browser window. Instead
it asks `poe-info-service` — the background service this app talks to — to
open **your own default system browser**: whatever you'd get from clicking a
link anywhere else on your computer. That's a real, independent browser
window, entirely outside this app's process, pointed at `pathofexile.com`'s
own login page.

You sign in there exactly as you would visiting the site directly, including
any two-factor step GGG has enabled for your account. Once you approve the
permissions PoE's page shows you, GGG's server redirects your browser back to
a tiny local address on your own machine — `poe-info-service` is listening
there for exactly that one redirect, and nothing else. Neither this app nor
poe-info-service is present in the tab your login happens in, cannot see what
you type, and never sees your password.

### Why this is a different (and stronger) mechanism than POESESSID/Steam

`POESESSID` and the Steam Web API key are both captured by watching a
*passive* signal — a cookie, or text a page prints — appear in a browser this
app itself embeds. The official OAuth API doesn't need that: it's built
around a standardized handoff (OAuth 2.0 with PKCE, a well-established
industry protocol) where GGG's server directly hands back a scoped
credential once you've approved it, addressed to the one-time local address
poe-info-service opened for that purpose. There's no cookie to intercept and
no page text to read — the credential is deliberately issued to us, for
exactly the permissions shown on GGG's consent screen, nothing more.

### What's actually granted

The consent screen GGG shows you lists precisely what's being requested:
reading your leagues, stash tabs, and characters. It cannot edit anything,
post to the forums, or trade on your behalf — the older website interface is
still what those specific actions require (see the Legacy API rationale).

### Your credential is stored by your operating system, not by us

Same guarantee as `POESESSID` and the Steam Web API key: nothing from this
login is written to our configuration file, our database, or any other file
we control. `poe-info-service` hands it straight to your operating system's
built-in secure credential store (Windows Credential Manager, macOS
Keychain, or the Linux Secret Service API) and never returns the value to
this app or any other client — only whether one is currently held. See
[the Legacy API rationale](legacy-api.md#your-session-token-is-stored-by-your-operating-system-not-by-us)
for detail on those stores.

### It renews itself

This credential expires roughly hourly, like most modern login sessions, but
`poe-info-service` renews it automatically in the background well before
that happens — you don't need to log in again for that. If a renewal
ultimately fails (for example, you revoked access from GGG's side, or the
credential has gone unused long enough that even a renewal is no longer
accepted), the Login button here will ask you to sign in again.

---

## Technical detail

### The protocol

This uses OAuth 2.0's Authorization Code grant with PKCE (Proof Key for Code
Exchange, RFC 7636) — the flow the industry has converged on for exactly
this situation: a desktop application that cannot keep a secret embedded in
its own binary. `poe-info-service` runs a temporary HTTP listener bound only
to `127.0.0.1` on a port chosen at random for that one login attempt, and
tears it down again as soon as the redirect lands or the attempt times out.
Nothing about this reaches beyond your own machine.

### Where this runs

Unlike `POESESSID` and the Steam Web API key, this flow needs no WebView
capability from this app at all — `poe-info-service` runs it entirely on its
own, launching your system browser and listening for the one redirect
itself. That's a deliberate design choice (see
[poe-info-service's ADR-004](https://github.com/MovingCairn/learn-to-play-poe/blob/main/poe-info-service/docs/decisions/004-credential-custody.md)):
it keeps this specific credential's acquisition decoupled from any one
client's capabilities.

### Scope of use

The resulting credential is used solely to make the specific read-only API
calls each feature that needs it requires, at rates that respect GGG's
published rate limits. We do not use it speculatively or for anything
outside the scope of the feature that required the login in the first
place.
