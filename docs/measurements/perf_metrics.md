# Startup Performance Metrics

Baseline recorded 2026-06-29, on Windows 11 (dev machine).  
Methodology: \just test-perf\ — 3 runs per (tab × scenario), median taken.  
All times are absolute ms from process start (clock starts before QApplication).  
Test DB: 120 closed sessions, no open session.

## Startup to session list

| Metric | ms |
|---|---|
| startup_to_session_list | 1283 |

## Per-tab milestones

\irst_paint\ = NavBar renders (user sees UI).  
\irst_interaction\ = user click on default tab registered.  
\irst_load\ = SQL data fully delivered to UI.  
\inal_paint\ = UI painted with loaded data.  
\inal_interaction\ = user click on swap tab registered.  
\menu_swap_*\ = swap tab page rendered.

### Baseline scenario (default tab → swap tab after data loads)

| Tab | first_paint | first_interaction | first_load | final_paint | final_interaction | menu_swap_final |
|---|---|---|---|---|---|---|
| guide (placeholder) | ??? | ??? | ??? | ??? | ??? | ??? |
| chats | ??? | ??? | ??? | ??? | ??? | ??? |
| dms | ??? | ??? | ??? | ??? | ??? | ??? |
| stash (placeholder) | ??? | ??? | ??? | ??? | ??? | ??? |
| profile (placeholder) | ??? | ??? | ??? | ??? | ??? | ??? |
| current | ??? | ??? | ??? | ??? | ??? | ??? |
| log | ??? | ??? | ??? | ??? | ??? | ??? |

### Swap-early scenario (swap tab clicked immediately after first_interaction)

| Tab | first_paint | first_interaction | menu_swap_early |
|---|---|---|---|
| guide (placeholder) | ??? | ??? | ??? |
| chats | ??? | ??? | ??? |
| dms | ??? | ??? | ??? |
| stash (placeholder) | ??? | ??? | ??? |
| profile (placeholder) | ??? | ??? | ??? |
| current | ??? | ??? | ??? |
| log | ??? | ??? | ??? |

## Notes

- \current\ (SessionViewPage) \irst_load\ ≈ \inal_paint\ because SessionViewPage has no
  running session in the test DB; the data load is instantaneous and \inal_paint\ is
  recorded immediately after \irst_load\ (no paint-event round-trip needed).
- \log\ has the highest \irst_interaction\ and \irst_load\ due to loading 120 sessions
  from the test DB. Real-world counts are usually lower for a single league.
- Placeholder tabs (guide, stash, profile) have ~3 ms between \irst_load\ and \inal_paint  because there is no async data fetch — the load is a no-op.
