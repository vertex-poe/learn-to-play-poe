# Startup Performance Metrics

Baseline recorded 2026-06-29, on Windows 11 (dev machine).  
Methodology: `just test-perf` — 10 runs per (tab × scenario), median taken.  
All times except `first_paint` are delta ms from the preceding milestone. `first_paint` is absolute ms from process start (clock starts before QApplication).
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 1142 |

## Per-tab milestones

`first_paint` = NavBar renders (user sees UI).  
`first_interaction` = user click on default tab registered.  
`first_load` = SQL data fully delivered to UI.  
`final_paint` = UI painted with loaded data.  
`final_interaction` = user click on swap tab registered.  
`menu_swap_*` = swap tab page rendered.

### Baseline scenario (default tab â†’ swap tab after data loads)

| Tab | first_paint | first_interaction | first_load | final_paint | final_interaction | menu_swap_final |
|---|---|---|---|---|---|---|
| guide (placeholder) | 718 | 28 | 1 | 1 | 2 | 13 |
| chats | 853 | 53 | 255 | 315 | 79 | 61 |
| dms | 590 | 32 | 94 | 113 | 35 | 26 |
| stash (placeholder) | 592 | 26 | 1 | 1 | 3 | 8 |
| profile (placeholder) | 555 | 23 | 0 | 1 | 2 | 8 |
| current | 563 | 25 | 0 | 0 | 1 | 8 |
| log | 580 | 24 | 758 | 119 | 34 | 26 |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | 709 | 33 | 13 |
| chats | 732 | 42 | 180 |
| dms | 585 | 29 | 89 |
| stash (placeholder) | 600 | 26 | 10 |
| profile (placeholder) | 556 | 23 | 9 |
| current | 581 | 24 | 10 |
| log | 574 | 24 | 711 |

## Notes

- `current` (SessionViewPage) `first_load` â‰ˆ `final_paint` because SessionViewPage has no
  running session in the test DB; the data load is instantaneous and `final_paint` is
  recorded immediately after `first_load` (no paint-event round-trip needed).
- `log` has the highest `first_interaction` and `first_load` due to loading 120 sessions
  from the test DB. Real-world counts are usually lower for a single league.
- Placeholder tabs (guide, stash, profile) have ~3 ms between `first_load` and `final_paint` because there is no async data fetch â€” the load is a no-op.

## Reference Implementations

These are barebones test applications built to measure the absolute minimum framework overhead without any application logic.

| Implementation | first_paint | first_interaction | first_load | final_paint | final_interaction |
|---|---|---|---|---|---|
| ref_basic_app | 492 | 65 | - | - | - |
| ref_data_app | 562 | 58 | 8 | 10 | 44 |
