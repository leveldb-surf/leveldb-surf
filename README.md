# LevelDB + SuRF: Range Query Filter Extension
**Team:** Jahnavi Manoj · Dhrish Kumar Suman
**University:** University of Southern California
**Repository:** github.com/dhrish-s/leveldb-surf

---

## What This Project Does

LevelDB is a key-value store built on a Log-Structured Merge Tree (LSM-tree). It is used inside Google Chrome, Bitcoin Core, and Minecraft. Every SSTable (sorted file on disk) has a Bloom filter attached to it. A Bloom filter can answer one question: "is this exact key possibly in this file?" It cannot answer range queries like "does any key between `dog` and `lion` exist in this file?"

This means when you run a range scan, LevelDB opens every SSTable whose key-range overlaps your query — even if that file contains zero keys in your range. That is wasted I/O.

**We fix this by replacing the Bloom filter with SuRF (Succinct Range Filter)** — a data structure that can answer range queries. When SuRF says no key exists in a range, LevelDB skips reading that SSTable's data blocks entirely. The SIGMOD 2018 Best Paper showed up to 5x improvement in range query performance in RocksDB using SuRF.

---

## Repository Structure

```
leveldb-surf/
│
├── Dockerfile                    # Builds the complete dev environment
├── docker-compose.yml            # Container config with volume mounts
├── .gitattributes                # Enforces LF line endings (critical for Windows)
├── .gitignore                    # Excludes compiled files and benchmark databases
├── README.md                     # This file
│
├── .vscode/
│   └── settings.json             # VS Code settings (LF endings, C++ format)
│
├── project/
│   ├── filter_policy.h           # Week 2: added RangeMayMatch + NewSuRFFilterPolicy
│   ├── surf_filter.cc            # Week 2: full SuRF filter implementation
│   ├── notes/
│   │   ├── source_reading_notes.md  # Deep notes on all 8 source files (1278 lines)
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
└── benchmarks/
    ├── rebuild.sh                # Compile and test after every code change
    ├── baseline_benchmark.sh     # Capture before-SuRF performance numbers
    └── baseline/                 # Baseline results (captured Week 1 Part C)
        ├── seekrandom.txt        # 4.317 micros/op
        ├── readrandom.txt        # 3.691 micros/op
        ├── readseq.txt           # 0.213 micros/op
        └── fillrandom.txt        # 3.953 micros/op
```

---

## Environment Setup — What We Did and Why

### Why Docker?

We use Docker so every teammate gets an identical Linux environment regardless of whether they are on Windows, Mac, or Linux. The alternative — installing g++, cmake, and dependencies directly on Windows — leads to version mismatches, PATH problems, and hours of debugging the environment instead of the project.

Docker creates a mini Ubuntu 22.04 computer inside your laptop. LevelDB and SuRF live compiled inside that container. Your code files live on your Windows laptop but are shared into the container through a volume mount. You write code in VS Code on Windows. The container compiles and tests it.

### Why SSH Keys?

GitHub disabled password authentication in August 2021. SSH keys are the standard way to authenticate. We generated an `ed25519` key pair, moved it to the default location `C:\Users\ASUS\.ssh\`, and added the public key to GitHub. The connection is verified with `ssh -T git@github.com`.

### Why `.gitattributes`?

Windows saves text files with CRLF (`\r\n`) line endings. Linux needs LF (`\n`) only. Shell scripts with CRLF fail inside the container with `bad interpreter` errors. The `.gitattributes` file forces git to always store `.sh`, `Dockerfile`, `.yml`, `.cc`, and `.h` files with LF endings regardless of the OS.

### Why `core.autocrlf false`?

Git on Windows has `autocrlf=true` by default — it silently converts line endings. We disabled this with `git config --global core.autocrlf false` so git never touches our line endings.

---

## The Dockerfile — Step by Step

```
FROM ubuntu:22.04
```
Pinned to Ubuntu 22.04 for g++ 11 (C++17, required by SuRF) and cmake 3.22 (LevelDB requires 3.9+).

```
RUN apt-get update && apt-get install -y build-essential cmake git ...
```
Installs compiler, build system, version control, and debugging tools in one layer.

```
RUN git clone --recurse-submodules https://github.com/google/leveldb.git
```
`--recurse-submodules` is critical — without it `third_party/googletest/` is empty and tests fail.

```
RUN git clone https://github.com/efficient/SuRF.git
RUN cp -r /workspace/SuRF/include/* /workspace/leveldb/include/surf/
```
Clones SuRF from the paper authors and copies headers into LevelDB's include tree so `#include "surf/surf.hpp"` works without extra flags.

```
RUN cd /workspace/leveldb && \
    sed -i '/"util\/bloom.cc"/a\    "util\/surf_filter.cc"' CMakeLists.txt && \
    printf '// placeholder\n' > util/surf_filter.cc && \
    cmake ... && cmake --build . --parallel 4
```
Three things in ONE RUN command (critical — must be atomic):
1. `sed` patches CMakeLists.txt to include `surf_filter.cc`
2. Creates empty placeholder so cmake can find the file
3. Compiles everything including the placeholder

Combined into one RUN because separate RUN commands caused Docker layer caching to miss the placeholder, causing `Cannot find source file: util/surf_filter.cc`.

```
RUN /workspace/leveldb/build/leveldb_tests
```
Runs all 211 tests during image build. **Result: 210 pass, 1 skipped (zstd — expected).**

---

## Daily Workflow

**Every day when you sit down (after a reboot):**
```powershell
cd "path"
docker compose up -d
docker exec -it leveldb-surf bash
```

**If container is already running (no reboot):**
```powershell
docker exec -it leveldb-surf bash
```

**Attach VS Code to the container:**
1. Open VS Code
2. Click the green `><` button bottom-left
3. Select "Attach to Running Container"
4. Select `leveldb-surf`
5. VS Code terminal is now inside Linux — no more switching

**After any code change (inside container terminal):**
```bash
bash /workspace/benchmarks/rebuild.sh
```

**After a work session (on Windows):**
```powershell
git add project/your_file.cc
git commit -m "Week X: description of what you did"
git push origin main
```

---

## What `rebuild.sh` Does

```
Step 1: Copy files from /workspace/project/ into LevelDB source tree
        surf_filter.cc        → util/
        filter_policy.h       → include/leveldb/
        filter_block.cc       → table/  (only if file exists in project/)
        table.cc              → table/  (only if file exists in project/)
        two_level_iterator.cc → table/  (only if file exists in project/)
        table_cache.cc        → db/     (only if file exists in project/)
        version_set.cc        → db/     (only if file exists in project/)

Step 2: cmake --build (incremental — only recompiles changed files)

Step 3: ./leveldb_tests — ALL original tests must keep passing
```

---

## LevelDB Source — What Each File Does

This section documents every source file we studied in Week 1. Understanding these files is required before writing any code.

### `include/leveldb/filter_policy.h` — The Contract
The abstract interface every filter must implement. Defines 3 methods:
- `Name()` — returns filter name, stored in SSTable, must match on read or filter is ignored
- `CreateFilter(keys, n, dst)` — builds filter from n sorted keys, appends bytes to dst
- `KeyMayMatch(key, filter)` — returns false = definitely absent, true = maybe present

**We add a 4th method (Week 2 — DONE):**
- `RangeMayMatch(lo, hi, filter)` — returns false = no key in [lo,hi], true = maybe has keys
- Default `return true` so existing Bloom filter works unchanged

`RangeMayMatch` had **0 grep results** across the entire codebase before Week 2.

### `util/bloom.cc` — The Existing Filter
Implements Bloom filter using a bit array and double-hashing.
- `CreateFilter`: hashes each key k times, sets those bits ON
- `KeyMayMatch`: hashes query key same way, checks if all k bits are ON
- **Cannot answer range queries**: hashing destroys all key ordering

### `util/surf_filter.cc` — Our SuRF Implementation (Week 2 — DONE)
New file implementing SuRFPolicy:
- `Name()` — returns `"leveldb.SuRFFilter"`. Never change this string.
- `CreateFilter()` — builds SuRF trie from sorted keys, serializes to bytes, appends to dst
- `KeyMayMatch()` — deserializes SuRF, calls `lookupKey()`. Returns false = definitely absent.
- `RangeMayMatch()` — deserializes SuRF, calls `lookupRange(lo, true, hi, true)`.

See `project/notes/week2_notes.md` for full implementation details.

### `table/filter_block.h` + `filter_block.cc` — The 2KB Problem
`FilterBlockBuilder` builds the filter during compaction. `FilterBlockReader` reads it during lookups.

`kFilterBaseLg = 11` means `GenerateFilter()` is called every 2KB — many mini-filters per SSTable.

**Why filter_block.cc was NOT changed:**
The `FilterBlockTest.MultiChunk` test requires per-block filter isolation.
Changing the writer without changing the reader and updating the tests would break existing behavior.
`RangeMayMatch` operates at the SSTable level in `table_cache.cc` so this does not block the project.
See `project/notes/week2_notes.md` for full explanation.

### `table/table_builder.cc` — Write Path
Builds one complete SSTable. Only 4 lines touch the filter. We do not change this file.

### `table/table.cc` — Read Path
- `Table::Open()`: reads footer → index → filter block into memory
- `InternalGet()` line 225: `filter->KeyMayMatch()` — point queries already optimized
- `NewIterator()`: creates TwoLevelIterator — no range filter check yet ← gap filled in Week 3

### `db/version_set.cc` — SSTable Directory
Tracks all SSTables across all 7 levels.
- `FileMetaData`: every SSTable has `smallest` and `largest` key — the coarse range filter
- `AddIterators()`: starting point for range scan iterators — our threading starts here

### `db/table_cache.cc` — Our Hook Point (Week 3)
LRU cache of open SSTable files.
- `FindTable()`: checks LRU cache, opens `.ldb` file if not cached, loads filter into memory
- `NewIterator()` — **our hook point for RangeMayMatch**

**Important architectural constraint:**
`RangeMayMatch` requires the filter bytes (the serialized SuRF trie). These bytes live inside the `.ldb` SSTable file in the filter block. To read them, the file must be opened. Opening the file is `FindTable()`. Therefore `RangeMayMatch` **cannot** run before `FindTable()` — this would require the filter to exist outside the SSTable, which is a full storage redesign (RocksDB's partitioned filter architecture, 2017).

**Correct hook location — AFTER FindTable(), BEFORE data block reads:**
```
FindTable()                    ← opens SSTable, loads filter block into memory
filter now in RAM              ← filter bytes available for RangeMayMatch
RangeMayMatch(lo, hi, filter)  ← check range against in-memory filter
false → return empty iterator  ← skip data block reads (the expensive part)
true  → table->NewIterator()   ← proceed to read data blocks
```

**When is this beneficial:**
- SSTable warm in LRU cache: `FindTable()` is a cache hit (nearly free). `RangeMayMatch` then prevents all data block reads. Maximum benefit.
- SSTable cold (not cached): `FindTable()` reads the filter block from disk anyway as part of `Table::Open()`. `RangeMayMatch` still prevents the more expensive data block reads. Reduced but real benefit.

**Why not before FindTable (pre-open check):**
This would require storing filter data in a separate cache tier independent of the SSTable file. RocksDB implemented this as "partitioned filters" in 2017. It is a significant storage layout redesign outside the scope of this project. This is a known limitation and a natural future extension.

### `InternalFilterPolicy` — The Hidden Wrapper
Defined in `db/dbformat.cc`. Strips internal key bytes (sequence number + type) before calling your filter. Your `SuRFPolicy` always receives clean user keys. You never handle internal key format.

---

## Project Architecture

### The Problem

When a range query `[lo, hi]` runs:
1. **Coarse check** (already exists): skip SSTables whose `[smallest, largest]` does not overlap `[lo, hi]`
2. **Fine check** (we add): for SSTables that pass the coarse check, does any actual key exist in `[lo, hi]`?

Example:
```
SSTable B: smallest=bear, largest=hippo → passes coarse check
Actual keys: bear, cat, elephant   (none between dog and fox)
Query: [dog, fox]
Without SuRF: LevelDB opens SSTable B, reads data blocks, finds nothing. Wasted I/O.
With SuRF:    RangeMayMatch("dog","fox") → false → skip data block reads
```

### The Solution — SuRF

SuRF builds a compressed trie from all keys. To answer "any key in [elk, hippo]?", it finds the successor of "elk" in the trie. If that successor > "hippo" — answer is no. Uses ~10 bits per key.

### Files We Modify

| File | Change | Week | Status |
|---|---|---|---|
| `include/leveldb/filter_policy.h` | Add `RangeMayMatch()` default `return true`, declare `NewSuRFFilterPolicy` | 2 | DONE |
| `util/surf_filter.cc` | New file: SuRFPolicy implementing all 4 methods | 2 | DONE |
| `table/filter_block.cc` | NOT changed — see week2_notes.md | 2 | SKIPPED |
| `db/table_cache.cc` | Add `RangeMayMatch` check after `FindTable()`, before `NewIterator()` | 3 | TODO |
| `db/version_set.cc` | Thread `[lo, hi]` from `AddIterators()` down to `GetFileIterator()` | 3 | TODO |
| `table/two_level_iterator.cc` | Pass range bounds to inner iterator | 3 | TODO |
| `benchmarks/db_bench.cc` | Change to `NewSuRFFilterPolicy()` at line 523 | 4 | TODO |

### The Read Path — Range Scan

```
DB::NewIterator()
  └── Version::AddIterators()             [version_set.cc]
        coarse check: FileMetaData min/max
        └── NewConcatenatingIterator()
              └── TwoLevelIterator
                    outer: LevelFileNumIterator (walks file list)
                    inner: GetFileIterator()
                             └── TableCache::NewIterator()   [table_cache.cc]
                                   FindTable()
                                     ← SSTable opened, filter loaded into RAM
                                   RangeMayMatch(lo, hi) CHECK HERE
                                     false → return empty iterator
                                             (data block reads skipped)
                                     true  → table->NewIterator()
                                               └── scan data blocks
```

### The Write Path — Filter Build During Compaction

```
DoCompactionWork()
  └── BuildTable()
        └── TableBuilder::Add(key, value)      [table_builder.cc]
              └── FilterBlockBuilder::AddKey()  [filter_block.cc]
                    buffers key (per-2KB chunks, original behavior)
              └── TableBuilder::Finish()
                    └── FilterBlockBuilder::Finish()
                          └── SuRFPolicy::CreateFilter(keys) [surf_filter.cc]
                                builds SuRF trie → serializes → writes to SSTable
```

### The Safety Rule

```
KeyMayMatch / RangeMayMatch:
  false → MUST be correct. Definitely no key. Never lie here.
  true  → CAN be wrong. False positives are acceptable (just slower).

False negative = data loss = catastrophic = never acceptable
False positive = extra data block read = acceptable

Filters are PERFORMANCE ONLY — not correctness.
```

### Architectural Limitation and Future Extension

Our `RangeMayMatch` check runs after `FindTable()` loads the filter into memory. For warm (cached) SSTables this is essentially free and the data block reads are fully avoided. For cold SSTables, the filter block is read from disk as part of opening the file, which is unavoidable in LevelDB's architecture.

A pre-open range check — verifying no keys exist in `[lo, hi]` before opening the SSTable file at all — would require storing filter data in a separate partition outside the SSTable, in its own cache tier. RocksDB implemented this as partitioned filters in 2017. This represents a natural extension beyond this project's scope.

---

## Baseline Benchmarks — Before SuRF

Captured: March 26, 2026 — 1 million keys, 16-byte keys, 100-byte values.

```
┌──────────────┬────────────────┬──────────────────────────────────┐
│ Benchmark    │ Result         │ Expected After SuRF              │
├──────────────┼────────────────┼──────────────────────────────────┤
│ seekrandom   │ 4.317 µs/op   │ ~2.0–3.5 µs/op (20–50% faster)  │
│ readrandom   │ 3.691 µs/op   │ ~3.691 µs/op (unchanged)         │
│ readseq      │ 0.213 µs/op   │ ~0.213 µs/op (unchanged)         │
│ fillrandom   │ 3.953 µs/op   │ ~4.2–4.8 µs/op (10–20% slower)  │
└──────────────┴────────────────┴──────────────────────────────────┘
```

**Key insight:** `seekrandom (4.317) > readrandom (3.691)` by 0.626 µs/op.
That gap = wasted data block reads during range scans.
SuRF's `RangeMayMatch()` eliminates those wasted reads.

---

## Project Timeline

### Week 1 — Study and Baseline COMPLETE
- All 8 source files read — notes in `project/notes/source_reading_notes.md`
- Baseline benchmarks captured — results in `benchmarks/baseline/`
- Hands-on demos D1-D12 — all in `project/demos/` with notes

### Week 2 — Implement SuRFPolicy COMPLETE
```
NEW:    project/surf_filter.cc
          SuRFPolicy::Name()           → "leveldb.SuRFFilter"
          SuRFPolicy::CreateFilter()   → build SuRF trie, serialize to bytes
          SuRFPolicy::KeyMayMatch()    → deserialize SuRF, call lookupKey
          SuRFPolicy::RangeMayMatch()  → deserialize SuRF, call lookupRange

MODIFY: project/filter_policy.h
          added: virtual bool RangeMayMatch(lo, hi, filter) const { return true; }
          added: LEVELDB_EXPORT const FilterPolicy* NewSuRFFilterPolicy();

NOT CHANGED: filter_block.cc
          Per-2KB behavior kept to preserve test compatibility.
          See project/notes/week2_notes.md for full explanation.
```
Test result: 210/210 tests pass. 1 skipped (zstd — expected).

### Week 3 — Wire Into Range Scan Path TODO
```
MODIFY: db/table_cache.cc
          In NewIterator(): after FindTable() loads filter into memory,
          call RangeMayMatch(lo, hi, filter).
          If false → return empty iterator (skip data block reads).
          If true  → proceed to table->NewIterator() as normal.

MODIFY: db/version_set.cc
          Thread [lo, hi] from AddIterators() → GetFileIterator()

MODIFY: table/two_level_iterator.cc
          Pass range bounds to inner iterator creation
```

### Week 4 — Benchmarking TODO
```
MODIFY: benchmarks/db_bench.cc line 523
          change: NewBloomFilterPolicy(10)
          to:     NewSuRFFilterPolicy()

RUN: seekrandom, YCSB Workload E at 1M and 10M keys
COLLECT: latency, data block reads skipped, false positive rate
```

### Week 5 — Analysis and Report TODO
### Week 6 — Presentation TODO

---

## Key Concepts Glossary

**LSM-tree** — Log-Structured Merge Tree. Writes go to memory first, then flush to sorted immutable files on disk.

**SSTable** — Sorted String Table. Immutable file on disk. Contains data blocks, index block, and filter block.

**Bloom filter** — Bit array + hash functions. Answers point queries only. Cannot answer range queries because hashing destroys key ordering.

**SuRF** — Succinct Range Filter. Compressed trie. Answers "does any key in [lo, hi] exist here?" using ~10 bits per key.

**FST** — Fast Succinct Trie. The compressed bit-array form of a trie that SuRF uses internally.

**Compaction** — Background process that merges SSTables. This is when `CreateFilter()` is called.

**FilterPolicy** — LevelDB's plug-in interface. `Name()`, `CreateFilter()`, `KeyMayMatch()`, `RangeMayMatch()` (added Week 2).

**InternalFilterPolicy** — Hidden wrapper that strips internal key bytes before calling your filter. Your code always receives clean user keys.

**False positive** — Filter says "might have data" when it does not. Acceptable — just reads data blocks unnecessarily.

**False negative** — Filter says "definitely no data" when there is data. Catastrophic — silent data loss. Must never happen.

**2KB problem** — FilterBlockBuilder calls `CreateFilter()` every 2KB. filter_block.cc kept as original. `RangeMayMatch` works at SSTable level regardless.

**Threading problem** — The query range `[lo, hi]` must be passed through multiple function call layers to reach `RangeMayMatch()` in `table_cache.cc`. Requires changing function signatures in `version_set.cc` and `two_level_iterator.cc`.

**Partitioned filters** — RocksDB's 2017 architecture that stores filter data in a separate cache tier, enabling range checks before opening an SSTable file. Not implemented in LevelDB. Would allow pre-open `RangeMayMatch` checks, which our implementation cannot do.

**micros/op** — Microseconds per operation. Smaller = faster.

---

## Current Status

- [x] SSH key generated and added to GitHub
- [x] Repository created at `github.com/dhrish-s/leveldb-surf`
- [x] Docker image built — 210/211 tests pass (1 skipped: zstd, expected)
- [x] All setup files committed and pushed
- [x] VS Code attached to container
- [x] Week 1 Part B: All 8 source files read — notes in `project/notes/source_reading_notes.md`
- [x] Week 1 Part C: Baseline benchmarks captured — seekrandom **4.317 µs/op**
- [x] Week 1 Part D: Hands-on demos D1-D12 — all in `project/demos/` with individual notes
- [x] Week 2: SuRF filter implemented — `filter_policy.h` + `surf_filter.cc` — **210/210 tests pass**
- [ ] Week 3: Range scan integration — `table_cache.cc`, `version_set.cc`, `two_level_iterator.cc`
- [ ] Week 4: Benchmarking
- [ ] Week 5: Analysis and report
- [ ] Week 6: Presentation