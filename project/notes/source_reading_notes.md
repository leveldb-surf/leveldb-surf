# LevelDB Source Reading Notes
# Week 1 Part B
# Read all 8 source files before writing any code

---

## B1 — filter_policy.h
**Path:** include/leveldb/filter_policy.h

**What it is:**
The contract. Every filter (Bloom, SuRF, anything) must follow this interface.
Think of it as a job description — anyone who wants to be a filter must do these exact things.

**The 3 methods that exist:**

```
Name()
  - Returns a string name for the filter
  - Stored inside every SSTable at write time
  - Checked at read time — if names don't match, filter is ignored silently
  - YOUR SuRF must return "leveldb.SuRFFilter" consistently

CreateFilter(keys, n, dst)
  - Called during compaction (write path)
  - keys[] is an array of ALL keys in the SSTable, already sorted
  - n is the count of keys
  - dst is a string you APPEND your filter bytes to
  - Bloom: hashes each key k times, sets bits in a bit array
  - SuRF: inserts all keys into SuRFBuilder, builds trie, serializes to bytes
  - IMPORTANT: keys arrive sorted — SuRF requires this for trie construction

KeyMayMatch(key, filter)
  - Called during DB::Get() (read path)
  - Returns false = key DEFINITELY NOT in this SSTable (skip it, no disk read)
  - Returns true  = key MIGHT be in this SSTable (open and check)
  - MUST NEVER return false when key actually exists (false negative = data loss)
  - False positives (return true when absent) are fine — just extra disk read
```

**The 1 method that does not exist yet (YOU ADD THIS):**

```
RangeMayMatch(lo, hi, filter)
  - Called during range scans (NEW — does not exist anywhere in codebase)
  - Returns false = NO key in [lo, hi] exists in this SSTable (skip entirely)
  - Returns true  = some key in [lo, hi] MIGHT exist (open and scan)
  - Default implementation returns true (safe — never skips anything)
  - This default means existing Bloom filter works without any changes
  - Only SuRF overrides this to actually answer range queries
```

**The factory function:**

```
NewBloomFilterPolicy(bits_per_key)
  - Creates a BloomFilterPolicy object on the heap, returns pointer
  - Called like: options.filter_policy = NewBloomFilterPolicy(10)
  - YOU write: NewSuRFFilterPolicy()
  - Changing one line switches the entire database from Bloom to SuRF
```

**Key insight:**
KeyMayMatch uses hashing — throws away all key ordering.
"ant" and "zebra" both become random numbers — no relationship preserved.
Cannot answer "any key between bear and fox?"
SuRF uses a trie — preserves actual characters and ordering.
This is why SuRF can answer range queries and Bloom cannot.

---

## B2 — bloom.cc
**Path:** util/bloom.cc

**What it is:**
The existing Bloom filter implementation you are replacing with SuRF.
This is the code that runs today when LevelDB asks "is this key possibly here?"

**Mental model — imagine a row of light switches:**
```
Position:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
Bit:       0  0  0  0  0  0  0  0  0  0   0  0  0  0  0  0
           (all OFF at start)

Adding "cat": hash1=3, hash2=7, hash3=11 → flip those positions ON
Position:  0  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15
Bit:       0  0  0  1  0  0  0  1  0  0   0  1  0  0  0  0
```

**Key variables:**
```
bits_per_key_  = bits used per key (10 = ~1% false positive rate)
k_             = number of hash functions = bits_per_key * 0.69
                 Mathematical optimum: k = (m/n) * ln(2), ln(2) ≈ 0.693
                 With bits_per_key=10: k = 6 hash functions
```

**CreateFilter(keys, n, dst):**
```
1. Allocate n * bits_per_key bits (zeroed out)
2. For each key:
   a. Hash it once: h = BloomHash(key)
   b. Compute delta = rotate h right by 17 bits (for double-hashing)
   c. Loop k_ times: bitpos = h % bits → set that bit ON → h += delta
3. Append k_ as the last byte (so KeyMayMatch knows how many hashes to check)
Called during compaction — write path
```

**KeyMayMatch(key, filter):**
```
1. Read k from last byte of filter
2. Hash the query key k times using same double-hash formula
3. For each bit position:
   - If bit is OFF → return false (DEFINITELY absent)
4. If all k bits are ON → return true (MAYBE present)
Called during Get() — read path
```

**Why it CANNOT do range queries:**
```
BloomHash() converts a key to a 32-bit number
All ordering information is DESTROYED
"ant" (before "dog") and "zebra" (after "dog") produce unrelated numbers
No way to answer "any key between bear and fox?"
```

**False positive rate formula:**
```
(1 - e^(-kn/m))^k
k=6 hash functions, n=keys, m=bits → approximately 1% false positive rate
```

---

## B3 — filter_block.h
**Path:** table/filter_block.h

**What it is:**
Defines two classes that manage the filter block inside an SSTable.
FilterBlockBuilder = builds filters during compaction (write path)
FilterBlockReader  = reads filters during lookups (read path)

**Call sequence for Builder (from the comment in the file):**
```
(StartBlock AddKey*)* Finish

Meaning:
StartBlock()      ← new 2KB data block starts
AddKey(key1)      ← key added to current block
AddKey(key2)
AddKey(key3)
StartBlock()      ← next 2KB block starts
AddKey(key4)
...
Finish()          ← called once at end of entire SSTable
```

**Key private variables in FilterBlockBuilder:**
```
policy_         = the plug-in filter (Bloom today, SuRF after your project)
keys_           = one big string with all keys concatenated: "catdogfox"
start_          = index of where each key starts: [0, 3, 6]
result_         = accumulates all filter bytes as they are built
filter_offsets_ = records where each mini-filter starts inside result_
tmp_keys_       = temporary Slice array rebuilt from keys_+start_ for CreateFilter()
```

**THE 2KB PROBLEM — Challenge 1 of your project:**
```
Current behavior:
  keys 1-20  (block 0) → GenerateFilter() → mini-filter 0
  keys 21-40 (block 1) → GenerateFilter() → mini-filter 1
  keys 41-60 (block 2) → GenerateFilter() → mini-filter 2
  ...
  One SSTable = many mini-filters, each covers only 20 keys

SuRF needs:
  keys 1-20  → buffer
  keys 21-40 → buffer
  keys 41-60 → buffer
  Finish()   → CreateFilter(ALL keys) → one SuRF trie
  One SSTable = one filter, covers ALL keys

Your fix in Week 2:
  Stop calling GenerateFilter() at every 2KB boundary
  Buffer ALL keys across all data blocks
  Call CreateFilter() once in Finish() with all keys
```

**FilterBlockReader key variables:**
```
base_lg_ = 11 → 2^11 = 2048 = 2KB (the encoding parameter)
data_    = pointer to start of filter block bytes
offset_  = pointer to the offset table at end of filter block
num_     = number of mini-filters
```

---

## B4 — filter_block.cc
**Path:** table/filter_block.cc

**What it is:**
The implementation of FilterBlockBuilder and FilterBlockReader.
Most important file for understanding Challenge 1.

**kFilterBaseLg = 11:**
```
kFilterBase = 1 << 11 = 2048 bytes = 2KB
This constant drives EVERYTHING about when filters are created
One new filter generated every 2KB of data
```

**AddKey(key) — simple buffering:**
```
start_.push_back(keys_.size())  ← record where this key starts
keys_.append(key)               ← append key bytes to the packed string

Example after adding "cat", "dog", "fox":
  keys_  = "catdogfox"
  start_ = [0, 3, 6]
```

**StartBlock(block_offset) — when to generate:**
```
filter_index = block_offset / 2048
while filter_index > filter_offsets_.size():
    GenerateFilter()

Example:
  StartBlock(0)    → filter_index=0, 0>0 false → nothing
  StartBlock(2048) → filter_index=1, 1>0 true  → GenerateFilter() called!
  StartBlock(4096) → filter_index=2, 2>1 true  → GenerateFilter() called!
```

**GenerateFilter() — the core:**
```
1. Reconstruct individual Slice objects from packed keys_+start_
   (adds keys_.size() as sentinel for last key length computation)
2. filter_offsets_.push_back(result_.size())  ← record where this filter starts
3. policy_->CreateFilter(&tmp_keys_[0], n, &result_)  ← BUILD THE FILTER
4. Clear keys_, start_, tmp_keys_ for next batch

THIS IS THE METHOD WE CHANGE IN WEEK 2:
  Current: called at every 2KB boundary with partial key set
  Future:  called ONCE in Finish() with ALL keys
```

**Finish() — assembles the final filter block:**
```
1. Flush remaining keys if any
2. Append filter_offsets_ table (4 bytes per filter, records start positions)
3. Append array_offset (where offset table starts)
4. Append kFilterBaseLg (11) as last byte

Final layout on disk:
[filter 0][filter 1][filter 2]...[offset table][array_offset][11]
```

**FilterBlockReader constructor — reading back from disk:**
```
Start from END of bytes:
  base_lg_   = last byte        (= 11)
  last_word  = 4 bytes before   (= array_offset, where offset table starts)
  data_      = start of bytes
  offset_    = data_ + last_word (start of offset table)
  num_       = (bytes between offset_ and last 5 bytes) / 4
```

**KeyMayMatch(block_offset, key):**
```
index = block_offset >> 11    ← which 2KB chunk = which mini-filter
start = offset_table[index]   ← where that mini-filter starts
limit = offset_table[index+1] ← where it ends
filter = data_[start..limit]
return policy_->KeyMayMatch(key, filter)

If error: return true (safe — never skip on doubt)
```

---

## B5 — table_builder.cc
**Path:** table/table_builder.cc

**What it is:**
Builds one complete SSTable file from scratch.
Takes key-value pairs one at a time, writes them to disk.
Most of this file is NOT your concern — only 4 lines matter.

**The 4 lines that matter for your project:**

**1. Constructor — plug-in point:**
```cpp
filter_block(opt.filter_policy == nullptr
    ? nullptr
    : new FilterBlockBuilder(opt.filter_policy))
```
If filter_policy is set → create FilterBlockBuilder with it.
This is where Bloom or SuRF gets wired into the write path.

**2. Add(key, value) — per-key call:**
```cpp
if (r->filter_block != nullptr) {
    r->filter_block->AddKey(key);   ← key given to filter
}
r->data_block.Add(key, value);      ← key+value written to data block
```
AddKey() called BEFORE data block flush. Key belongs to current block.

**3. Flush() — every 2KB:**
```cpp
WriteBlock(&r->data_block, &r->pending_handle);  ← data block to disk
filter_block->StartBlock(r->offset);              ← triggers GenerateFilter()
```
After writing 2KB of data → tell filter "new block starting at this offset"

**4. Finish() — end of SSTable:**
```cpp
WriteRawBlock(r->filter_block->Finish(), kNoCompression, &filter_block_handle);
key.append(r->options.filter_policy->Name());  ← filter name stored in SSTable
```
Calls filter_block->Finish() → gets all filter bytes → writes to disk.
Stores filter name in metaindex. Must match on read or filter ignored.

**What you do NOT change in this file:**
TableBuilder stays exactly the same.
You only change FilterBlockBuilder (filter_block.cc).
TableBuilder already calls Finish() at the end — you just change what Finish() does.

**Write path summary:**
```
Add() × N keys → Flush() every 2KB → StartBlock() → GenerateFilter()
→ Finish() → filter_block->Finish() → filter written to disk
```

---

## B6 — table.cc
**Path:** table/table.cc

**What it is:**
Reads and uses an SSTable file. The librarian who opens the book.
table_builder.cc was the printer. table.cc is the reader.

**Analogy:** SSTable = a book. table.cc = the librarian.
```
The Book (SSTable):
├── Chapters (Data Blocks)    ← actual key-value pairs
├── Index at the back         ← "chapter X starts at page Y"
├── Filter bookmark ribbon    ← "does this chapter contain key X?"
└── Back cover (footer)       ← points to index + filter
```

**Table::Open():**
```
1. Read FOOTER (last bytes of file — always fixed size)
   Footer → find index location + filter location
2. Read INDEX BLOCK into memory
   Index = table of contents (key range → data block location)
3. Call ReadMeta() → load the filter
```

**ReadMeta() — filter name check:**
```
Looks for entry: "filter." + policy->Name()
Example: "filter.leveldb.BuiltinBloomFilter2"

If found AND name matches current policy → load filter
If not found OR name mismatch → no filter (falls back to reading everything)

WHY THIS MATTERS: Your SuRF Name() must stay consistent.
Wrong name = filter silently ignored = slow reads with no error message.
```

**ReadFilter():**
```
Reads raw filter bytes from disk into RAM.
Creates FilterBlockReader — answers KeyMayMatch() questions.
```

**InternalGet() — THE KEY METHOD for point queries:**
```
Step 1: Ask index → which data block might have this key?
Step 2: Ask filter → KeyMayMatch(block_offset, key)?
        NO  → skip block, zero disk reads saved ← BLOOM FILTER WORKS HERE
        YES → open block, search for key
Step 3: Read value from data block

Line 225: if (!filter->KeyMayMatch(handle.offset(), k)) { // Not found }
This is the line that saves disk reads for Get() calls.
```

**NewIterator() — for range scans:**
```
return NewTwoLevelIterator(
    index_block->NewIterator(),   ← outer: walks index entries
    &Table::BlockReader,          ← inner: opens one data block
    ...);

NO FILTER CHECK EXISTS HERE RIGHT NOW.
Your project adds RangeMayMatch() check before BlockReader opens a block.
```

**The gap your project fills:**
```
InternalGet  → has KeyMayMatch check     → point queries already optimized
NewIterator  → NO filter check           → range scans still open everything
After SuRF   → RangeMayMatch added here  → range scans optimized too
```

---

## B7 — version_set.cc
**Path:** db/version_set.cc

**What it is:**
The library directory — tracks ALL SSTables across ALL levels.
Knows which files exist, what key range each covers, how to scan them.

**Analogy:** 7-floor library. Each floor = one level (0-6).
Each shelf = one SSTable. Spine label = smallest key to largest key.

**FileMetaData — the spine label on each SSTable:**
```
struct FileMetaData {
    uint64_t number;       // file ID (e.g. 000042.ldb)
    uint64_t file_size;    // size in bytes
    InternalKey smallest;  // smallest key in this file
    InternalKey largest;   // largest key in this file
};
```

**The two-level filter (coarse + fine):**
```
Query: "find keys between dog and fox"

COARSE CHECK (already exists — FileMetaData):
  SSTable A: largest=cat   → cat < dog  → skip (entirely before query)
  SSTable B: range overlaps query        → pass to fine check
  SSTable C: smallest=lion → lion > fox → skip (entirely after query)

FINE CHECK (SuRF — you add this):
  SSTable B: range overlaps BUT actual keys are bear, cat, elephant
  None are between dog and fox
  RangeMayMatch("dog", "fox") → false → SKIP
  Saved one disk read that coarse check could not prevent
```

**AddIterators() — THE function your project touches:**
```cpp
void Version::AddIterators(const ReadOptions& options,
                           std::vector<Iterator*>* iters) {
    // Level 0: one iterator per file (files can overlap)
    for each file in files_[0]:
        iters->push_back(table_cache_->NewIterator(file))

    // Levels 1-6: one concatenating iterator per level
    for level in 1..6:
        iters->push_back(NewConcatenatingIterator(level))
}

YOUR HOOK GOES HERE:
  Before table_cache_->NewIterator(file) is called:
  if RangeMayMatch(lo, hi, file) == false → skip this file
```

**LevelFileNumIterator — the outer iterator:**
```
Walks through the list of SSTables on one level.
key()   = largest key in current SSTable
value() = file number + file size (used to open the file)

This is the outer level of the TwoLevelIterator.
Inner level opens the actual SSTable when outer lands on it.
```

**GetFileIterator() — opens one SSTable:**
```
Called for each candidate SSTable during range scan.
Currently called unconditionally — no filter check.
YOUR CHANGE: check RangeMayMatch before calling this.
```

**NewConcatenatingIterator():**
```
Combines:
  Outer = LevelFileNumIterator (walks file list)
  Inner = GetFileIterator (opens one SSTable lazily)

"Lazily" = inner only opens a file when outer actually lands on it.
Your RangeMayMatch check goes between outer landing and inner opening.
```

---

## B8 — table_cache.cc
**Path:** db/table_cache.cc

**What it is:**
A cache of recently opened SSTable files.
Opening an SSTable from disk is slow. Cache keeps recent ones in memory.

**Analogy:** Hotel concierge with a recently-opened-rooms list.
Guest arrives → concierge checks list → if room recently opened: instant access.
If not: concierge walks to room, unlocks it, adds to list.

**TableAndFile struct:**
```
struct TableAndFile {
    RandomAccessFile* file;   // open file handle
    Table* table;             // parsed SSTable object (index + filter loaded)
};
One entry per cached SSTable in the LRU cache.
```

**FindTable(file_number, file_size) — core of the cache:**
```
Step 1: cache_->Lookup(file_number)
        Hit  → return immediately (fast, memory only)
        Miss → continue to step 2

Step 2: Open file from disk
        env_->NewRandomAccessFile(filename, &file)
        Table::Open(options_, file, file_size, &table)
        This reads footer, index, and filter from disk (SLOW)

Step 3: cache_->Insert(file_number, TableAndFile{file, table})
        Store for next time

YOUR HOOK POINT:
  RangeMayMatch check happens BEFORE FindTable() is called.
  If false → return empty iterator, skip FindTable entirely.
  Zero disk I/O, zero cache lookup.
```

**NewIterator() — YOUR HOOK POINT:**
```cpp
Iterator* TableCache::NewIterator(file_number, file_size) {
    FindTable(file_number, file_size, &handle);  ← opens SSTable if needed
    table->NewIterator(options);                 ← creates key iterator
    return result;
}

YOUR CHANGE:
  Add parameter: const Slice& lo, const Slice& hi
  Add check BEFORE FindTable():
    if (has_range && !RangeMayMatch(lo, hi, file))
        return NewEmptyIterator()  ← skip entirely

  This requires threading [lo, hi] down from:
  AddIterators() → NewConcatenatingIterator() → GetFileIterator() → here
  That threading work = Challenge 2 from project proposal
```

**Get() — point queries:**
```
Calls FindTable() then t->InternalGet().
InternalGet() already calls KeyMayMatch() inside table.cc.
Point queries already optimized. No changes needed here.
```

**Evict(file_number):**
```
Called when SSTable deleted after compaction.
Removes from cache so memory not wasted on dead files.
```

---

## B9 — grep Results — Complete Call Map

**Every place filters are mentioned across the entire codebase:**

### INTERFACE DEFINITION (1 place)
```
include/leveldb/filter_policy.h:43   CreateFilter() — the contract
include/leveldb/filter_policy.h:51   KeyMayMatch()  — the contract
include/leveldb/options.h:147        filter_policy = nullptr (default: no filter)
```

### IMPLEMENTATION (2 places — you replace bloom.cc)
```
util/bloom.cc:28   CreateFilter() — builds bit array
util/bloom.cc:56   KeyMayMatch()  — checks bit array
```
These are the lines your SuRFPolicy replaces.

### WRITE PATH (3 places)
```
table/filter_block.cc:70    policy_->CreateFilter()   ← called in GenerateFilter()
table/table_builder.cc:31   new FilterBlockBuilder()  ← filter created here
table/table_builder.cc:233  filter_policy->Name()     ← name stored in SSTable
```
Order: TableBuilder → FilterBlockBuilder → GenerateFilter() → CreateFilter()

### READ PATH — POINT QUERIES (4 places)
```
table/table.cc:83    filter_policy == nullptr  ← skip entirely if no filter
table/table.cc:102   filter_policy->Name()     ← verify name matches on open
table/table.cc:131   new FilterBlockReader()   ← load filter from disk
table/table.cc:225   filter->KeyMayMatch()     ← THE CHECK for point queries
```
Line 225 is the most important existing line. Your RangeMayMatch mirrors this.

### READ PATH — RANGE SCANS (YOUR NEW HOOK)
```
db/table_cache.cc — NewIterator()   ← RangeMayMatch check goes HERE
db/version_set.cc — AddIterators()  ← [lo, hi] range originates here
db/version_set.cc — GetFileIterator() ← [lo, hi] must be threaded through here
```
Currently NO filter check exists for range scans anywhere in the codebase.

### THE INTERNAL WRAPPER (easy to miss — very important)
```
db/dbformat.cc:101  InternalFilterPolicy::CreateFilter()
db/dbformat.cc:110  user_policy_->CreateFilter()          ← calls your code
db/dbformat.cc:113  InternalFilterPolicy::KeyMayMatch()
db/dbformat.cc:114  user_policy_->KeyMayMatch(ExtractUserKey(key))
```
LevelDB internally stores keys with extra bytes appended (sequence number + type).
Example: "cat" stored as "cat\x00\x00\x00\x01..."
InternalFilterPolicy STRIPS these extra bytes before calling your filter.
Your CreateFilter() and KeyMayMatch() receive CLEAN user keys.
Your RangeMayMatch() lo and hi will also be clean user keys — no stripping needed.

### TESTS (your safety net — all must still pass)
```
table/filter_block_test.cc — Tests KeyMayMatch at various block offsets
  Line 49:  KeyMayMatch(0,    "foo")     → true  (key IS there)
  Line 70:  KeyMayMatch(100,  "missing") → false (key NOT there)
  Line 100: KeyMayMatch(0,    "box")     → false (key in different block)

db/db_test.cc:1938 — End-to-end test with Bloom filter
util/bloom_test.cc — Unit tests for Bloom filter specifically
```
Your SuRF must pass all filter_block_test.cc tests.
When you write RangeMayMatch tests, follow the same pattern.

### C API WRAPPER
```
db/c.cc:114   CreateFilter() forwarded to user_policy_
db/c.cc:127   KeyMayMatch()  forwarded to user_policy_
db/c.cc:380   leveldb_options_set_filter_policy()
```
C API forwards all calls through the FilterPolicy interface automatically.
Your SuRFPolicy works through this wrapper without any changes needed.

### BENCHMARK
```
benchmarks/db_bench.cc:523  filter_policy_(NewBloomFilterPolicy(10))
benchmarks/db_bench.cc:815  options.filter_policy = filter_policy_
```
In Week 4 you change line 523 to:
  filter_policy_(NewSuRFFilterPolicy())
Run the same benchmark. That is your project result.

---

## THE COMPLETE CALL MAP

```
                    ┌─────────────────────────────────┐
                    │       filter_policy.h            │
                    │  Name()           [interface]    │
                    │  CreateFilter()   [interface]    │
                    │  KeyMayMatch()    [interface]    │
                    │  RangeMayMatch()  [YOU ADD]      │
                    └────────────┬────────────────────┘
                                 │ implemented by
               ┌─────────────────┴──────────────────┐
               │                                    │
          bloom.cc                          surf_filter.cc
       (you replace this)                   (you write this)
       CreateFilter()                       CreateFilter()
       KeyMayMatch()                        KeyMayMatch()
                                            RangeMayMatch() ← NEW

WRITE PATH:                        READ PATH — POINT QUERIES:
table_builder.cc                   table.cc line 225
  Add(key) → filter_block              filter->KeyMayMatch()
     → AddKey(key)                     Already works. No changes.
  Flush() → StartBlock(offset)
     → GenerateFilter()            READ PATH — RANGE SCANS:
        → CreateFilter(2KB keys)   table_cache.cc NewIterator()
                                       ← YOUR NEW HOOK
  Finish()                             RangeMayMatch(lo, hi)
     → filter_block->Finish()          if false → skip SSTable
        → CreateFilter(ALL keys)       Called from:
           (after your Week 2 fix)     version_set.cc AddIterators()
                                       → GetFileIterator()
                                       → TableCache::NewIterator()
                                          ← check goes HERE

INTERNAL WRAPPER:
db/dbformat.cc InternalFilterPolicy
  Strips internal key bytes before calling your filter
  Your code always receives clean user keys

TESTS:
table/filter_block_test.cc  ← KeyMayMatch tests, all must pass
db/db_test.cc:1938          ← End-to-end DB test with filter
util/bloom_test.cc          ← Bloom unit tests (unaffected)

BENCHMARK:
benchmarks/db_bench.cc:523  ← Change to NewSuRFFilterPolicy() in Week 4

THE ZERO THAT IS YOUR ENTIRE PROJECT:
grep RangeMayMatch = 0 results
You add it everywhere it needs to be.
```

---

## Summary — What Changes Week by Week

```
Week 2 — Implement SuRFPolicy:
  NEW FILE: util/surf_filter.cc
    SuRFPolicy::Name()          → "leveldb.SuRFFilter"
    SuRFPolicy::CreateFilter()  → build SuRF trie from sorted keys
    SuRFPolicy::KeyMayMatch()   → lookup exact key in SuRF trie
    SuRFPolicy::RangeMayMatch() → lookup key range in SuRF trie
  MODIFY: include/leveldb/filter_policy.h
    Add RangeMayMatch() with default return true
  MODIFY: table/filter_block.cc
    Stop calling GenerateFilter() at 2KB boundaries
    Buffer all keys, call CreateFilter() once in Finish()

Week 3 — Wire into range scan path:
  MODIFY: db/table_cache.cc
    Add RangeMayMatch check before FindTable() in NewIterator()
  MODIFY: db/version_set.cc
    Thread [lo, hi] range from AddIterators() down to GetFileIterator()
  MODIFY: table/two_level_iterator.cc
    Pass range query bounds to inner iterator creation

Week 4 — Benchmarking:
  MODIFY: benchmarks/db_bench.cc
    Change NewBloomFilterPolicy(10) to NewSuRFFilterPolicy()
  RUN: db_bench seekrandom, YCSB Workload E
  COMPARE: before vs after SuRF numbers
```
