# Game Running Detection

How the app determines whether Path of Exile is running, and how it retrieves
the executable name, PID, install directory, window position, and process start
time.

---

## What the app does, in plain terms

### Two independent sources

The app learns that the game is running from two completely separate places, and
combines what they each know to build the "Game is running" card.

**The operating system** — every second the app asks Windows the same questions
that Task Manager answers on its Details tab: is a Path of Exile process running,
what is the full path to its executable, what is its process ID, and roughly when
did it start? The OS gives a precise start timestamp, but only at
hours-and-minutes resolution (e.g. `14:30`), and with no calendar date attached.

**Path of Exile's own log file (`Client.txt`)** — PoE writes a line to this file
every time you enter a new area. The app watches this file in the background and
records each session to a local database. When a "game started" event is in the
database it carries a full timestamp with date and seconds (e.g.
`2026-06-22 14:30:05`), plus the character name and class you were playing.

Neither source alone gives the full picture. The OS knows the process is running
but not who you are playing. `Client.txt` knows your character but only starts
producing data once you enter a zone — and can't tell you the process ID or where
the game is installed. Together they cover everything the card shows.

### What the card shows and where each field comes from

When you click the "Game is running" card to expand it, you see:

| Field | Source |
|-------|--------|
| Executable name (in the header) | OS — `QueryFullProcessImageName` |
| Timestamp (top-right corner) | OS — process creation time, `HH:mm` |
| Character · Class | `Client.txt` database — most recent session start for this install |
| Started (full date + time + seconds) | `Client.txt` database when available; OS time only as last resort |
| PID | OS — process ID from `GetWindowThreadProcessId` |
| Folder | OS — parent directory of the executable |

### What happens if the game was already running before the app launched

The OS will still report the process, so the card always appears. However, if
no session-start event exists in the database for this particular instance of
the game, the app cannot know the exact date the process started — only the time.
In that case the "Started" field shows just the time (e.g. `14:30:22`) without a
date, rather than guessing that it was today (which would be wrong for overnight
sessions). Once you zone into a new area the `Client.txt` entry is written and
the database is updated, but the card's "Started" field won't update retroactively
for the current session.

If the game is not running, nothing is read at all. If multiple instances are
running (e.g. two separate Steam installs), each one gets its own card.

The result is used for three things:

- Show the "Game is running" card on the Current tab, including the session
  start time and the character name once the log has been parsed.
- Position the overlay window over the game window.
- Locate the `Client.txt` log file automatically so the app knows where to
  watch for new game events.

---

## Anti-cheat considerations

### How our access differs from cheats

Memory-reading cheats and aimbots need to *reach inside* a running process —
reading game state, entity positions, or ability cooldowns from the process's
address space. The Windows API calls that make this possible require one or more
of these access flags on the process handle:

| Flag | What it enables |
|------|----------------|
| `PROCESS_VM_READ` | Read the process's memory |
| `PROCESS_VM_WRITE` | Write to the process's memory |
| `PROCESS_CREATE_THREAD` | Inject a thread (code execution) |
| `PROCESS_ALL_ACCESS` | Everything above and more |

This app requests **`PROCESS_QUERY_LIMITED_INFORMATION`** — a flag introduced
specifically to allow read-only queries about a process (its name, path, and
timing) without granting any of the capabilities above. It is structurally
impossible to read game memory or inject code with this flag. Windows enforces
this at the kernel level; it is not a matter of the app choosing not to.

The same flag, making the same `QueryFullProcessImageName` and `GetProcessTimes`
calls for the same reason, is used by Windows Task Manager, Windows Defender,
antivirus software, hardware monitoring tools, and process explorers. Blocking
it would break standard Windows tooling on every gaming PC.

### Why we don't consider this an anti-cheat risk

Anti-cheat systems (BattleEye, Easy Anti-Cheat, VAC) watch for external
processes that interact with the game process in meaningful ways: memory reads,
code injection, API hooks inside the game process, and kernel-level drivers that
bypass process isolation. A process that opens `PROCESS_QUERY_LIMITED_INFORMATION`
and reads the exe path is in the same category as Task Manager — it is
specifically not the kind of interaction anti-cheat is designed to flag.

Additional factors that reduce risk:

- The handle is open for roughly two Win32 function calls, then released. It is
  never held persistently.
- No interaction with the game window's message queue (no `SendMessage`,
  `PostMessage`, or input injection of any kind).
- No interaction with the game's network stack, file handles, or any other
  resource.
- The overlay itself is a transparent always-on-top window with `WS_EX_TRANSPARENT`
  (click-through). This is the same technique used by the Discord overlay, OBS
  game capture, and community PoE tools that GGG has co-existed with for years.

The risk that would actually matter — an anti-cheat scanning for open handles
at `PROCESS_QUERY_LIMITED_INFORMATION` level — would produce false positives
against Windows built-in utilities running on every PC in the world. No
commercial anti-cheat system does this.

---

## Technical detail

### Poll cadence and entry point

`MainWindow::onPollTimer` fires every 1 000 ms and calls
`WindowTracker::poll(exeNames)`, returning a `QList<WindowState>` — one entry
per running game instance. Page updates and overlay changes only happen when
the set of running PIDs changes between ticks, not on every tick.

### Executable name list

The tracker searches by bare filename. The built-in list covers every known
PoE 1 distribution:

```
PathOfExile_x64Steam.exe   PathOfExileSteam.exe
PathOfExile_x64.exe        PathOfExile.exe
PathOfExile_x64            PathOfExile          (Linux / non-Windows names)
```

Users can override this list in settings. When the override is empty the
built-in list is used.

### Detection sequence — `WindowTrackerWindows::poll`

Source: `src/WindowTrackerWindows.cpp`

```
EnumWindows
  └─ for each visible top-level window:
       GetWindowThreadProcessId  → DWORD pid
       OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, pid)
       QueryFullProcessImageNameW → full exe path
       basename match against target list
       GetWindowRect              → window bounds (skipped if minimised)
       GetProcessTimes            → process creation timestamp
       CloseHandle
```

**EnumWindows** — iterates every top-level window on the desktop. Invisible
windows are skipped immediately. The window list is the entry point; there is
no separate process enumeration step.

**GetWindowThreadProcessId** — given an `HWND`, returns the PID of the process
that owns it. No process handle is required for this call.

**OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)** — opens a handle to the
process so its name and timing can be queried. See the anti-cheat section above
for detail on what this access level permits and does not permit. The handle is
opened, used for exactly two calls, and released with `CloseHandle` before the
callback returns. It is never held between poll ticks.

**QueryFullProcessImageNameW** — returns the absolute path to the executable,
e.g. `C:\Program Files (x86)\Steam\steamapps\common\Path of Exile\PathOfExile_x64Steam.exe`.
From this the app derives:

- **Executable name** — the bare filename (`PathOfExile_x64Steam.exe`)
- **Install directory** — the parent directory (`…\Path of Exile`), used to
  locate `Client.txt` and for orphan-session detection

If the basename does not match the target list the handle is closed and the
loop moves on without recording anything.

**GetWindowRect** — reads the game window's screen position and size.
Minimised windows produce a zero rect and are skipped. The rect is used to
position the overlay.

**GetProcessTimes** — reads the process creation timestamp from the kernel.
Converted to local time and formatted as `"HH:mm"`, it appears on the
"Game is running" card as the session start time.

### `WindowState` — fields returned per instance

```cpp
struct WindowState {
    QRect    rect;           // screen bounds (zero if minimised)
    QString  installDir;     // absolute path of the directory containing the exe
    QString  executableName; // bare filename, e.g. "PathOfExile_x64Steam.exe"
    QString  startedAt;      // "HH:mm" local time — process creation time from GetProcessTimes
    quint32  pid;
};
```

`startedAt` is hours and minutes only, with no calendar date. `GetProcessTimes`
returns a `FILETIME` that could give full date+time precision, but only the time
component is surfaced here because when the DB has a session record (the common
case) it is used instead and provides full `"YYYY-MM-DD HH:MM:SS"` precision.
The OS time is the compact fallback, not the primary source.

### "Started" timestamp resolution — three tiers

Source: `SessionViewPage::applyCurrentPageData` / `findStartEvent` lambda.

When building the "Game is running" card the app resolves the "Started" detail
field through three tiers in order:

**Tier 1 — most-recent DB session event (primary)**

The `log.session` WebSocket request (session ID -1, meaning the most recent
open session) returns up to the 10 most recent session events from the
database. If the most recent event has `eventType = "start"`, it
is used directly. This is the normal path: `Client.txt` was being watched when
the game launched, a start record was written, and `occurredAt` gives
`"YYYY-MM-DD HH:MM:SS"` with full date and seconds. Character name and class
from this event are also shown in the expanded card.

**Tier 2 — folder + time match (fallback)**

If the most-recent event is not a "start" (e.g. the game restarted and the new
start hasn't been recorded yet, or there are multiple installs and the most-recent
event belongs to a different one), the app scans the same batch of 10 events
looking for any `"start"` record where:

- `installPath` begins with `g.installDir` (case-insensitive on Windows), linking
  the event to the correct `Client.txt` file, and
- the time component of `occurredAt` is within 60 seconds of the window's
  reported `startedAt` time.

If a match is found its `occurredAt` is used as the full started datetime.

**Tier 3 — OS time only (last resort)**

If neither DB path succeeds the app shows just the time string from
`WindowState::startedAt` (or the detection timestamp stored in `m_detectedAt`,
formatted `"HH:mm:ss"`), with no date prefix. Displaying a bare time rather than
prepending today's date avoids silently showing a wrong date for processes that
started before midnight.

### Auto-detection of install directories

When `autoDetectInstallDir` is enabled (default: on), any `installDir` not
already in the saved config is appended and saved. This is how the app finds
`Client.txt` without manual configuration on first run.

---

## Alternatives considered

### WMI (`Win32_Process`)

Windows Management Instrumentation can enumerate processes and return name,
path, PID, and creation time without ever calling `OpenProcess`. A WMI query
like `SELECT * FROM Win32_Process WHERE Name LIKE 'PathOfExile%'` returns all
the same fields with zero process handles opened.

**Why we didn't use it:** WMI queries go through the WMI service, which
introduces latency (typically 50–200 ms per query vs. <5 ms for `EnumWindows`
on a normal desktop). Running a 200 ms query every second on the UI thread
would itself risk triggering the frame-budget guards added in the UI freeze
hardening work. WMI would be the right choice if the open-handle concern ever
became a real problem — the trade-off is latency and a more complex COM
initialization path. It is not worth the cost given the current risk assessment.
