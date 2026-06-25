<!-- docs/rationales/legacy-api.md (markdown) -->

# Legacy API — Feature Rationale

Path of Exile has two integration surfaces for third-party tools: an official
developer API, and the older website-based interface that predates it. Some of
what players want from a companion tool is only available through the older
interface — not because GGG intends it to be off-limits, but because the newer
API is still catching up to everything the website has always been able to
provide.

Using the older interface requires logging into pathofexile.com. This page
explains what that login does and does not involve, how we keep the resulting
credential safe, and why we need it at all.

---

## In plain terms

### We never see your password

When you click Login, we open a real browser window — a full embedded Chromium
browser, the same engine that powers Google Chrome — pointed directly at
pathofexile.com. You log in on GGG's own website, through GGG's own login form,
over a standard HTTPS connection that your browser validates exactly as it
would from any other browser.

Our application sits outside that browser. It cannot intercept what you type,
cannot read your credentials as they are sent to GGG's servers, and does not
act as a proxy or a relay at any point in the process. The login is between you
and GGG, exactly as if you had opened Chrome and gone to pathofexile.com
yourself.

What we watch for is the session token that GGG places in the browser's cookies
once you have successfully authenticated. That token — `POESESSID` — is what
GGG's website uses to recognise "this request is from a logged-in player." We
capture it at that moment, store it in your operating system's secure
credential store (see below), and close the browser window. Your password never
passes through any part of our code.

### Your session token is stored by your operating system, not by us

We do not write your session token to our configuration file, our database, or
any other file we control. Instead, we hand it immediately to your operating
system's built-in secure credential store:

- **Windows** — the [Windows Credential Manager](https://support.microsoft.com/en-us/windows/accessing-credential-manager-1b5c916a-6a16-889f-8581-fc16e8165ac0),
  the same encrypted vault that protects Wi-Fi passwords, Remote Desktop
  credentials, and Windows Hello data.
- **macOS** — the [Keychain](https://support.apple.com/guide/keychain-access/what-is-keychain-access-kyca1083/mac),
  the system keychain that protects certificates, bank passwords, and app
  credentials.
- **Linux** — the [Secret Service API](https://specifications.freedesktop.org/secret-service/),
  implemented by GNOME Keyring or KWallet depending on your desktop
  environment.

Each of these stores encrypts its contents and ties access to your login
session. Other applications cannot read from the store without explicit
permission, and on most configurations the vault is protected by your system
password or biometric authentication. This is meaningfully more secure than
anything we could implement ourselves in an application data file.

If you uninstall the app, the stored token stays in your OS credential store
until you remove it. You can do this at any time using your system's native
Credential Manager or Keychain interface, or by clicking Logout in the Accounts
page, which removes it through the same API that stored it.

### Why we need to log in at all

GGG built a new official developer API that third-party tools are encouraged to
use. We prefer it and use it whenever it genuinely serves what a feature
requires — meaning the data is present, accurate, in a useful format, and
available within rate limits that make the feature viable in practice.

The honest reality is that GGG has not yet moved everything to the new API.
Some data players care about is not there yet: character details in specific
formats, certain stash tab types, data that updates frequently enough to be
useful. In some cases the information exists in the new API but in a form that
makes building a meaningful feature around it impractical — rate limits that
are too low, response times that make the data stale before it arrives, or
formats that require significant translation to produce something helpful.

When that happens, the older website interface is the only path available.
We use it only where we genuinely cannot serve the player any other way.
For a detailed account of how we decide when that threshold has been met and
what restraints we commit to when it has, see
[ADR-004](../decisions/004-game-addon-interaction-principles.md).

When GGG adds something to the official API that makes a website call
unnecessary, we move to the sanctioned surface. We would rather be there.

---

## Technical detail

### What POESESSID is and what it grants

`POESESSID` is an HTTP session cookie that pathofexile.com sets when you
successfully authenticate. It is a long random string — a bearer token — that
GGG's servers recognise as a valid logged-in session. Any request that carries
a valid `POESESSID` is treated as coming from the authenticated account that
session belongs to.

Bearer tokens of this kind are standard web authentication practice. They are
not tied to an IP address or device, which means they can be used in API calls
without the user present in a browser. They expire: GGG's sessions time out
eventually, at which point the token stops working and a fresh login produces a
new one.

### Why we capture it from the browser cookie store rather than asking the user to paste it

The alternative is asking the player to locate their `POESESSID` manually — a
confusing and error-prone process that exposes the token on-screen and requires
players to know what they are looking for. Capturing it from the browser at
login is the least-friction path and the one most consistent with the security
model: the player authenticates through GGG's real login flow, and we receive
only the credential that GGG generated as a direct result of that.

### Scope of use

The session token is used solely to make requests to GGG's website and API
endpoints on the player's behalf, at the player's explicit direction. We do not
use it speculatively, in background polling, or for anything outside the scope
of the feature that required the login in the first place.

Rate limiting, local caching, and request restraint are built in from the
beginning of any feature that uses these endpoints — consistent with
[ADR-004 §5](../decisions/004-game-addon-interaction-principles.md#5-use-the-legacy-website-api-sparingly-and-with-restraint).
The health of GGG's infrastructure is a shared resource. We do not abuse it.
