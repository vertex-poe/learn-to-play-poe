# Game Started Detection

How the app detects that a new Path of Exile session has begun.

---

## Source

**`Client.txt`** — Path of Exile's log file, watched in the background by the
app while the game is running.

## What triggers it

When you log in and select a character, PoE writes a sequence of lines to
`Client.txt`. The app recognises the login pattern and records a session-start
event to the local database with:

- Full timestamp (`YYYY-MM-DD HH:MM:SS`)
- Character name and class

This is the primary source for the "Started" field on the
[Game is running](game-running.md) card and for "Game started" entries in the
Past tab.

## Limitations

- The event is only recorded if the app was already watching `Client.txt` when
  you logged in. If the app launched after you were already in a zone, the
  session start is missed and the OS detection time is used as a fallback.
- Character name and class are not available from any other source — if this
  event is missed they will not appear on the card.
