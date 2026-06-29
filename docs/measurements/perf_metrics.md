# Startup Performance Metrics

Baseline recorded 2026-06-29, on Windows 11 (dev machine).  
Methodology: `just test-perf` — 3 runs per (tab × scenario), median taken.  
All times except `first_paint` are delta ms from the preceding milestone. `first_paint` is absolute ms from process start (clock starts before QApplication).
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 1283 |

## Per-tab milestones

`first_paint` = NavBar renders (user sees UI).  
`first_interaction` = user click on default tab registered.  
`first_load` = SQL data fully delivered to UI.  
`final_paint` = UI painted with loaded data.  
`final_interaction` = user click on swap tab registered.  
`menu_swap_*` = swap tab page rendered.

### Baseline scenario (default tab → swap tab after data loads)

| Tab | first_paint | first_interaction | first_load | final_paint | final_interaction | menu_swap_final |
|---|---|---|---|---|---|---|
| guide (placeholder) | 1392 | 54 | 4 | 5 | 7 | 59 |
| chats | 1666 | 302 | 264 | 3 | 88 | 55 |
| dms | 1606 | 147 | 136 | 2 | 48 | 30 |
| stash (placeholder) | 1490 | 32 | 4 | 5 | 7 | 20 |
| profile (placeholder) | 1445 | 31 | 3 | 6 | 6 | 14 |
| current | 1369 | 33 | 3 | 0 | 6 | 12 |
| log | 1434 | 38 | 900 | 124 | 70 | 34 |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | 1275 | 28 | 19 |
| chats | 1634 | 297 | 436 |
| dms | 1432 | 157 | 194 |
| stash (placeholder) | 1498 | 32 | 19 |
| profile (placeholder) | 1514 | 33 | 23 |
| current | 1509 | 33 | 24 |
| log | 1431 | 35 | 918 |

## Notes

- `current` (SessionViewPage) `first_load` ≈ `final_paint` because SessionViewPage has no
  running session in the test DB; the data load is instantaneous and `final_paint` is
  recorded immediately after `first_load` (no paint-event round-trip needed).
- `log` has the highest `first_interaction` and `first_load` due to loading 120 sessions
  from the test DB. Real-world counts are usually lower for a single league.
- Placeholder tabs (guide, stash, profile) have ~3 ms between `first_load` and `final_paint` because there is no async data fetch — the load is a no-op.

## Reference Implementations

These are barebones test applications built to measure the absolute minimum framework overhead without any application logic.

| Implementation | first_paint | first_interaction | first_load | final_paint | final_interaction |
|---|---|---|---|---|---|
| ref_basic_app | 484 | 39 | - | - | - |
| ref_data_app | 375 | 41 | 5 | 10 | 43 |
