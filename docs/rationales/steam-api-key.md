<!-- docs/rationales/steam-api-key.md (markdown) -->

# Steam Web API Key — Feature Rationale

Steam presence (showing what you're currently playing, and richer details
about it) can be built two ways: scraping your public profile page, or calling
Valve's official Web API. The official API is more reliable and gives us
fields the public profile scrape cannot — but it requires a Steam Web API key
tied to your account. This page explains what logging in for that key does and
does not involve, how we keep it safe, and why we need it at all.

---

## In plain terms

### We never see your Steam password

When you click Login, we open a real browser window — a full embedded
Chromium browser, the same engine that powers Google Chrome — pointed
directly at `steamcommunity.com/dev/apikey`. You log into Steam on Valve's own
website, through Valve's own login form (including Steam Guard, if you have
it enabled), over a standard HTTPS connection that your browser validates
exactly as it would from any other browser.

Our application sits outside that browser. It cannot intercept what you type,
cannot read your credentials as they are sent to Valve's servers, and does not
act as a proxy or a relay at any point in the process. The login is between
you and Valve, exactly as if you had opened Chrome and gone to
steamcommunity.com yourself.

What we watch for is the key itself, once Valve's page displays it. Unlike
`POESESSID`, this isn't a cookie we intercept in the background — it's text
Valve's own page prints on screen ("Key: ...") once you already have one
registered. We read that text, store it in your operating system's secure
credential store (see below), and close the browser window. Your password
never passes through any part of our code.

### If you don't have a key yet

Registering a Steam Web API key for the first time requires agreeing to
Valve's Web API Terms of Use on a form Valve itself presents — we do not, and
will not, submit that agreement on your behalf. In that case the browser
window will simply show you Valve's registration form to fill in yourself.
Once submitted (or if you already had a key from before), the same window
picks up the key automatically and closes on its own — you don't need to
click Login a second time.

### Your key is stored by your operating system, not by us

Same as `POESESSID`: we do not write your Steam Web API key to our
configuration file, our database, or any other file we control. Instead, we
hand it immediately to your operating system's built-in secure credential
store — Windows Credential Manager, macOS Keychain, or the Linux Secret
Service API, depending on your platform. See
[the Legacy API rationale](legacy-api.md#your-session-token-is-stored-by-your-operating-system-not-by-us)
for detail on those stores; the same guarantees apply here. Once stored, the
key is never sent back to this application or displayed again — only whether
one is present.

### Why this key doesn't need to be refreshed

A Steam Web API key does not expire the way a login session does. Capturing
it is a one-time action, not something we need to repeat in the background —
once it's stored, it stays valid until you revoke it yourself (from Valve's
own `steamcommunity.com/dev/apikey` page, or by clicking Logout here, which
only removes our local copy).

---

## Technical detail

### What the key grants

A Steam Web API key authorizes calls to Valve's `ISteamUser`/`IPlayerService`
endpoints on your behalf — the same endpoints Steam's own official tools use.
Without one, Steam presence falls back to scraping your public profile page,
which is more brittle and exposes fewer fields.

### Why we scrape the page's text rather than asking you to paste the key

The alternative is asking you to locate your key manually on
`steamcommunity.com/dev/apikey` and paste it in — an extra round trip that
exposes the key on-screen and requires you to know what you're looking for.
Reading it directly off the page you're already logged into is the
least-friction path, and the one most consistent with the security model: you
authenticate through Valve's real login flow, and we receive only the value
Valve's own page already displays to you.
