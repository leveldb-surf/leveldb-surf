# D7 — Range Scan
# File: /workspace/project/demos/d07_range_scan.cc
# THE most important demo — this is the exact query your project optimizes

---

## What This Demo Teaches
- How Seek() positions the iterator at the first key >= target
- The prefix scan pattern — stop when prefix no longer matches
- The explicit [lo, hi] range scan — the query SuRF optimizes
- Why giraffe appeared in Range 3 (important correctness lesson)
- Cross-prefix range scans — lexicographic order handles naturally
- The compare() vs substr() difference — C++ string comparison
- The direct connection to your RangeMayMatch() implementation

---

## No New Headers
```cpp
#include "leveldb/db.h"
#include "leveldb/iterator.h"
```
Same as D6. Range scan uses the same iterator — just with Seek() instead of SeekToFirst().

---

## The New Method — it->Seek()

```cpp
it->Seek("animal:cat");
```
Positions the iterator at the FIRST key that is >= the given key.
The key does not need to exist.

Examples:
```
Database has: ant, bear, cat, dog, elephant, fox, giraffe

it->Seek("animal:cat")     → lands on animal:cat  (exists)
it->Seek("animal:chimp")   → lands on animal:dog  (chimp missing, next >= is dog)
it->Seek("animal:aardvark")→ lands on animal:ant  (aardvark missing, next >= is ant)
it->Seek("animal:zebra")   → Valid() = false       (nothing >= zebra)
```

Original file: table/two_level_iterator.cc
  Seeks in index block to find correct data block
  Then seeks within that data block for the exact key position

---

## The 4 Range Scan Patterns

### Pattern 1 — Prefix Scan
```cpp
it->Seek("animal:");
while (it->Valid()) {
    k = it->key().ToString();
    if (k.substr(0, 7) != "animal:") break;
    // process key
    it->Next();
}
```
Use when: "give me everything starting with X"
Stop condition: key no longer starts with the prefix
Works because: all animal: keys are consecutive in sorted order

### Pattern 2 — Explicit [lo, hi] Range
```cpp
lo = "animal:cat";
hi = "animal:fox";
it->Seek(lo);
while (it->Valid()) {
    k = it->key().ToString();
    if (k > hi) break;
    // process key
    it->Next();
}
```
Use when: "give me all keys between X and Y inclusive"
Stop condition: key exceeds the upper bound
THIS IS EXACTLY WHAT RangeMayMatch(lo, hi) answers

### Pattern 3 — Truly Empty Range
```cpp
lo = "animal:hippo";   // does not exist
hi = "animal:iguana";  // does not exist
it->Seek(lo);          // lands on animal:jaguar (if exists) or next
// if first key > hi → loop never executes → 0 results
```
Use to demonstrate: SuRF would return false for this SSTable
No keys in [hippo, iguana] → RangeMayMatch = false → skip entirely

### Pattern 4 — Cross-Prefix Range
```cpp
lo = "animal:fox";
hi = "plant:oak";
it->Seek(lo);
while (it->Valid()) {
    k = it->key().ToString();
    if (k > hi) break;
    // process key — works across prefix boundary naturally
    it->Next();
}
```

---

## C++ Concepts

### substr() for prefix check
```cpp
if (k.substr(0, 7) != "animal:") break;
```
substr(start, length) extracts a substring.
substr(0, 7) = first 7 characters of k.
Compare with "animal:" using != directly.
Simple, readable, correct.

### compare() — the bug and the fix
```cpp
// WRONG — compile error
if (k.compare(0, 7) != "animal:") break;
// Missing 3rd argument, compare() returns int not string

// CORRECT
if (k.compare(0, 7, "animal:") != 0) break;
```
compare(pos, len, str) returns:
  0     strings are equal
  < 0   k.substr(pos,len) comes before str
  > 0   k.substr(pos,len) comes after str
You must compare against 0, not against a string.

### String comparison with >
```cpp
if (k > hi) break;
```
For std::string, > compares lexicographically.
"animal:fox" > "animal:fox" is false (equal, not greater).
"animal:giraffe" > "animal:fox" is true (g > f).
"plant:cactus" > "animal:fox" is true (p > a).
This is how the upper bound check works naturally.

---

## Output Explained

### Range 1
```
animal:ant, animal:bear, animal:cat, animal:dog,
animal:elephant, animal:fox, animal:giraffe
Total: 7 keys
```
Seek("animal:") landed on animal:ant.
Loop stopped at plant:cactus (substr(0,7) != "animal:").
All 7 animal keys returned, zero plant keys.

### Range 2
```
animal:cat, animal:dog, animal:elephant, animal:fox
Found: 4 keys
```
Seek("animal:cat") landed exactly on cat.
Loop included cat, dog, elephant, fox — all <= "animal:fox".
elephant is included because:
  "animal:cat" < "animal:elephant" < "animal:fox"
  c < d < e < f — elephant is between cat and fox alphabetically.
Stopped at animal:giraffe because "animal:giraffe" > "animal:fox".

### Range 3 — The Important Lesson
```
animal:giraffe
Found: 1 keys
```
The range was ["animal:giraffe", "animal:hippo"].
animal:giraffe EXISTS and is:
  >= "animal:giraffe" (equal to lower bound)
  <= "animal:hippo" (giraffe < hippo alphabetically)
So giraffe IS correctly inside the range.

The comment said "empty range" — that was wrong.
A truly empty range would be ["animal:hippo", "animal:iguana"].
No keys exist between hippo and iguana.

CRITICAL LESSON FOR YOUR PROJECT:
RangeMayMatch("animal:giraffe", "animal:hippo") MUST return true.
giraffe actually exists in that range.
If SuRF returned false here → false negative → giraffe becomes invisible.
That is data loss. Never acceptable.

RangeMayMatch("animal:hippo", "animal:iguana") CAN return false.
No keys exist there. Safe to skip.

### Range 4
```
animal:fox, animal:giraffe, plant:cactus, plant:oak
Found: 4 keys
```
Range crossed the prefix boundary from animal: to plant:.
LevelDB handled it seamlessly — sorted order makes this natural.
fox (animal) → giraffe (animal) → cactus (plant) → oak (plant).
Stopped after oak because plant:oak = hi (equal to upper bound, included).

---

## The Range Scan — What Happens Inside LevelDB

```
it->Seek("animal:cat")
    |
    v
TwoLevelIterator::Seek()           [two_level_iterator.cc]
    |
    v
Outer: LevelFileNumIterator        [version_set.cc]
  Finds correct SSTable using index
    |
    v
Inner: GetFileIterator()           [version_set.cc]
  Opens the SSTable
    └── TableCache::NewIterator()  [table_cache.cc]
          ← YOUR RANGEMAYMATCH CHECK GOES HERE IN WEEK 3
          RangeMayMatch("animal:cat", "animal:fox")
          false → return empty iterator (skip SSTable)
          true  → FindTable() → open file → search
    |
    v
Table::NewIterator()               [table.cc]
  Iterates data blocks within SSTable
    |
    v
Keys returned in sorted order
```

Currently: no filter check at TableCache::NewIterator().
Every candidate SSTable opened even if it has no keys in [lo, hi].
Your Week 3 work adds RangeMayMatch at the ← line above.

---

## Why This Is Your Project's Core

Range scan without SuRF:
```
Query: [animal:cat, animal:fox]
Candidate SSTables: A, B, C, D, E (all overlap the range metadata)
LevelDB opens: ALL 5 SSTables
Finds keys in: only B and D
Wasted: opened A, C, E for nothing
```

Range scan with SuRF:
```
Query: [animal:cat, animal:fox]
RangeMayMatch("cat", "fox") on each SSTable:
  A: false → skip (0 disk reads)
  B: true  → open → finds keys
  C: false → skip (0 disk reads)
  D: true  → open → finds keys
  E: false → skip (0 disk reads)
Wasted: nothing
Saved: 3 unnecessary SSTable opens
```

The seekrandom baseline number (4.317 micros/op) will drop because
of exactly this saving — fewer SSTable opens per range query.

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d07_range_scan.cc \
    -o /workspace/project/demos/d07 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d07
```