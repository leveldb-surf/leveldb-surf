# D6 — Iterator (Full Scan)
# File: /workspace/project/demos/d06_iterator.cc

---

## What This Demo Teaches
- How to walk through ALL keys in sorted order
- The 4 iterator methods: SeekToFirst, Valid, Next, key/value
- That LevelDB sorts keys lexicographically regardless of insert order
- Why ro.fill_cache = false matters for large scans
- Why you must always delete the iterator and check its status
- How the timestamp prefix pattern enables time-range queries
- The direct connection to two_level_iterator.cc from Part B

---

## New Header

```cpp
#include "leveldb/iterator.h"
```
File location: /workspace/leveldb/include/leveldb/iterator.h
Gives you: leveldb::Iterator class
The abstract base class for all iterators in LevelDB.

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"
#include "leveldb/iterator.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;
    db->Put(wo, "animal:fox",   "Clever omnivore");
    db->Put(wo, "animal:bear",  "Large omnivore");
    db->Put(wo, "plant:oak",    "Hardwood tree");
    db->Put(wo, "animal:cat",   "Domestic feline");
    db->Put(wo, "sensor:10:00", "temp=22.1");
    db->Put(wo, "animal:dog",   "Domestic canine");
    db->Put(wo, "plant:cactus", "Desert plant");
    db->Put(wo, "sensor:10:05", "temp=22.3");

    leveldb::ReadOptions ro;
    ro.fill_cache = false;     // explained below

    leveldb::Iterator* it = db->NewIterator(ro);

    int count = 0;
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::cout << "  [" << ++count << "] "
                  << it->key().ToString()
                  << " = "
                  << it->value().ToString() << "\n";
    }

    if (!it->status().ok()) {
        std::cout << "Error: " << it->status().ToString() << "\n";
    }

    delete it;     // ALWAYS delete the iterator

    delete db;
    return 0;
}
```

---

## New Method — db->NewIterator()

```cpp
leveldb::Iterator* it = db->NewIterator(ro);
```
Returns a pointer to an Iterator object created on the heap.
The Iterator is a TwoLevelIterator (from table/two_level_iterator.cc).
You must delete it when done — it holds file handles and memory.

Parameter: ro (ReadOptions)
  ro.fill_cache = false    do not put scanned blocks into block cache
                           for full scans this prevents flooding the cache
                           with data that will not be read again soon
  ro.snapshot = nullptr    read the latest data (default)

Original file: db/db_impl.cc DBImpl::NewIterator()
  Creates a MergingIterator over all levels
  Each level has a TwoLevelIterator (from table/two_level_iterator.cc)
  This is the EXACT code path your RangeMayMatch hook sits inside

---

## The 4 Iterator Methods

### SeekToFirst()
```cpp
it->SeekToFirst();
```
Positions the iterator at the SMALLEST key in the database.
Internally: seeks to beginning of Level 0, then merges with other levels.
O(log n) — uses the index blocks to jump directly to the start.

### Valid()
```cpp
it->Valid()
```
Returns true if the iterator currently points to a real key.
Returns false when you have walked past the last key.
Always check Valid() before calling key() or value().
Calling key() or value() when !Valid() = undefined behaviour (crash or garbage).

### Next()
```cpp
it->Next();
```
Advances to the next key in sorted order.
Internally: advances within current data block, or opens the next data block.
If crossing SSTable boundary: opens the next SSTable.
THIS is where your RangeMayMatch check will skip SSTables in Week 3.

### key() and value()
```cpp
it->key().ToString()    // returns current key as std::string
it->value().ToString()  // returns current value as std::string
```
Both return Slice objects (pointer + length, no copy).
Call .ToString() to get a std::string copy you can safely store.
key() and value() are only valid while the iterator has not advanced.

---

## The For Loop Pattern — Standard Iterator Usage

```cpp
for (it->SeekToFirst(); it->Valid(); it->Next()) {
    // use it->key() and it->value() here
}
```
This is the standard pattern for full scans in LevelDB.
Equivalent to:
```cpp
it->SeekToFirst();
while (it->Valid()) {
    // use key and value
    it->Next();
}
```
Both are correct. The for loop is more compact.

---

## Always Check Status After the Loop

```cpp
if (!it->status().ok()) {
    std::cout << "Error: " << it->status().ToString() << "\n";
}
```
Why: if an IO error occurs mid-scan (disk read fails), the iterator
stops and records the error in its status. The loop ends normally
(Valid() returns false) but the scan is incomplete.
Without this check you would silently return partial results.
Always check it->status() after any iterator loop.

---

## Always Delete the Iterator

```cpp
delete it;
```
The iterator holds:
  - File handles to open SSTables
  - Memory for data blocks
  - Position state
If you do not delete it, these resources leak.
Rule: every db->NewIterator() must have a matching delete it.

---

## C++ Concepts

### Pointer to Iterator
```cpp
leveldb::Iterator* it = db->NewIterator(ro);
```
Same pattern as leveldb::DB* db — pointer to heap object.
db->NewIterator() creates the object, you must delete it.

### ++count in the Loop
```cpp
std::cout << "[" << ++count << "]"
```
++count means increment THEN use. So first iteration prints [1].
count++ would mean use THEN increment — first would print [0].
Pre-increment (++count) is the correct form for numbering from 1.

### Slice::ToString()
```cpp
it->key().ToString()
```
key() returns a Slice — just a pointer and length, no copy.
.ToString() makes a std::string copy — safe to store and print.
Always call .ToString() when you need the string value.

---

## Output Explained

```
Written 8 keys in random order
LevelDB stores them sorted
```
Keys written: fox, bear, oak, cat, 10:00, dog, cactus, 10:05.
Completely random order. LevelDB sorted in MemTable immediately.

```
[1] animal:bear = Large omnivore
[2] animal:cat  = Domestic feline
[3] animal:dog  = Domestic canine
[4] animal:fox  = Clever omnivore
[5] plant:cactus = Desert plant
[6] plant:oak   = Hardwood tree
[7] sensor:10:00 = temp=22.1
[8] sensor:10:05 = temp=22.3
```

Three levels of sorting:

Level 1 — prefix order:
  animal: before plant: before sensor:
  'a'=97 < 'p'=112 < 's'=115 in ASCII
  LevelDB compares byte by byte from left to right

Level 2 — within animal: prefix:
  bear < cat < dog < fox
  'b'=98 < 'c'=99 < 'd'=100 < 'f'=102

Level 3 — sensor timestamps:
  10:00 < 10:05
  Time order works because ISO timestamp format sorts lexicographically
  "10:00" < "10:05" because '0' < '5' at position 4
  This is exactly why time-series systems use LevelDB with timestamp keys

```
Total keys: 8
```
All 8 keys visited exactly once. Iterator walked every key in order.

---

## Lexicographic Sort — How LevelDB Compares Keys

LevelDB compares keys byte by byte from left to right.
The first byte that differs determines the order.

```
"animal:bear" vs "animal:cat":
  a=a, n=n, i=i, m=m, a=a, l=l, :=:, b vs c
  'b'(98) < 'c'(99) → "animal:bear" comes first

"animal:fox" vs "plant:cactus":
  a vs p → 'a'(97) < 'p'(112) → "animal:fox" comes first
```

This is identical to dictionary order for English words.
Numbers sort differently:
```
"sensor:9" would come AFTER "sensor:10" because '9'(57) > '1'(49)
This is why you must zero-pad numbers: "sensor:09" < "sensor:10"
```

---

## The Iterator Stack — Connection to Your Project

When you called db->NewIterator():
```
db->NewIterator()                          [db_impl.cc]
    └── creates MergingIterator
          └── for each level:
                TwoLevelIterator           [two_level_iterator.cc]
                  outer: LevelFileNumIterator   [version_set.cc]
                    walks the list of SSTables
                  inner: GetFileIterator        [version_set.cc]
                    opens one SSTable when outer lands on it
                    └── TableCache::NewIterator()  [table_cache.cc]
                          └── Table::NewIterator() [table.cc]
```

YOUR HOOK IN WEEK 3:
Between GetFileIterator and TableCache::NewIterator:
```
if RangeMayMatch(lo, hi) == false
    return empty iterator  ← skip this SSTable
```

D6 shows you this iterator working in real life.
Your project makes it smarter by skipping SSTables at the inner step.

---

## The Time-Series Pattern

```
sensor:10:00 = temp=22.1
sensor:10:05 = temp=22.3
```
This is the YCSB Workload E pattern your benchmark uses.
Range query: "all readings between 10:00 and 10:30"
  it->Seek("sensor:10:00")
  while key <= "sensor:10:30": process, it->Next()

Timestamps in ISO format sort correctly as strings.
SuRF's RangeMayMatch("sensor:10:00", "sensor:10:30") will skip
SSTables that have no readings in that time window.
That is the direct practical benefit of your project.

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d06_iterator.cc \
    -o /workspace/project/demos/d06 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d06
```