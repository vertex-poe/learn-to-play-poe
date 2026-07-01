# Client.txt Ingest — Measurements

**Date**: 2026-06-22  
**Source**: `[ingest]` lines in `l2p-poe.log`, single real-world run

---

## File

| Property | Value |
|---|---|
| File | `Client.txt` |
| Size | 96.6 MB (96,597,230 bytes) |
| Total lines | 930,092 |
| Lines per KB | ~9.9 (103.8 bytes/line avg) |
| Area visits recorded | 39,614 |

---

## Chunk size

The ingest processes 10,000 matched lines per SQLite transaction (lines that don't match the timestamp regex are not counted).

| Property | Value |
|---|---|
| Lines per chunk | 10,000 |
| Average chunk size | **0.96 MB** |
| Implied avg line length | ~101 bytes |
| Chunks to cover 96.6 MB | ~99 |

---

## Throughput

The app runs two passes: a cold non-live pass (cancelled early once the UI is ready) then a live pass from the last safe commit offset.

| Phase | Bytes processed | Wall time | Rate |
|---|---|---|---|
| Cold pass (non-live, cancelled at 6 chunks) | ~5.8 MB | ~1.3 s | ~4.5 MB/s |
| Live pass (offset 6,048,241 → EOF) | ~90.5 MB | ~36.4 s | ~2.5 MB/s |

The live pass rate is lower than the cold pass because periodic SQLite WAL flush pauses dominate overall time:

| Condition | Per-chunk time | Parse rate |
|---|---|---|
| Fast chunks (commit_ms < 10 ms) | 170–215 ms | 4.5–5.6 MB/s |
| Slow chunks (WAL flush) | 800–1100 ms extra | pulls average to ~2.5 MB/s |

WAL flush pauses (~750–1100 ms) occur roughly every 5–6 chunks throughout the live pass.

---

## Raw log (abridged)

```
[ingest] start size=96597230 offset=0 live=false
[ingest] commit  1% pos=  987788/96597230 visits=  735 commit_ms=  2 elapsed_ms=  220
[ingest] commit  2% pos= 2002202/96597230 visits= 1365 commit_ms=  2 elapsed_ms=  438
[ingest] commit  3% pos= 3020855/96597318 visits= 2012 commit_ms=  3 elapsed_ms=  632
[ingest] commit  4% pos= 4048157/96597318 visits= 2490 commit_ms=  2 elapsed_ms=  804
[ingest] commit  5% pos= 5059216/96597406 visits= 3097 commit_ms=  3 elapsed_ms= 1005
[ingest] commit  6% pos= 6049215/96597406 visits= 3799 commit_ms=108 elapsed_ms= 1310
[ingest] final commit_ms=272
[ingest] done total_ms=1583 visits=3799 avg_chunk_mb="0.96"

[ingest] start size=96597406 offset=6048241 live=true
...
[ingest] commit 99% pos=96409572/96598528 visits=39553 commit_ms=  3 elapsed_ms=36369
[ingest] eof-commit 100% pos=96598528/96598528 visits=39612 commit_ms=1 elapsed_ms=36432
```
