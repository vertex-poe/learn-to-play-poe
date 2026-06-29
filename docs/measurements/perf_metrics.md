# Startup Performance Metrics

Baseline recorded 2026-06-29, on Windows 11 (dev machine).  
Methodology: `just test-perf` — 3 runs per (tab × scenario), median taken.  
All times are absolute ms from process start (clock starts before QApplication).  
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 1614 |

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
| guide (placeholder) | 1380 | 1411 | 1413 | 1417 | 1423 | 1442 |
| chats | 1324 | 1535 | 1740 | 1743 | 1813 | 1855 |
| dms | 1287 | 1416 | 1524 | 1525 | 1568 | 1596 |
| stash (placeholder) | 1348 | 1377 | 1380 | 1385 | 1390 | 1405 |
| profile (placeholder) | 1293 | 1321 | 1323 | 1327 | 1334 | 1370 |
| current | 1269 | 1298 | 1301 | 1301 | 1306 | 1319 |
| log | 1371 | 2234 | 2331 | 2354 | 2407 | 2436 |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | 1290 | 1318 | 1336 |
| chats | 1271 | 1484 | 1753 |
| dms | 1334 | 1474 | 1672 |
| stash (placeholder) | 1400 | 1429 | 1450 |
| profile (placeholder) | 1357 | 1386 | 1403 |
| current | 1372 | 1406 | 1428 |
| log | 1380 | 2235 | 2472 |

## Notes

- `current` (SessionViewPage) `first_load` ≈ `final_paint` because SessionViewPage has no
  running session in the test DB; the data load is instantaneous and `final_paint` is
  recorded immediately after `first_load` (no paint-event round-trip needed).
- `log` has the highest `first_interaction` and `first_load` due to loading 120 sessions
  from the test DB. Real-world counts are usually lower for a single league.
- Placeholder tabs (guide, stash, profile) have ~3 ms between `first_load` and `final_paint`
  because there is no async data fetch — the load is a no-op.
