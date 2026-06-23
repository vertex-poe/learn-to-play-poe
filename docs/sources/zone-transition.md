# Zone Transition Detection

How the app detects when you move between areas in Path of Exile.

---

## Source

**`Client.txt`** — Path of Exile appends a line to its log file every time you
enter a new area.

## What triggers it

When you enter a zone (via waypoint, area transition, or login), PoE writes a
line to `Client.txt` of the form:

```
... [INFO Client ...] : You have entered <Area Name>.
```

The app recognises this pattern and records a zone-transition event with:

- Area name
- Area level (looked up from game data by area name)
- Timestamp

These appear as individual cards in the Current tab's zone history. The
duration shown on each card is the time between entering that zone and entering
the next one.

## Limitations

- Only zones entered after the app started watching `Client.txt` are captured.
  Zones entered before the app launched do not appear.
- Area level is derived from a bundled lookup table, not from the log line
  itself, so private leagues or future content patches may show an incorrect
  level until the table is updated.
