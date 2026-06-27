# Startup Performance Metrics

Baseline recorded 2026-06-27, commit d23edb6+, on Windows 11 (dev machine).  
Methodology: `just test-perf` — 3 runs per (tab × scenario), median taken.  
All times are absolute ms from process start (clock starts before QApplication).  
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 1339 |

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
| guide (placeholder) | 987 | 1055 | 1059 | 1062 | 1129 | 1156 |
| chats | 1060 | 1307 | 1512 | 1515 | 1581 | 1619 |
| dms | 1029 | 1169 | 1284 | 1286 | 1326 | 1352 |
| stash (placeholder) | 963 | 993 | 996 | 1001 | 1008 | 1085 |
| profile (placeholder) | 924 | 953 | 957 | 960 | 966 | 1001 |
| current | 1015 | 1047 | 1050 | 1050 | 1058 | 1071 |
| log | 1059 | 1898 | 1997 | 2021 | 2093 | 2128 |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | 1003 | 1033 | 1054 |
| chats | 974 | 1174 | 1431 |
| dms | 983 | 1108 | 1260 |
| stash (placeholder) | 938 | 965 | 981 |
| profile (placeholder) | 931 | 957 | 977 |
| current | 990 | 1020 | 1044 |
| log | 1008 | 1809 | 2002 |

## Notes

- `current` (SessionViewPage) `first_load` ≈ `final_paint` because SessionViewPage has no
  running session in the test DB; the data load is instantaneous and `final_paint` is
  recorded immediately after `first_load` (no paint-event round-trip needed).
- `log` has the highest `first_interaction` and `first_load` due to loading 120 sessions
  from the test DB. Real-world counts are usually lower for a single league.
- Placeholder tabs (guide, stash, profile) have ~3 ms between `first_load` and `final_paint`
  because there is no async data fetch — the load is a no-op.
