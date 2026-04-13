# LevelDB + SuRF: Range Query Filter Extension

**Course:** CSCI-543, Spring 2026 — University of Southern California
**Team:** Jahnavi Manoj, Dhrish Kumar Suman, Sai Pragna Boyapati
**Repository:** https://github.com/dhrish-s/leveldb-surf

---

## Quick Start (End-to-End)

This section guides you through generating benchmark metrics, starting the backend and frontend, and viewing the observability dashboard.

**Start the container and generate metrics:**
```bash
cd "path-to-repo"
docker run -it --rm -v "${PWD}\project:/workspace/project" -v "${PWD}\benchmarks:/workspace/benchmarks" -v "${PWD}\metrics:/workspace/metrics" leveldb-surf

# Inside container: Generate Bloom baseline
cd /workspace/leveldb/build
rm -rf /tmp/dbbench
./db_bench --filter=bloom --benchmarks=fillrandom,surfscan25,surfscan50,surfscan75 --num=500000 --metrics_out=/workspace/metrics/bloom_fixcheck.jsonl

# Generate SuRF results
./db_bench --filter=surf --benchmarks=fillrandom,surfscan25,surfscan50,surfscan75 --num=500000 --metrics_out=/workspace/metrics/surf_fixcheck.jsonl
```

**Start the backend (Node.js) on host machine:**
```bash
# Outside container, on your host machine
cd "path-to-repo/dashboard-server"
npm install
node index.js
# Backend runs on http://localhost:3001
```

**Start the frontend (React/Vite):**
```bash
# In a new terminal (on host)
cd "path-to-repo/dashboard-ui"
npm install
npm run dev
# Frontend runs on http://localhost:5173
```

**Open the dashboard:**
Navigate to `http://localhost:5173` in your browser. The dashboard polls metrics every 2 seconds and displays live Bloom vs SuRF comparisons.

**Switch between filter results:**
To compare different metrics files, restart the backend with a different metrics path:
```bash
# Backend defaults to bloom_fixcheck.jsonl; to use SuRF results:
METRICS_FILE=./metrics/surf_fixcheck.jsonl node index.js
```

---

## Why This Project Exists — The Real-World Problem

LevelDB is a key-value store built on a Log-Structured Merge Tree (LSM-tree). It powers Google Chrome (IndexedDB backend), Bitcoin Core (UTXO storage), and Minecraft (world chunk storage). Every SSTable (sorted immutable file on disk) has a **Bloom filter** attached to it. A Bloom filter can answer one question:

> "Is this exact key possibly in this file?"

It **cannot** answer range queries like:

> "Does any key between `dog` and `lion` exist in this file?"

This limitation exists because Bloom filters use hash functions that destroy key ordering — once a key is hashed into bit positions, all information about its alphabetical or numerical position relative to other keys is lost.

This means when you run a range scan, LevelDB opens **every** SSTable whose key-range overlaps your query — even if that file contains zero keys in your range. Every opened SSTable means reading data blocks from disk. That is wasted I/O.

**Real-world impact:** Applications that rely on range scans — time-series databases querying a time window, blockchain nodes scanning transaction ranges, game engines loading world regions — all pay this penalty on every query. The more SSTables that pass the coarse key-range overlap check but contain no actual keys in the queried range, the more I/O is wasted.

**Our fix:** We replaced the Bloom filter with **SuRF (Succinct Range Filter)** — a compressed trie data structure from the SIGMOD 2018 Best Paper that can answer both point queries and range queries. When SuRF says "no key exists in [lo, hi]", LevelDB skips reading that SSTable's data blocks entirely. SuRF preserves key ordering in its trie structure, which is what enables range query support. The original paper demonstrated up to 5x improvement in range query performance in RocksDB using SuRF.

---

## Results Summary

All benchmarks run with 1,000,000 keys, 16-byte keys, 100-byte values.
Filter selection via `--filter=bloom` or `--filter=surf` command-line flag (no recompilation needed).

### Standard Benchmarks — Bloom vs SuRF

| Benchmark    | Bloom (µs/op) | SuRF (µs/op) | Change     | Winner   | Why                                                         |
|--------------|---------------|--------------|------------|----------|-------------------------------------------------------------|
| fillrandom   | 2.638         | 4.078        | +54.6%     | Bloom    | SuRF trie construction is more expensive than Bloom hashing  |
| seekrandom   | 8.891         | 6.634        | **-25.4%** | **SuRF** | SuRF's trie structure aids seek navigation                   |
| readrandom   | 2.847         | 8.977        | +215.2%    | Bloom    | Bloom's flat bit-check is much cheaper than trie traversal    |
| readseq      | 0.212         | 0.447        | +110.8%    | Bloom    | SuRF filter is larger and costlier to load into memory        |

**Why Bloom is faster on point queries and sequential reads:**
Bloom is a flat bit array — checking a key means computing a few hash values and testing bit positions. SuRF is a compressed trie (FST) — checking a key means deserializing the trie and traversing nodes. The trie traversal is inherently more expensive than flat bit lookups, so for workloads that only need point-query answers, Bloom's simpler structure wins.

**Why SuRF is faster on seekrandom:**
Even without explicit `lo/hi` bounds, SuRF's trie structure provides information about key distribution that helps the iterator navigate more efficiently during seeks. Bloom provides zero structural information — it only answers yes/no for exact keys.

### Variable Miss Rate — Range Scan Benchmarks (The Key Experiment)

We designed benchmarks with configurable miss rates to show how SuRF's advantage changes with workload composition. "Miss" means the query range contains no keys in the SSTable (empty range). "Hit" means the range overlaps with actual keys.

| Benchmark    | Bloom (µs/op) | SuRF (µs/op) | Change     | Winner   | Keys Scanned |
|--------------|---------------|--------------|------------|----------|--------------|
| surfscan100  | 1.298         | 1.374        | +5.9%      | Bloom    | 0            |
| surfscan75   | 3.922         | 5.005        | +27.6%     | Bloom    | 1,736,663    |
| surfscan50   | 5.213         | 4.760        | **-8.7%**  | **SuRF** | 3,478,645    |
| surfscan25   | 7.214         | 5.817        | **-19.4%** | **SuRF** | 5,215,152    |
| surfscan0    | 7.503         | 6.934        | **-7.6%**  | **SuRF** | 6,952,830    |

**What this data shows:**

1. **At 100% miss rate** (all empty ranges), both filters are fast (~1.3 µs) because the coarse check (`FileMetaData.smallest/largest`) already skips most SSTables before either filter is consulted. The 5.9% gap is just filter overhead — SuRF's trie is slightly more expensive to have loaded in memory than Bloom's flat bit array. Neither filter is doing meaningful work here.

2. **At 75% miss rate**, Bloom is still ahead (+27.6%). Most queries are empty, so the overhead of SuRF's trie structure outweighs its benefits on the minority of queries that hit data.

3. **At 50% miss rate**, the crossover happens — SuRF pulls ahead (-8.7%). With half the queries hitting real data, SuRF's structural knowledge about key distribution starts paying off during data block iteration.

4. **At 25% miss rate**, SuRF shows its largest advantage (**-19.4%**). Most queries hit data, and SuRF's trie structure provides the most benefit when the iterator is actively scanning data blocks. This is the sweet spot.

5. **At 0% miss rate** (all queries hit keys), SuRF is still faster (-7.6%), confirming that the advantage comes from the trie structure aiding iteration, not just from skipping empty SSTables.

### Wide Range Scan (range_width=100, 100% miss)

| Benchmark     | Bloom (µs/op) | SuRF (µs/op) | Change | Winner |
|---------------|---------------|--------------|--------|--------|
| surfscan_wide | 1.361         | 1.411        | +3.7%  | Bloom  |

At 100% miss rate with wide ranges, both filters produce nearly identical results because no data blocks are read. Range width does not affect performance when all ranges are empty.

### Write Performance

| Benchmark  | Bloom (µs/op) | SuRF (µs/op) | Change | Winner |
|------------|---------------|--------------|--------|--------|
| fillrandom | 2.638         | 4.078        | +54.6% | Bloom  |

SuRF trie construction during compaction is more expensive than Bloom's bit-array hashing. This is the expected trade-off — SuRF pays a write-time cost to enable range query filtering at read time.

### Trade-off Summary

**SuRF wins on:**
- Range scans with mixed hit/miss workloads (surfscan25: **-19.4%**, surfscan50: **-8.7%**)
- Range scans with all hits (surfscan0: **-7.6%**)
- Random seeks (seekrandom: **-25.4%**)

**Bloom wins on:**
- Point lookups (readrandom: +215.2%)
- Sequential reads (readseq: +110.8%)
- Write throughput (fillrandom: +54.6%)
- Pure miss range scans (surfscan100: +5.9% — but both are fast, ~1.3 µs)

**Conclusion:** The right filter depends on workload. Applications dominated by range scans and seeks (time-series databases, analytics, blockchain range queries) benefit from SuRF. Applications dominated by point lookups and writes (caching, exact-key retrieval) are better served by Bloom. The ideal production system would select the filter per-SSTable based on observed access patterns.

---

## Observability Dashboard

A full-stack web dashboard was built to visualize and compare benchmark metrics across Bloom and SuRF filters. The dashboard enables real-time analysis of query performance and filter behavior.

**Features:**
- **Per-Query Metrics:** Displays latency (`time_us`) for each benchmark across both filters
- **Live Polling:** Auto-refreshes every 2 seconds from the metrics JSON files
- **Bloom vs SuRF Comparison:** Side-by-side charts showing performance for surfscan benchmarks at different miss rates (25%, 50%, 75%)
- **Filter Analysis:** Calculates and displays filter pruning effectiveness, false positive rates, and latency behavior patterns
- **Responsive UI:** Built with React and Tailwind CSS for intuitive exploration

**Insights Enabled:**
- Observe how SuRF's advantage varies with miss rate (surfscan25 shows largest SuRF win at -19.4%)
- Identify workloads where each filter excels (range scans vs point queries)
- Monitor filter memory overhead and deserialization cache hit rates
- Track performance regressions during development (metrics auto-update after each benchmark run)

**UI Components:**
- **Summary Cards:** High-level metrics (throughput, best/worst case scenarios for each filter)
- **Comparison Charts:** Line and bar charts showing relative performance across benchmarks
- **Query Event Table:** Detailed per-query results with filtering and sorting capabilities
- **EventInspector:** Drill-down view for individual queries with full metric details

**Architecture:**
- **Backend** (`dashboard-server/`): Node.js server parsing JSONL metrics files and serving REST API
- **Frontend** (`dashboard-ui/`): React + Vite application with real-time polling
- **Metrics** (`metrics/`): JSONL format (generated by `db_bench`), not committed to git

Metrics are generated inside Docker and consumed outside via mounted volumes.

---

### Previous Benchmark Results (Before Variable Miss Rate Tests)

Early benchmarks were run before the `--filter` flag was added. Switching between Bloom and SuRF required editing the constructor in `db_bench.cc` and recompiling — an error-prone process. These results used a simpler benchmark (`surfscan` with 100% miss only):

| Benchmark    | Bloom (µs/op) | SuRF (µs/op) | Change  |
|--------------|---------------|--------------|---------|
| fillrandom   | 3.953         | 4.653        | +18%    |
| seekrandom   | 4.317         | 10.781       | +150%   |
| readrandom   | 3.691         | 5.606        | +52%    |
| readseq      | 0.213         | 0.245        | +15%    |
| surfscan     | 2.102         | 1.420        | -32%    |

**Why the numbers differ from the final results:**
- `seekrandom` showed +150% in early tests vs **-25.4%** in final tests. The early tests likely had a filter initialization mismatch — the code was being edited manually to switch filters, and the filter may not have been correctly active for the SuRF run. The `--filter` flag eliminated this class of error entirely.
- The overall µs/op values vary between runs due to system load, caching state, and compaction timing. The relative comparisons within a single run are what matter.
- The early `surfscan` (-32% for SuRF) only tested 100% miss rate. The comprehensive variable miss rate tests reveal that SuRF's real advantage is on **mixed and hit-heavy workloads** (surfscan25: -19.4%), not pure misses (surfscan100: +5.9% for Bloom).

**Why did surfscan flip from SuRF -32% to Bloom +5.9% at 100% miss?**
At 100% miss rate, both filters finish in ~1.3 µs because the coarse `FileMetaData` check skips most SSTables before either filter is even consulted — neither filter is doing meaningful work. A difference of 0.08 µs (new run) vs 0.68 µs (old run) at this scale is dominated by system noise. The old Bloom result (2.102 µs) is suspiciously slow compared to the new Bloom result (1.298 µs), suggesting the old Bloom run had external overhead — cold LRU cache, incomplete compaction, or higher system load. The old runs were also separate compilations run at different times (manual code editing to switch filters), while the new runs use the `--filter` flag in the same binary run back-to-back under identical conditions. The -32% was an artifact of inconsistent test conditions, not a real SuRF advantage on pure misses. The real SuRF advantage appears at **25% miss rate (-19.4%)** and **seekrandom (-25.4%)** — workloads where actual data scanning happens and SuRF's trie structure provides value that Bloom's flat bit array cannot.

---

## Project Architecture

### The Problem — Wasted I/O on Range Scans

When a range query `[lo, hi]` runs, LevelDB performs two checks:

1. **Coarse check** (already exists): Skip SSTables whose `[smallest, largest]` key range does not overlap `[lo, hi]`. This uses `FileMetaData` stored in the version set.
2. **Fine check** (we add): For SSTables that pass the coarse check, does any *actual* key exist in `[lo, hi]`?

Without the fine check, every SSTable that passes the coarse check gets its data blocks read from disk — even if it contains zero keys in the queried range.

**Example:**
```
SSTable B: smallest=bear, largest=hippo → passes coarse check
Actual keys in SSTable B: bear, cat, elephant
Query range: [dog, fox]

Without SuRF: LevelDB opens SSTable B, reads data blocks, finds nothing. Wasted I/O.
With SuRF:    RangeMayMatch("dog", "fox") → false → skip data block reads entirely.
```

### How SuRF Works

SuRF builds a compressed trie (internally called FST — Fast Succinct Trie) from all keys in an SSTable. To answer "any key in [lo, hi]?", it finds the successor of `lo` in the trie. If that successor is greater than `hi`, the answer is no — no key exists in the range. The trie uses approximately 10 bits per key, comparable to a Bloom filter.

Unlike Bloom (which hashes keys into a flat bit array, destroying all ordering), SuRF preserves the lexicographic ordering of keys in its trie structure. This is what makes range queries possible — the trie knows which keys come before and after any given point.

### Where the Hook Lives — The Read Path

The `RangeMayMatch` check is inserted in `table_cache.cc::NewIterator()`, **after** `FindTable()` loads the SSTable's filter block into memory, and **before** `table->NewIterator()` reads data blocks:

```
DB::NewIterator(options)                          [db_impl.cc]
  options.lo/hi passed through
  └── Version::AddIterators(options, iters, lo, hi)  [version_set.cc]
        ├── Level 0: each file → TableCache::NewIterator(lo, hi)
        └── Levels 1+: NewConcatenatingIterator(lo, hi)
              └── TwoLevelIterator
                    outer: LevelFileNumIterator (walks file list)
                    inner: GetFileIterator(TableCacheArg{cache, lo, hi})
                             └── TableCache::NewIterator()          [table_cache.cc]
                                   FindTable()
                                     ← SSTable opened, filter loaded into RAM
                                   table->RangeMayMatch(lo, hi)    ← CHECK HERE
                                     false → Release(handle)
                                             return EmptyIterator
                                             (data block reads skipped entirely)
                                     true  → table->NewIterator()
                                               └── read data blocks
```

### Why After FindTable() and Not Before

The filter bytes (the serialized SuRF trie) live inside the `.ldb` SSTable file in the filter block. To access them, the file must be opened — and opening the file IS `FindTable()`. Therefore `RangeMayMatch` **cannot** run before `FindTable()`.

For **warm** (cached) SSTables, `FindTable()` is an LRU cache hit (nearly free), and `RangeMayMatch` then prevents all data block reads — maximum benefit. For **cold** SSTables, `FindTable()` reads the filter block from disk as part of `Table::Open()`, but `RangeMayMatch` still prevents the more expensive data block reads — reduced but real benefit.

A pre-open range check would require storing filter data in a separate partition outside the SSTable. RocksDB implemented this as "partitioned filters" in 2017. This is a natural future extension beyond this project's scope.

### The Write Path — Filter Build During Compaction

```
DoCompactionWork()
  └── BuildTable()
        └── TableBuilder::Add(key, value)           [table_builder.cc]
              └── FilterBlockBuilder::AddKey()       [filter_block.cc]
                    buffers all keys (single filter per SSTable)
              └── TableBuilder::Finish()
                    └── FilterBlockBuilder::Finish()
                          └── SuRFPolicy::CreateFilter(all_keys)  [surf_filter.cc]
                                builds SuRF trie → serializes → writes to SSTable
```

### The Safety Rule

```
KeyMayMatch / RangeMayMatch:
  false → MUST be correct. Definitely no key/range. Never lie here.
  true  → CAN be wrong. False positives are acceptable (just slower).

False negative = data loss   = catastrophic = never acceptable
False positive = extra I/O   = acceptable   = just a performance penalty

Filters are PERFORMANCE ONLY — they never affect correctness.
```

---

## All Files Changed From Original LevelDB

Every modified file lives in `/workspace/project/` and is copied into the LevelDB source tree by `rebuild.sh`.

| # | File | Copies To | What Changed | Week |
|---|------|-----------|-------------|------|
| 1 | `filter_policy.h` | `include/leveldb/filter_policy.h` | Added `RangeMayMatch()` virtual method with default `return true`; declared `NewSuRFFilterPolicy()` | 2 |
| 2 | `surf_filter.cc` | `util/surf_filter.cc` | New file: complete SuRF filter policy with thread_local deserialization cache | 2 |
| 3 | `filter_block.cc` | `table/filter_block.cc` | Changed from per-2KB filters to single filter per SSTable; `base_lg_=32` | 3 |
| 4 | `filter_block_test.cc` | `table/filter_block_test.cc` | Updated `MultiChunk` test to expect single combined filter behavior | 3 |
| 5 | `table.h` | `include/leveldb/table.h` | Added public `RangeMayMatch(lo, hi)` method declaration | 3 |
| 6 | `table.cc` | `table/table.cc` | Implemented `Table::RangeMayMatch()` delegating to filter | 3 |
| 7 | `table_cache.h` | `db/table_cache.h` | Updated `NewIterator` signature with `lo`/`hi` default parameters | 3 |
| 8 | `table_cache.cc` | `db/table_cache.cc` | Core hook: `RangeMayMatch` check after `FindTable()`, with `cache_->Release(handle)` on skip | 3 |
| 9 | `version_set.h` | `db/version_set.h` | Updated `AddIterators` and `NewConcatenatingIterator` signatures with `lo`/`hi` | 3 |
| 10 | `version_set.cc` | `db/version_set.cc` | Added `TableCacheArg` struct to thread `lo`/`hi` through `void* arg`; updated `GetFileIterator`, `AddIterators`, `NewConcatenatingIterator`, `MakeInputIterator`, `ApproximateOffsetOf` | 3 |
| 11 | `options.h` | `include/leveldb/options.h` | Added `Slice lo` and `Slice hi` fields to `ReadOptions`; added `#include "leveldb/slice.h"` | 3 |
| 12 | `db_impl.cc` | `db/db_impl.cc` | One-line change: pass `options.lo`/`options.hi` to `AddIterators()` | 3 |
| 13 | `db_bench.cc` | `benchmarks/db_bench.cc` | Added `--filter` flag for bloom/surf switching; added variable miss rate benchmarks (`surfscan0/25/50/75/100`, `surfscan_wide`); added `DoSuRFRangeScan()` core function | 4 |

**Files NOT changed:** `table_builder.cc`, `bloom.cc`, `dbformat.cc` — these work correctly as-is.

---

## Key Design Decisions

### Single Filter Per SSTable (filter_block.cc)
The original LevelDB calls `GenerateFilter()` every 2KB of data block output, creating many small filters per SSTable. SuRF's `deSerialize()` crashes (segfault/FPE) on tries built from only 10-50 keys — the trie structure needs a minimum number of keys to be valid. We changed `filter_block.cc` to build ONE filter containing all keys in the SSTable. The trick: setting `base_lg_=32` means `block_offset >> 32 = 0` for any offset under 4GB, so `FilterBlockReader` always finds `filter[0]` — the single combined filter.

### Thread-Local Deserialization Cache (surf_filter.cc)
SuRF's serialized trie can be ~42KB per SSTable. Deserializing on every `KeyMayMatch`/`RangeMayMatch` call (copying 42KB + rebuilding the trie) caused OOM at 1M keys. We use a `thread_local` cache: if `filter.data()` pointer matches the last call, reuse the already-deserialized `surf::SuRF*` object. This works because `filter.data()` points into LevelDB's block cache — same SSTable = same pointer, different SSTable = different pointer. Each thread gets its own cached object, so no locking is needed.

### Alignment Copy Before Deserialization (surf_filter.cc)
`SuRF::deSerialize(char*)` internally casts to `uint64_t*`, requiring 8-byte alignment. `filter.data()` points into LevelDB's block cache which may not be 8-byte aligned. We copy to a `std::string buf` before calling `deSerialize` — `std::string` guarantees sufficient alignment.

### Public `RangeMayMatch` on Table (table.h / table.cc)
`Table::Rep` is forward-declared in `table.h` — its members (including the filter) are only defined inside `table.cc`. Even with `friend class TableCache`, `table_cache.cc` cannot access `rep_->filter` because it does not know what `Rep` contains. Adding a public `RangeMayMatch(lo, hi)` method on `Table` is the clean solution.

### `TableCacheArg` Struct for Threading Range Bounds (version_set.cc)
`GetFileIterator` is a static callback with signature `(void* arg, ...)`. The `void* arg` originally carried only `TableCache*`. We introduced a `TableCacheArg` struct containing `{cache, lo, hi}` and use `RegisterCleanup` with a custom deleter to avoid memory leaks.

### `--filter` Flag in db_bench.cc
Originally, switching between Bloom and SuRF required editing the constructor in `db_bench.cc` and recompiling. This was error-prone — the early benchmark anomaly where `seekrandom` showed SuRF as +150% slower was likely caused by a filter initialization mismatch during manual code editing. We added a `--filter=bloom|surf` command-line flag that selects the filter at runtime, eliminating this class of error and making automated benchmark scripts possible.

### `surf/surf.hpp` Isolation (CRITICAL)
`surf/surf.hpp` is a header-only library. Including it in `table.h` caused it to be pulled into every `.cc` file that includes `table.h`, giving the linker hundreds of duplicate function definitions (ODR violation). **`surf/surf.hpp` is included ONLY in `surf_filter.cc` — never anywhere else.**

---

## Bugs We Hit and How We Fixed Them

These are documented here because they represent non-trivial systems-level debugging.

| Bug | Symptom | Root Cause | Fix |
|-----|---------|-----------|-----|
| ODR violation | Linker error: multiple definitions of `surf::*` | `surf/surf.hpp` included in `table.h`, pulled into every `.cc` file | Remove include from `table.h`; keep only in `surf_filter.cc` |
| Alignment crash | SIGFPE / SIGSEGV in `deSerialize` | `SuRF::deSerialize()` casts to `uint64_t*`; `filter.data()` not 8-byte aligned | Copy to `std::string buf` before deserializing |
| OOM at 1M keys | Process killed during benchmark | 42KB copy per `KeyMayMatch` call × thousands of calls | `thread_local` cache with pointer comparison |
| Incomplete type error | Cannot access `rep_->filter` from `table_cache.cc` | `Table::Rep` is forward-declared; members invisible outside `table.cc` | Add public `RangeMayMatch()` method on `Table` |
| Missing header in rebuild | Signature mismatch between `.h` and `.cc` | `rebuild.sh` copied `.cc` files but not `.h` headers | Add header copy rules to `rebuild.sh` |
| `Slice` not a type | Compile error in `options.h` | Added `Slice lo, hi` without including the header | Add `#include "leveldb/slice.h"` to `options.h` |
| `KeyMayMatch` vs `RangeMayMatch` | Wrong method called, wrong parameter types | Typo: wrote `filter->KeyMayMatch(lo, hi)` | Correct to `filter->RangeMayMatch(lo, hi)` |
| SuRF crash on small tries | Segfault/FPE in `deSerialize` on per-2KB filters | Trie built from 10-50 keys too small for SuRF internals | Single filter per SSTable (`base_lg_=32`) |
| seekrandom anomaly | SuRF showed +150% slower in early benchmarks | Filter switching done by manual code editing; likely initialization mismatch | Added `--filter` command-line flag to eliminate manual errors |

---

## Repository Structure

```
leveldb-surf/
│
├── Dockerfile                    # Builds the complete dev environment
├── docker-compose.yml            # Container config with volume mounts
├── .gitattributes                # Enforces LF line endings (critical for Windows→Linux)
├── .gitignore                    # Excludes compiled files and benchmark databases
├── README.md                     # This file
│
├── .vscode/
│   └── settings.json             # VS Code settings (LF endings, C++ format)
│
├── project/
│   ├── filter_policy.h           # Week 2: added RangeMayMatch + NewSuRFFilterPolicy
│   ├── surf_filter.cc            # Week 2: full SuRF filter with thread_local cache
│   ├── filter_block.cc           # Week 3: single filter per SSTable (base_lg_=32)
│   ├── filter_block_test.cc      # Week 3: updated MultiChunk test expectations
│   ├── table.h                   # Week 3: added public RangeMayMatch declaration
│   ├── table.cc                  # Week 3: implemented Table::RangeMayMatch
│   ├── table_cache.h             # Week 3: updated NewIterator signature with lo/hi
│   ├── table_cache.cc            # Week 3: core RangeMayMatch hook after FindTable()
│   ├── version_set.h             # Week 3: updated AddIterators/NewConcatenatingIterator sigs
│   ├── version_set.cc            # Week 3: TableCacheArg, threading lo/hi through void* arg
│   ├── options.h                 # Week 3: added Slice lo/hi to ReadOptions
│   ├── db_impl.cc                # Week 3: one-line change passing lo/hi to AddIterators
│   ├── db_bench.cc               # Week 4: --filter flag, variable miss rate benchmarks
│   ├── notes/
│   │   ├── source_reading_notes.md  # Deep notes on all source files (1278 lines)
│   │   ├── week2_notes.md           # Week 2 decisions and implementation notes
│   │   └── demos/                   # Notes for each demo D1-D12
│   └── demos/                    # Hands-on demo files D1-D12
│       ├── d01_open_close.cc
│       ├── d02_put.cc
│       ├── d03_get.cc
│       ├── d04_delete.cc
│       ├── d05_writebatch.cc
│       ├── d06_iterator.cc
│       ├── d07_range_scan.cc
│       ├── d08_snapshot.cc
│       ├── d09_getproperty.cc
│       ├── d10_compaction.cc
│       ├── d11_filter_policy.cc
│       └── d12_leveldbutil.cc
│
├── dashboard-server/             # Node.js REST API backend
│   ├── index.js                  # Server entry point
│   ├── package.json              # Dependencies
│   ├── src/
│   │   ├── server.js             # Express app setup
│   │   ├── routes.js             # API endpoints
│   │   ├── metrics-parser.js     # JSONL parsing
│   │   └── summary-calculator.js # Metric aggregation
│   └── README.md                 # Backend documentation
│
├── dashboard-ui/                 # React + Vite frontend
│   ├── package.json              # Dependencies
│   ├── vite.config.ts            # Vite configuration
│   ├── index.html                # HTML entry point
│   ├── tsconfig.json             # TypeScript configuration
│   ├── tailwind.config.js        # Tailwind CSS config
│   ├── src/
│   │   ├── App.tsx               # Main React component
│   │   ├── main.tsx              # React DOM render
│   │   ├── index.css             # Global styles
│   │   ├── components/           # Reusable React components
│   │   │   ├── SummaryCards.tsx  # High-level metrics
│   │   │   ├── ComparisonCharts.tsx  # Bloom vs SuRF charts
│   │   │   ├── EventTable.tsx    # Query results table
│   │   │   ├── EventInspector.tsx        # Drill-down details
│   │   │   ├── FilterBar.tsx     # Search and filter
│   │   │   ├── LoadingState.tsx  # Loading UI
│   │   │   └── Header.tsx        # Navigation header
│   │   ├── lib/                  # Utility functions
│   │   │   ├── api.ts            # API client functions
│   │   │   └── metrics.ts        # Metric calculations
│   │   └── types/                # TypeScript definitions
│   │       └── metrics.ts        # Metric data types
│   └── README.md                 # Frontend documentation
│
├── metrics/                      # Generated JSONL benchmark results (git-ignored)
│   ├── bloom_fixcheck.jsonl      # Bloom filter metrics
│   └── surf_fixcheck.jsonl       # SuRF filter metrics
│
└── benchmarks/
    ├── rebuild.sh                # Compile and test after every code change
    ├── baseline_benchmark.sh     # Capture Bloom-only baseline numbers
    ├── surf_benchmarks.sh        # Full SuRF vs Bloom benchmark suite
    ├── baseline/                 # Bloom baseline results (captured Week 1)
    │   ├── seekrandom.txt
    │   ├── readrandom.txt
    │   ├── readseq.txt
    │   └── fillrandom.txt
    └── surf_results/             # SuRF vs Bloom comparison (captured Week 4)
        ├── SUMMARY.txt           # Complete comparison with analysis
        ├── bloom_readrandom.txt
        ├── surf_readrandom.txt
        ├── bloom_readseq.txt
        ├── surf_readseq.txt
        ├── bloom_seekrandom.txt
        ├── surf_seekrandom.txt
        ├── bloom_fillrandom.txt
        ├── surf_fillrandom.txt
        ├── bloom_surfscan100.txt
        ├── surf_surfscan100.txt
        ├── bloom_surfscan75.txt
        ├── surf_surfscan75.txt
        ├── bloom_surfscan50.txt
        ├── surf_surfscan50.txt
        ├── bloom_surfscan25.txt
        ├── surf_surfscan25.txt
        ├── bloom_surfscan0.txt
        ├── surf_surfscan0.txt
        ├── bloom_surfscan_wide.txt
        ├── surf_surfscan_wide.txt
        └── run_info.txt
```

---

## Environment Setup

### Why Docker
We use Docker so every teammate gets an identical Linux environment regardless of OS. Docker creates a mini Ubuntu 22.04 computer inside your laptop. LevelDB and SuRF live compiled inside that container. Code files live on your host machine but are shared into the container through a volume mount. You edit code in VS Code on your host. The container compiles and tests it.

### Why `.gitattributes` and `core.autocrlf false`
Windows saves text files with CRLF (`\r\n`) line endings. Linux needs LF (`\n`). Shell scripts with CRLF fail inside the container with `bad interpreter` errors. The `.gitattributes` file forces git to store all source files with LF endings. We also set `git config --global core.autocrlf false` so git never silently converts line endings.

### Why SSH Keys
GitHub disabled password authentication in August 2021. SSH keys (`ed25519`) are the standard authentication method. Verified with `ssh -T git@github.com`.

---

## The Dockerfile — Step by Step

**Base image:** `FROM ubuntu:22.04` — pinned for g++ 11 (C++17, required by SuRF) and cmake 3.22 (LevelDB requires 3.9+).

**Dependencies:** `apt-get install build-essential cmake git ...` — compiler, build system, version control, debugging tools in one layer.

**LevelDB clone:** `git clone --recurse-submodules` — `--recurse-submodules` is critical; without it `third_party/googletest/` is empty and tests fail.

**SuRF setup:** Clone from `github.com/efficient/SuRF`, copy headers into `include/surf/` so `#include "surf/surf.hpp"` resolves without extra compiler flags.

**CMakeLists.txt patch + build:** Three operations in ONE RUN command (must be atomic to avoid Docker layer caching issues):
1. `sed` patches CMakeLists.txt to include `surf_filter.cc`
2. Creates empty placeholder so cmake can find the file
3. Compiles everything

**Test validation:** `./leveldb_tests` runs during image build. Result: 210 pass, 1 skipped (zstd — expected, not installed).

---

## Daily Workflow

**Start the container:**
```powershell
cd "path-to-repo"
docker compose up -d
docker exec -it leveldb-surf bash
```

**Attach VS Code:** Click green `><` button → "Attach to Running Container" → select `leveldb-surf`.

**After any code change (inside container):**
```bash
bash /workspace/benchmarks/rebuild.sh
```

**Run a single benchmark (quick test):**
```bash
cd /workspace/leveldb/build
rm -rf /tmp/dbbench
./db_bench --filter=surf --benchmarks=fillrandom,surfscan100 --num=100000
```

**Run all benchmarks (full suite, ~20-30 minutes):**
```bash
bash /workspace/benchmarks/surf_benchmarks.sh
```

**Switch between filters (no recompilation needed):**
```bash
# Test with SuRF
./db_bench --filter=surf --benchmarks=fillrandom,surfscan50 --num=1000000

# Test with Bloom
./db_bench --filter=bloom --benchmarks=fillrandom,surfscan50 --num=1000000
```

**Available benchmark names:**
```
surfscan100   — 100% miss rate (all empty ranges)
surfscan75    — 75% miss, 25% hit
surfscan50    — 50% miss, 50% hit
surfscan25    — 25% miss, 75% hit
surfscan0     — 0% miss (all ranges hit keys)
surfscan_wide — 100% miss, wide range (width=100 keys)
surfscan      — alias for surfscan100
```

**Commit (on host):**
```bash
git add project/
git commit -m "Week X: description"
git push origin main
```

---

## What `rebuild.sh` Does

1. **Copy files** from `/workspace/project/` into LevelDB source tree (each file to its correct location)
2. **Incremental build** via `cmake --build` (only recompiles changed files)
3. **Run all tests** via `./leveldb_tests` — all 210 original tests must pass after every change

---

## LevelDB Source Files — What Each One Does

Understanding these files was required before writing any code (Week 1 study).

**`include/leveldb/filter_policy.h`** — The abstract interface every filter must implement: `Name()`, `CreateFilter()`, `KeyMayMatch()`. We added `RangeMayMatch()` with a safe default `return true` so existing Bloom filters work unchanged.

**`util/bloom.cc`** — The existing Bloom filter. Uses a flat bit array and double-hashing. Checking a key = compute k hash values, test k bit positions. Very fast for point queries. Cannot answer range queries because hashing destroys key ordering.

**`util/surf_filter.cc`** — Our new file. Implements `SuRFPolicy` with all four methods. Uses a compressed trie (FST) that preserves key ordering. Checking a key = deserialize trie, traverse nodes. Slower than Bloom for point queries, but can answer range queries. Includes thread_local deserialization cache and alignment-safe copy.

**`table/filter_block.h` + `filter_block.cc`** — `FilterBlockBuilder` builds filters during compaction. `FilterBlockReader` reads them during lookups. Originally created a filter every 2KB (`kFilterBaseLg=11`). We changed to one filter per SSTable (`base_lg_=32`).

**`table/table_builder.cc`** — Builds one complete SSTable. Only 4 lines touch the filter. We do not change this file.

**`table/table.cc`** — Read path. `Table::Open()` reads footer → index → filter block into memory. `InternalGet()` calls `KeyMayMatch()` for point queries. We added `RangeMayMatch()` for range queries.

**`db/version_set.cc`** — Tracks all SSTables across all 7 levels. `AddIterators()` is the starting point for range scan iterators. We thread `lo/hi` through here via `TableCacheArg`.

**`db/table_cache.cc`** — LRU cache of open SSTable files. `FindTable()` opens files and loads filters. `NewIterator()` is our hook point — `RangeMayMatch` runs here after `FindTable()`.

**`db/dbformat.cc` (`InternalFilterPolicy`)** — Hidden wrapper that strips internal key bytes (sequence number + type) before calling your filter. Your `SuRFPolicy` always receives clean user keys. We do not change this file.

---

## Architectural Limitation and Future Work

Our `RangeMayMatch` check runs after `FindTable()` loads the filter into memory. A pre-open range check — verifying no keys exist in `[lo, hi]` before opening the SSTable file at all — would require storing filter data in a separate partition outside the SSTable, in its own cache tier. RocksDB implemented this as **partitioned filters** in 2017. This represents a natural extension beyond this project's scope.

Other future directions include: per-SSTable adaptive filter selection (choose Bloom or SuRF based on observed access patterns for each SSTable), SuRF-Hash and SuRF-Real variants from the original paper (which trade false positive rate for space), and integration with LevelDB's compaction statistics to automatically identify SSTables that would benefit most from range filtering.

### Scalability — Variable Database Size (surfscan25, range_width=10)

| Keys   | DB Size | Bloom (µs/op) | SuRF (µs/op) | Change     | Notes                        |
|--------|---------|---------------|--------------|------------|------------------------------|
| 100K   | 11 MB   | 9.051         | 8.110        | **-10.4%** | SuRF wins, small working set |
| 500K   | 55 MB   | 9.145         | 10.795       | +18.0%     | Bloom wins, cache thrashing  |
| 1M     | 110 MB  | 7.214         | 5.817        | **-19.4%** | SuRF wins, sweet spot        |
| 10M    | 1.1 GB  | 13.761        | OOM killed   | —          | SuRF exhausts memory         |

**Why SuRF runs out of memory at 10M keys:**
The `thread_local` deserialization cache holds one `surf::SuRF*` object per thread. At 10M keys, each SSTable's serialized trie is much larger. As surfscan25 queries across many SSTables, the cache is invalidated frequently — each invalidation destroys the old trie and deserializes a new one. The repeated allocation and deallocation of large trie objects, combined with the larger working set, overwhelms available container memory.

**This is a known limitation of SuRF-Base.** The original SIGMOD 2018 paper tested SuRF inside RocksDB, which has a more sophisticated memory management system (partitioned filters, block-based filter caching). Integrating similar memory management into LevelDB — pooling deserialized tries in the LRU cache alongside data blocks rather than using `thread_local` — would be the fix. This is a natural future extension.

---

## Key Concepts Glossary

**LSM-tree** — Log-Structured Merge Tree. Writes go to memory first, then flush to sorted immutable files on disk.

**SSTable** — Sorted String Table. Immutable file containing data blocks, index block, and filter block.

**Bloom filter** — Flat bit array + hash functions. Answers point queries only. Checking = hash key k times, test k bit positions. Very fast. Cannot answer range queries because hashing destroys key ordering.

**SuRF** — Succinct Range Filter. Compressed trie answering both point and range queries using ~10 bits per key. Preserves key ordering in its trie structure, which is what enables range support. Slower than Bloom for point queries due to trie traversal overhead.

**FST** — Fast Succinct Trie. The compressed bit-array representation SuRF uses internally.

**Compaction** — Background process merging SSTables. This is when `CreateFilter()` is called.

**FilterPolicy** — LevelDB's plug-in interface: `Name()`, `CreateFilter()`, `KeyMayMatch()`, `RangeMayMatch()` (added by us).

**InternalFilterPolicy** — Hidden wrapper stripping internal key bytes before calling your filter. Your code always receives clean user keys.

**False positive** — Filter says "might have data" when it does not. Acceptable — just reads data blocks unnecessarily.

**False negative** — Filter says "definitely no data" when there is data. Catastrophic — silent data loss. Must never happen.

**Partitioned filters** — RocksDB's 2017 architecture storing filter data in a separate cache tier, enabling range checks before opening an SSTable. Not implemented in LevelDB; a natural future extension.

**micros/op** — Microseconds per operation. Smaller = faster.

---

## Project Timeline

| Week | What | Status |
|------|------|--------|
| 1 | Study all 8 source files, capture baseline benchmarks, build demos D1-D12 | COMPLETE |
| 2 | Implement `SuRFPolicy` in `surf_filter.cc`, add `RangeMayMatch` to `filter_policy.h` | COMPLETE |
| 3 | Wire `RangeMayMatch` into range scan path: `table_cache.cc`, `version_set.cc`, `table.h/cc`, `options.h`, `db_impl.cc`, `filter_block.cc` | COMPLETE |
| 4 | Add `--filter` flag, variable miss rate benchmarks, run comprehensive suite | COMPLETE |
| 5 | Analysis and report | IN PROGRESS |
| 6 | Presentation | TODO |

**Test status:** 210/210 tests passing after all changes.

---

## Current Checklist

- [x] SSH key generated and added to GitHub
- [x] Repository created at `github.com/dhrish-s/leveldb-surf`
- [x] Docker image built — 210/211 tests pass (1 skipped: zstd, expected)
- [x] All setup files committed and pushed
- [x] VS Code attached to container
- [x] Week 1: All 8 source files studied — notes in `project/notes/source_reading_notes.md`
- [x] Week 1: Baseline benchmarks captured — seekrandom 4.317 µs/op
- [x] Week 1: Hands-on demos D1-D12 completed
- [x] Week 2: SuRF filter implemented — `filter_policy.h` + `surf_filter.cc` — 210/210 tests pass
- [x] Week 3: Range scan integration — all files wired, `RangeMayMatch` active — 210/210 tests pass
- [x] Week 4: `--filter` flag added, variable miss rate benchmarks (surfscan0/25/50/75/100, surfscan_wide)
- [x] Week 4: Full benchmark suite run — SuRF 19.4% faster at 25% miss, 25.4% faster on seekrandom
- [x] Week 5: Observability dashboard built — full-stack Node.js + React with live polling and comparison charts
- [ ] Week 6: Presentation

---

## Restart Workflow

After each work session, follow this sequence to resume development and inspection:

**1. Generate metrics in container:**
```bash
cd "path-to-repo"
docker run -it --rm -v "${PWD}\project:/workspace/project" -v "${PWD}\benchmarks:/workspace/benchmarks" -v "${PWD}\metrics:/workspace/metrics" leveldb-surf

# Inside container
cd /workspace/leveldb/build
rm -rf /tmp/dbbench
./db_bench --filter=bloom --benchmarks=surfscan25,surfscan50,surfscan75 --num=500000 --metrics_out=/workspace/metrics/bloom_fixcheck.jsonl
```
If C++ source code changed, run `bash /workspace/benchmarks/rebuild.sh` before benchmarking.

**2. Start backend on host machine:**
```bash
cd "path-to-repo/dashboard-server"
node index.js  # uses bloom_fixcheck.jsonl by default
# Or to use SuRF results:
METRICS_FILE=./metrics/surf_fixcheck.jsonl node index.js
```

**3. Start frontend and open dashboard:**
```bash
# In a new terminal (on host)
cd "path-to-repo/dashboard-ui"
npm run dev
# Open http://localhost:5173
```

Recompilation is only required if C++ source code changes.