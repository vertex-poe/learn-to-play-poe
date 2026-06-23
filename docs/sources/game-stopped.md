# Game Stopped Detection

How the app detects that a Path of Exile session has ended.

---

## Source

**`Client.txt`** — session timing is derived from timestamps in the log file,
combined with OS process tracking to determine when the game exited.

## What triggers it

The app has no direct "game stopped" signal from PoE. Instead, it infers
session end by combining two things:

1. **OS process disappears** — the app polls for the game process every second.
   When a previously-running PID is no longer present, it records the current
   time as the session end.
2. **Session-start record in the database** — the duration shown on the card
   (`Active` and `Total`) is the gap between the recorded session-start
   timestamp and the stop time.

The resulting record is written to the database and appears in the Past tab as
a "Game stopped" card.

## Active vs Total duration

- **Active** — time from the most recent character login (`Client.txt`) to when
  the process exited.
- **Total** — time from when the game process was first detected (OS) to when
  it exited. Includes time spent on the login screen before selecting a
  character.
