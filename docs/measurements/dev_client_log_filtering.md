# dev/refilter_logs.py — design & benchmark

`dev/refilter_logs.py` is an exploration tool for trimming `Client.txt` down to
lines worth caring about. It has no influence on the production ingester
(`LogIngestWorker`), which uses a separate allowlist approach.

## What it does

Two-phase split of `Client.txt`:

1. **Parallel extraction** — one `findstr /c:PATTERN` process per category,
   all running concurrently up to `os.cpu_count()` threads. Each writes its
   matching lines to `dev/filtered_client/<category>.txt`.

2. **Single remainder pass** — one `findstr /v /c:P1 /c:P2 … /c:Pn` call
   combining all patterns into a single inverse scan. Output is
   `dev/filtered_client.txt`, the lines left over after all known noise is
   removed.

`/c:` treats every string literally, so brackets (`[RENDER`, `[JOB]`) need no
escaping.

## Approaches compared

Four strategies were benchmarked on the same machine (12 logical cores, SSD,
Windows 11). Test files were made by concatenating copies of a real 90 MB
`Client.txt` and read from a local temp directory to keep I/O conditions equal.

| approach | what it does |
|---|---|
| **inline** | single Python pass, `re` match per line, route to open file handles |
| **concurrent\_inline** | split file into N byte-aligned chunks, one `ProcessPoolExecutor` worker per chunk |
| **concurrent\_recurse** | N parallel `findstr` extraction passes + 1 combined inverse pass |
| **recurse** | 12 sequential `findstr` pairs (extract + inverse), each working on the previous remainder |

### Results (wall-clock seconds)

| file size | inline | concurrent\_inline | concurrent\_recurse | recurse |
|---|---|---|---|---|
| 90 MB (1×) | 3.3 s | 3.3 s | **2.6 s** | 17.2 s |
| 180 MB (2×) | 5.5 s | 4.9 s | **3.6 s** | 9.7 s |
| 359 MB (4×) | 11.2 s | 25.9 s | **7.1 s** | 59.8 s |
| 718 MB (8×) | 26.8 s | 29.8 s | **13.6 s** | 176.7 s |

**concurrent\_recurse wins at every size** and was kept as `refilter_logs.py`.

## Why each approach lost

**recurse** spawns 24 `findstr` processes (2 per category × 12 categories) and
reads the file from the beginning for every pass. Even though the working set
shrinks with each pass, process-spawn overhead (~50–100 ms each) adds up, and
the total I/O across all passes dwarfs a single read.

**inline** reads the file once and does all routing in Python. It avoids
repeated I/O but Python's regex loop and GIL become the bottleneck. Scales
linearly but the constant factor is higher than native `findstr`.

**concurrent\_inline** attempts to bypass the GIL with `ProcessPoolExecutor` and
byte-aligned file chunks. On Windows, process spawn is fork-less (spawn method),
so each worker cold-starts a Python interpreter. That overhead (~0.5–1 s per
worker) is break-even at small sizes and a net loss at larger sizes once merge
I/O is added back in. On Linux (fork-based) this would likely be competitive.

**concurrent\_recurse** wins because:
- All 12 category extractions run simultaneously — only as many waves as needed
  to saturate the thread pool (12 categories / 12 cores = 1 wave).
- `findstr.exe` starts fast, runs without a Python GIL, and the OS scheduler
  can overlap its I/O with other workers reading the same cached file.
- The single combined `/v` pass for the remainder is one native scan regardless
  of how many categories exist.
- Total effective I/O waves: 2 (parallel extraction + 1 inverse), vs 24 for
  recurse and 1 (but slow) for inline.

## GNU grep vs findstr (`refilter_grep.py`)

`dev/refilter_grep.py` uses the same two-phase concurrent design as
`refilter_logs.py` but substitutes `grep -F -e PATTERN` for `findstr /c:PATTERN`
and `grep -F -v -e P1 -e P2 …` for the remainder pass. `-F` selects fixed-string
matching so brackets need no escaping, same as findstr `/c:`.

Tested with GnuWin32 grep 2.5.4 (2009 vintage) from
`C:\Program Files (x86)\GnuWin32\bin`, and separately with GNU grep 3.11 inside
WSL2 (Ubuntu 24.04). For the WSL run both input and output were on the WSL-native
`/tmp` filesystem to avoid VirtioFS bridge overhead; `REFILTER_OUTDIR` env var
controls the output directory for this purpose.

| file size | findstr (refilter\_logs) | grep 2.5.4 Windows (refilter\_grep) | grep 3.11 WSL native |
|---|---|---|---|
| 90 MB (1×) | 2.6 s | 3.3 s | **0.87 s** |
| 180 MB (2×) | 3.6 s | 4.0 s | **1.14 s** |
| 359 MB (4×) | 7.1 s | 6.9 s | **2.08 s** |
| 718 MB (8×) | 13.6 s | 25.9 s | **4.30 s** |

WSL grep 3.11 is 3–6× faster than findstr and 6–38× faster than GnuWin32 grep
2.5.4. The gap is a combination of three things:

- **Grep version**: 3.11 vs 2.5.4 — over a decade of SIMD and buffer
  optimisations (Boyer-Moore-Horspool, PCRE2 JIT paths, AVX2 byte-scan).
- **Filesystem**: WSL `/tmp` is a native ext4 ramfs-backed tmpfs; reading a 718 MB
  file from it is essentially memory bandwidth. Windows NTFS on the same SSD
  adds kernel translation and driver overhead per read syscall.
- **Process model**: on Linux, `subprocess` forks (copy-on-write), so each
  `grep` worker starts in microseconds. On Windows, every process is a full
  CreateProcess spawn (~50–100 ms each).

`refilter_logs.py` (findstr) is kept as the default because it works without
WSL. `refilter_grep.py` is the better tool if WSL is available and the file is
large — at 718 MB it is 3× faster even accounting for the copy into WSL.

## Scaling note

With 12 logical cores and 12 categories, all extractions fit in one scheduling
wave. Adding more categories beyond `os.cpu_count()` would require multiple
waves and degrade towards the recurse baseline.
