# Game Running Detection

How the app determines whether Path of Exile is running, and how it retrieves
the executable name, PID, install directory, window position, and process start
time.

---

## What the app does, in plain terms

Every second, the app does the equivalent of opening Windows Task Manager and
looking down the process list for a Path of Exile executable. It knows which
process names to look for from a built-in list of every known PoE 1 distribution
(`PathOfExile_x64Steam.exe`, `PathOfExile.exe`, and so on) that ships with the
app and can be customised in Settings if needed. If it finds a matching process,
it reads three things that Task Manager also shows you: the process name, the
full path to the executable on disk, and when the process was started. It also
reads the game window's position on screen so the overlay knows where to sit.

That's it. The app never reads what is happening *inside* the game process — no
game state, no memory, no network traffic. It only asks Windows "is this process
running, where is it on disk, and where is its window?" — the same questions
Task Manager answers on the Details and Performance tabs.

If the game is not running, nothing is read at all. If multiple instances are
running (e.g. two separate Steam installs), each one gets its own entry.

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
    QString  startedAt;      // "HH:mm" local time — process creation time
    quint32  pid;
};
```

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
