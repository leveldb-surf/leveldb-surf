# D9 - GetProperty (Internal Database Stats)
# File: /workspace/project/demos/d09_getproperty.cc

---

## What This Demo Teaches
- How to query LevelDB internal stats with GetProperty()
- What the stats table means - levels, files, sizes, compaction
- How to read the SSTable listing - file number, size, key range
- Why 100 keys showed 0 files first (MemTable not full)
- How write_buffer_size controls when SSTables are created
- What each level in the LSM-tree is for
- How this connects to your RangeMayMatch() implementation

---

## No New Headers
```cpp
#include "leveldb/db.h"
```
GetProperty() is part of the core DB class. No extra header needed.

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    options.write_buffer_size = 4096;   // 4KB buffer forces flush quickly

    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;

    for (int i = 0; i < 100; i++) {
        std::string key   = "key:" + std::to_string(i);
        std::string value = "value:" + std::to_string(i);
        db->Put(wo, key, value);
    }
    std::cout << "Written 100 keys\n\n";

    std::string stats;
    bool ok = db->GetProperty("leveldb.stats", &stats);
    std::cout << "leveldb.stats:\n";
    if (ok) std::cout << stats << "\n";

    std::string sstables;
    ok = db->GetProperty("leveldb.sstables", &sstables);
    std::cout << "leveldb.sstables:\n";
    if (ok) std::cout << sstables << "\n";

    std::cout << "Files per level:\n";
    for (int level = 0; level < 7; level++) {
        std::string prop = "leveldb.num-files-at-level"
                           + std::to_string(level);
        std::string result;
        ok = db->GetProperty(prop, &result);
        if (ok) {
            std::cout << "  Level " << level
                      << ": " << result << " files\n";
        }
    }

    std::string mem;
    ok = db->GetProperty("leveldb.approximate-memory-usage", &mem);
    std::cout << "\nApproximate memory usage: " << mem << " bytes\n";

    delete db;
    return 0;
}
```

---

## New Method - db->GetProperty()

```cpp
std::string stats;
bool ok = db->GetProperty("leveldb.stats", &stats);
```

Parameter 1: property name (const char* converted to Slice)
  Must be one of the valid property names listed below.
  Unknown name returns false and leaves stats unchanged.

Parameter 2: &stats (std::string*)
  Address of your string. Result written here.
  Same & pattern as Get() and DB::Open().

Return value: bool
  true  : property exists, result written to stats
  false : unknown property name, stats unchanged

Original file: db/db_impl.cc DBImpl::GetProperty()

Valid property names:
```
"leveldb.stats"                   : full stats table
"leveldb.sstables"                : SSTable listing per level
"leveldb.num-files-at-levelN"     : file count at level N (0-6)
"leveldb.approximate-memory-usage": RAM used by caches
```

---

## C++ Concepts

### std::to_string()
```cpp
std::string key = "key:" + std::to_string(i);
```
to_string() converts an integer to a string.
i=0  : key = "key:0"
i=42 : key = "key:42"
i=99 : key = "key:99"

String + operator concatenates two strings.
"key:" + "42" = "key:42"

### for loop with int
```cpp
for (int i = 0; i < 100; i++) {
```
Standard C++ for loop.
int i = 0  : start at 0
i < 100    : run while i is less than 100 (so 0 to 99)
i++        : increment i by 1 each iteration
Total iterations: 100 (i=0 through i=99)

### bool return value
```cpp
bool ok = db->GetProperty("leveldb.stats", &stats);
if (ok) { ... }
```
bool is a type that holds either true or false.
GetProperty returns true if the property name is valid.
Always check the return value before reading stats.

---

## Why 100 Keys Showed 0 Files First (The MemTable Problem)

First attempt without write_buffer_size change:
```
100 keys x ~20 bytes each = ~2000 bytes total data
Default write_buffer_size = 4MB = 4,000,000 bytes
Buffer filled: 2000 / 4,000,000 = 0.05%
No flush happened
No SSTables created
All data still in RAM (MemTable)
Result: 0 files at all levels
```

After adding options.write_buffer_size = 4096 (4KB):
```
4096 bytes buffer
100 keys x ~20 bytes = ~2000 bytes
Buffer filled: 2000 / 4096 = 48%
LevelDB flushes when buffer reaches limit
After closing (delete db): final flush happens
Result: 1 SSTable file at Level 0
```

The lesson: SSTables only exist on disk after the MemTable overflows
OR when the database is closed and flushed. This is why D1 through D8
showed no .ldb files - the buffer never filled up.

---

## Output Explained - Every Part

### The Stats Table
```
                               Compactions
Level  Files Size(MB) Time(sec) Read(MB) Write(MB)
--------------------------------------------------
  0        1        0         0        0         0
```

Level 0: 1 file
  One SSTable file exists at Level 0.
  Level 0 is where fresh flushes from MemTable land first.
  Files at Level 0 can overlap each other (unlike Levels 1-6).
  When Level 0 accumulates 4 files, compaction merges them into Level 1.

Size(MB): 0
  The file is less than 1MB so it rounds to 0.
  Our 100 keys with small values are tiny.

Time(sec), Read(MB), Write(MB): all 0
  No compaction has happened yet.
  These would show non-zero values after background compaction runs.

Levels 1-6: not shown
  Empty levels are not printed in the stats table.
  All levels 1-6 have 0 files.

### The SSTable Listing
```
--- level 0 ---
 5:968['key:0' @ 1 : 1 .. 'key:99' @ 100 : 1]
```

This one line tells you everything about the SSTable file.

5
  The file number. The actual filename on disk is 000005.ldb.
  LevelDB uses a global counter for all files.
  Previous numbers (1,2,3,4) were used for LOG, MANIFEST, LOCK, etc.

968
  File size in bytes. The SSTable is 968 bytes on disk.
  Contains 100 key-value pairs compressed with Snappy.
  Also contains: filter block (Bloom filter), index block, footer.

'key:0' @ 1 : 1
  The smallest key in this SSTable.
  key:0    : the actual key
  @ 1      : sequence number 1 (the first write to this database)
  : 1      : type 1 = kTypeValue (a real value, not a tombstone)

'key:99' @ 100 : 1
  The largest key in this SSTable.
  key:99   : the actual key (note: key:99 > key:9 lexicographically)
  @ 100    : sequence number 100 (the 100th write)
  : 1      : type 1 = kTypeValue

So this SSTable:
  File: 000005.ldb
  Size: 968 bytes
  Contains: ALL 100 keys from key:0 to key:99
  Smallest key: key:0
  Largest key:  key:99

### Why key:99 is the largest (not key:9)
```
Lexicographic sort of "key:N":
  key:0
  key:1
  key:10   <- comes after key:1, before key:2
  key:11
  ...
  key:19
  key:2
  key:20
  ...
  key:9
  key:90
  key:91
  ...
  key:99
```
"key:99" is the lexicographically largest because
'9' > all other digits and "key:99" > "key:9" because
at position 5, '9' > end-of-string.

This is why zero-padding matters for numeric keys:
  "key:099" < "key:100" (correct numeric order)
  "key:99"  > "key:100" (wrong - lexicographic order)

### Files Per Level
```
Level 0: 1 files
Level 1: 0 files
Level 2: 0 files
Level 3: 0 files
Level 4: 0 files
Level 5: 0 files
Level 6: 0 files
```
All 100 keys in one SSTable at Level 0.
Levels 1-6 empty because no compaction has run yet.

### Memory Usage
```
Approximate memory usage: 8208 bytes
```
8208 bytes = about 8KB of RAM used by LevelDB.
This includes:
  Block cache: stores recently read data blocks
  Filter: the Bloom filter loaded from the SSTable
  Index: the SSTable index block loaded in memory
Very small because our dataset is tiny and no block cache was configured.

---

## The 7 Levels Explained

LevelDB has 7 levels (0 through 6):

```
Level 0: fresh SSTables from MemTable flush
         files CAN overlap each other
         compaction triggered when >= 4 files
         YOUR filter checked most often here

Level 1: after first compaction from Level 0
         files do NOT overlap (sorted, non-overlapping)
         maximum size: 10MB

Level 2: after compaction from Level 1
         maximum size: 100MB

Level 3: maximum size: 1GB

Level 4: maximum size: 10GB

Level 5: maximum size: 100GB

Level 6: oldest, largest data
         maximum size: 1TB
         once data reaches here it stays here
```

For range scans:
  LevelDB checks SSTables at ALL levels that overlap the query range.
  A query might check: 2 files at Level 0 + 1 at Level 1 + 1 at Level 2.
  Each file check = one RangeMayMatch() call.
  Each false from RangeMayMatch = one saved SSTable open = saved disk I/O.

---

## Connection to Your Project

The SSTable listing showed:
```
 5:968['key:0' @ 1 : 1 .. 'key:99' @ 100 : 1]
```

The smallest and largest keys (key:0 and key:99) are the FileMetaData
that version_set.cc uses for the coarse filter check you read in B7.

Your SuRF filter is built from ALL keys in this SSTable (key:0 through key:99).
When a range query [key:50, key:60] arrives:
  Coarse check: does [key:0, key:99] overlap [key:50, key:60]? YES -> pass
  SuRF check: RangeMayMatch("key:50", "key:60") -> true (keys exist here)
  Open SSTable -> scan -> return matching keys

When a range query [key:aaa, key:zzz] arrives (no such keys exist):
  Coarse check: does [key:0, key:99] overlap [key:aaa, key:zzz]? YES -> pass
  SuRF check: RangeMayMatch("key:aaa", "key:zzz") -> false (no keys there)
  Skip SSTable -> zero disk reads -> fast

The 968-byte file on disk contains your Bloom filter block.
After Week 2, it will contain your SuRF filter block instead.
GetProperty lets you verify SSTables were created and filters were built.

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d09_getproperty.cc \
    -o /workspace/project/demos/d09 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d09
```