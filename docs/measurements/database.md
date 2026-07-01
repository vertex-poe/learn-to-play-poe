# Database Engine: Preliminary Evaluation

**Date**: 2026-06-19  
**Status**: Preliminary — actual benchmarks pending if this becomes a bottleneck

---

## Investigation

Path of Exile writes session events to `client.txt` — a single append-only log file that can reach hundreds of megabytes and millions of lines over a long play session. Before committing to an engine, we surveyed the available C++ embeddable options against two workloads:

- **Bulk ingest** — loading the full file (or tail-appending new lines) as fast as possible
- **Filtered reads** — low-latency queries filtering by timestamp range, event type, player name, and similar multi-attribute combinations

Additional constraints: no server process, C++ API linkable from CMake, SQL or SQL-like interface preferred (to avoid hand-rolling a query planner).

---

## Integration Options (SQLite3 in this project)

Three ways to bring SQLite3 into a C++/CMake/Qt6 project were considered:

**Qt6's bundled SQLite (`Qt6::Sql` + `QSQLITE` driver)**  
Qt ships SQLite internally as part of its SQL module. No new dependency, but access is through `QSqlDatabase` / `QSqlQuery` — Qt's abstraction layer. This loses direct control over `sqlite3_prepare_v2`, `sqlite3_bind_*`, and pragma sequencing, which are the critical path for fast batched ingest. Eliminated for this use case.

**vcpkg `sqlite3` package — chosen**  
Consistent with the project's existing approach (`tomlplusplus` is already pulled via vcpkg). Provides the direct C API (`sqlite3.h`). One line in `vcpkg.json`:
```json
"sqlite3"
```
CMake usage:
```cmake
find_package(unofficial-sqlite3 CONFIG REQUIRED)
target_link_libraries(l2p-poe PRIVATE unofficial::sqlite3::sqlite3)
```
Installed version: `3.53.2` (with `json1` extension). See [vcpkg.json](../../vcpkg.json) and [src/CMakeLists.txt](../../src/CMakeLists.txt).

**Vendored amalgamation**  
Download `sqlite3.c` + `sqlite3.h` (two files, ~260 KB) from sqlite.org and add via `add_library`. Zero external dependency, fully self-contained. Valid alternative if vcpkg ever becomes inconvenient, but less consistent with the project's current dependency management.

---

## Engines Surveyed

### SQLite3 (WAL mode)

Single-file C amalgamation, public domain. The standard embeddable SQL engine.

**Ingest**: Excellent when done correctly. Transaction batching is the dominant factor — one transaction + one reused prepared statement reaches millions of rows/sec. WAL mode (`PRAGMA journal_mode=WAL`) allows concurrent reads during the write, important for incremental tail-appends while the UI queries. Key pragmas for bulk load:

```sql
PRAGMA journal_mode=WAL;
PRAGMA synchronous=NORMAL;   -- or OFF during initial import
PRAGMA temp_store=MEMORY;
PRAGMA cache_size=-65536;    -- 64 MB page cache
```

**Reads**: Good for selective range queries with a composite index (e.g. `(event_type, timestamp)`). Weak on full-table analytical aggregates — executes one row at a time through its bytecode VM. Benchmarks typically show a 10–100× gap vs. DuckDB on pure aggregate workloads over 10M+ rows.

**Integration**: Vendor the amalgamation as `add_library(sqlite3 sqlite3.c)` — zero external deps, no DLL to ship, clean MSVC build.

**License**: Public domain. **Maintenance**: Best-in-class.

---

### DuckDB (columnar OLAP)

Embedded analytical database with columnar storage and vectorized execution (~1024-value batches). Full SQL including window functions and CTEs.

**Ingest**: Designed for bulk load. `COPY table FROM 'file.csv'` uses a multi-threaded CSV parser. The C++ Appender API is efficient for programmatic inserts. Can query CSV/Parquet files directly without a prior load step. Columnar compression produces files ~3× smaller than SQLite for typical log data.

**Reads**: 10–100× faster than SQLite on filtered scans and aggregates over millions of rows, via columnar layout, vectorized batch execution, and automatic min/max zone-map pruning (timestamp-range filters skip irrelevant row groups with no manual index required).

**Integration**: Native C++ API (`duckdb.hpp`) + stable C API. `find_package(DuckDB)` in CMake. Static lib (`duckdb_static`) requires `/bigobj` and has reported MSVC 2022 issues ([duckdb#9257](https://github.com/duckdb/duckdb/issues/9257), [duckdb#10683](https://github.com/duckdb/duckdb/issues/10683)). Prebuilt `duckdb.dll` + import lib is the practical Windows path.

**License**: MIT. **Maintenance**: Very active; LTS from v1.4.0.

---

### Not Evaluated: Turso (libsql)

Turso's `libsql` is a fork/rewrite of SQLite3 in Rust with additional features (built-in replication, embedded replicas, vector search extensions). Not evaluated — the project has no sync or replication requirement, and the SQLite3 C API is already well-understood here. May be worth a look if any of those extra capabilities become relevant.

---

### Eliminated: RocksDB, LevelDB, LMDB, HDF5

All four were eliminated early because they lack a query layer for multi-attribute filtering:

| Engine | Reason eliminated |
|---|---|
| RocksDB / LevelDB | LSM key-value — no SQL, no secondary indexes; multi-attribute filters require hand-coded composite keys and manual merge logic |
| LMDB | mmap B+tree KV — fastest raw read latency of the group, but same absent query layer |
| HDF5 | Columnar array format for scientific numerics — no query engine; awkward fit for variable-length text log records |

---

## Comparison: SQLite3 vs DuckDB

| | SQLite3 (WAL) | DuckDB |
|---|---|---|
| Bulk ingest | Excellent (txn-batched, prepared stmts) | Excellent (COPY, Appender, CSV reader) |
| Selective range filter | Fast with composite index | Fast (zone-map pruning, no index needed) |
| Full-table aggregate | Slow at scale | 10–100× faster (columnar + vectorized) |
| File size | Baseline | ~3× smaller (columnar compression) |
| CMake integration | Trivial — vendor amalgamation, zero deps | Heavier — MSVC `/bigobj`, needs DLL |
| License | Public domain | MIT |
| Maintenance | Excellent | Very active, LTS |

---

## Preliminary Findings

**SQLite3 in WAL mode** satisfies every hard requirement and has zero Windows/Qt integration friction. It is the right starting point:

- Trivial to vendor; no deployment artifact beyond the amalgamation
- Fast ingest when done with batching + prepared statements + WAL pragmas
- Selective range queries are fast with a composite index on `(event_type, timestamp)`
- Weak spot: full-table aggregates (session-history statistics, time-bucket histograms over 10M+ rows) will be noticeably slower

**DuckDB** is the natural upgrade path if analytical aggregate reads become a bottleneck. Its columnar architecture closes the aggregate gap at the cost of a larger build and a `duckdb.dll` deployment artifact.

A thin data-access interface (abstract `LogStore` with `ingest()` / `query()`) keeps the engine swappable without touching the rest of the app.

---

## If Benchmarks Are Needed Later

Workloads worth measuring if ingestion or query latency becomes a problem:

1. **Bulk ingest time** — time to load a representative `client.txt` (e.g. 500 MB, ~5M lines) from cold storage into each engine
2. **Incremental tail-append throughput** — lines/sec for appending new events while a reader query is in flight
3. **Selective range scan** — filter `WHERE event_type = 'death' AND timestamp BETWEEN t1 AND t2` over 5M rows; measure wall time
4. **Full-table aggregate** — `SELECT event_type, COUNT(*) FROM log GROUP BY event_type` over 5M rows; measure wall time

These four cover the two failure modes (slow startup ingest, laggy UI queries) and distinguish the workloads where SQLite and DuckDB differ most.

---

## Sources

- [DuckDB vs SQLite Benchmarks — Galaxy](https://www.getgalaxy.io/learn/glossary/duckdb-vs-sqlite-benchmarks)
- [We Benchmarked DuckDB, SQLite, and Pandas on 1M Rows — KDnuggets](https://www.kdnuggets.com/we-benchmarked-duckdb-sqlite-and-pandas-on-1m-rows-heres-what-happened)
- [DuckDB vs SQLite — MotherDuck](https://motherduck.com/learn/duckdb-vs-sqlite-databases/)
- [SQLite and DuckDB for analytics workloads — marending.dev](https://marending.dev/notes/sqlite-vs-duckdb/)
- [Improving bulk insert speed in SQLite — PDQ](https://www.pdq.com/blog/improving-bulk-insert-speed-in-sqlite-a-comparison-of-transactions/)
- [DuckDB MSVC static link issues — duckdb#9257](https://github.com/duckdb/duckdb/issues/9257)
- [DuckDB /bigobj — duckdb#10683](https://github.com/duckdb/duckdb/issues/10683)
- [DuckDB 1.4.0 LTS announcement](https://duckdb.org/2025/09/16/announcing-duckdb-140)
- [Benchmarking LevelDB vs RocksDB vs LMDB — InfluxData](https://www.influxdata.com/blog/benchmarking-leveldb-vs-rocksdb-vs-hyperleveldb-vs-lmdb-performance-for-influxdb/)
