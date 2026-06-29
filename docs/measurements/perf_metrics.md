# Startup Performance Metrics

Baseline recorded 2026-06-29, on Windows 11 (dev machine).  
Methodology: `just test-perf` — 3 runs per (tab × scenario), median taken.  
All times except `first_paint` are delta ms from the preceding milestone. `first_paint` is absolute ms from process start (clock starts before QApplication).
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 790 |

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
| guide (placeholder) | 606 | 26 | 1 | 0 | 2 | 9 |
| chats | 656 | 34 | 165 | 236 | 71 | 42 |
| dms | 596 | 44 | 94 | 110 | 37 | 25 |
| stash (placeholder) | 585 | 25 | 0 | 1 | 2 | 8 |
| profile (placeholder) | 613 | 22 | 0 | 1 | 2 | 8 |
| current | 574 | 24 | 0 | 0 | 1 | 9 |
| log | 602 | 25 | 819 | 122 | 41 | 30 |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | 613 | 24 | 9 |
| chats | 607 | 37 | 136 |
| dms | 581 | 29 | 73 |
| stash (placeholder) | 582 | 25 | 9 |
| profile (placeholder) | 599 | 23 | 8 |
| current | 590 | 25 | 11 |
| log | 597 | 26 | 813 |

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
