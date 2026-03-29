# D4 — Deleting Keys with Delete()
# File: /workspace/project/demos/d04_delete.cc

---

## What This Demo Teaches
- How Delete() works — it does NOT remove data immediately
- What a tombstone record is
- Why deleting a non-existent key returns OK (not an error)
- Why the log file grows after Delete() just like after Put()
- The append-only nature of LevelDB
- How Get() knows a key is deleted even though bytes are still on disk

---

## No New Headers Needed

```cpp
#include "leveldb/db.h"
```
Same as D1, D2, D3. Delete() uses the same WriteOptions as Put().
No new includes required for this demo.

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;
    leveldb::ReadOptions ro;
    std::string value;
    leveldb::Status s;

    db->Put(wo, "animal:bear", "Large omnivore");
    db->Put(wo, "animal:cat",  "Domestic feline");
    db->Put(wo, "animal:dog",  "Domestic canine");
    std::cout << "Written: bear, cat, dog\n\n";

    db->Get(ro, "animal:bear", &value);
    std::cout << "Before delete: animal:bear = " << value << "\n";

    s = db->Delete(wo, "animal:bear");
    std::cout << "Delete animal:bear → " << s.ToString() << "\n";

    s = db->Get(ro, "animal:bear", &value);
    if (s.IsNotFound()) {
        std::cout << "After delete: animal:bear → NotFound\n";
        std::cout << "  Tombstone hides the value\n";
        std::cout << "  Old bytes still on disk until compaction\n\n";
    }

    db->Get(ro, "animal:cat", &value);
    std::cout << "animal:cat still exists: " << value << "\n";

    s = db->Delete(wo, "animal:zebra");
    std::cout << "\nDelete animal:zebra (never existed) → "
              << s.ToString() << "\n";
    std::cout << "Deleting non-existent key = OK, not an error\n";

    std::cout << "\nLog file after deletes:\n";
    system("ls -la /tmp/mydb/000003.log");

    delete db;
    return 0;
}
```

---

## The Delete() Method — Parameters

```cpp
s = db->Delete(wo, "animal:bear");
```

Parameter 1: wo (WriteOptions)
  Exact same WriteOptions as Put().
  wo.sync = false means tombstone goes to OS buffer first (fast).
  wo.sync = true means fsync() after write (safe, slow).
  Delete is treated exactly like a write — same options, same WAL path.

Parameter 2: "animal:bear" (const char* converted to Slice)
  The key to mark as deleted.
  Must match byte-for-byte including case.
  LevelDB does NOT check if this key exists before writing the tombstone.

Return value: leveldb::Status
  Always returns OK unless there is a serious IO error.
  Returns OK even if the key never existed.

Original files this connects to:
  db/db_impl.cc DBImpl::Delete()   receives the call
  db/log_writer.cc                  appends tombstone to WAL
  db/memtable.cc                    adds tombstone to MemTable

---

## C++ Concepts

### Nothing New in C++
D4 uses exactly the same C++ as D1-D3.
The learning here is all about LevelDB behaviour, not C++ syntax.
This is intentional — the concept of tombstones is the key insight.

---

## The Tombstone — The Most Important Concept in D4

Imagine a library book catalogue. When a book is removed from the library,
the librarian does NOT erase the card. Instead they stamp it "REMOVED."
Next time someone looks for that book, they see the REMOVED stamp and know
it is gone. Eventually during a big catalogue cleanup the old cards are
physically thrown away.

LevelDB works exactly the same way:

```
db->Delete(wo, "animal:bear")
    |
    v
Append to WAL (000003.log):
    Record type: kTypeDeletion   <- special type byte, not kTypeValue
    Key: "animal:bear"
    (no value field — tombstones have no value)
    |
    v
Add to MemTable:
    "animal:bear" → [TOMBSTONE]
    |
    v
Delete() returns OK immediately

When Get("animal:bear") is called later:
    Found TOMBSTONE in MemTable
    Return NotFound immediately
    Old bytes "Large omnivore" still exist in WAL
    But tombstone takes priority over old value

During compaction (background cleanup):
    Merges old value + tombstone
    Both get thrown away — neither written to new SSTable
    This is when data is actually removed from disk
```

---

## Why Delete Non-Existent Key Returns OK

```cpp
s = db->Delete(wo, "animal:zebra");
// s.ToString() = "OK"
```

This surprises everyone the first time. Here is why:

LevelDB does NOT check if the key exists before deleting.
Checking would require:
  1. Looking through the MemTable
  2. Possibly opening SSTables
  3. Running Bloom filter checks
  That is expensive — potentially a full disk read.

Instead LevelDB just writes a tombstone and returns OK immediately.
If the key never existed, the tombstone sits in the MemTable doing nothing.
During compaction it is thrown away with no matching value to clean up.
No harm done. Much faster.

The philosophy: writes are always fast. Checks are avoided.

---

## Output Explained

```
Written: bear, cat, dog
```
3 Put() calls. All 3 in MemTable and WAL.

```
Before delete: animal:bear = Large omnivore
```
Get() found the key before deletion. Confirmed it exists.

```
Delete animal:bear → OK
```
Tombstone written to WAL and MemTable. NOT physically removed.

```
After delete: animal:bear → NotFound
  Tombstone hides the value
  Old bytes still on disk until compaction
```
Get() sees the tombstone in MemTable first.
Returns NotFound. The bytes "Large omnivore" are still in 000003.log.
They will be cleaned up during compaction in D10.

```
animal:cat still exists: Domestic feline
```
Delete is precise. Only bear was deleted. cat and dog untouched.

```
Delete animal:zebra (never existed) → OK
Deleting non-existent key = OK, not an error
```
Tombstone written for zebra even though it was never Put().
LevelDB does not check existence. Just writes tombstone and moves on.
Tombstone cleaned up during compaction with no matching value.

```
-rw-r--r-- 1 root root 206 Mar 29 20:03 /tmp/mydb/000003.log
```
206 bytes = 3 Put records + 2 Delete tombstones + WAL record headers.
Log grew because Delete() appends to WAL exactly like Put().

---

## The WAL Contents After D4

```
000003.log contains 5 records:
  Record 1: kTypeValue   "animal:bear" → "Large omnivore"
  Record 2: kTypeValue   "animal:cat"  → "Domestic feline"
  Record 3: kTypeValue   "animal:dog"  → "Domestic canine"
  Record 4: kTypeDeletion "animal:bear" (tombstone, no value)
  Record 5: kTypeDeletion "animal:zebra" (tombstone, no value)

If process crashes right now and restarts:
  LevelDB reads all 5 records
  Replays them in order
  Final state: cat and dog exist, bear is deleted, zebra never existed
  Correct state restored from WAL alone
```

---

## LevelDB Is Append-Only — The Core Design

```
Put()    → append kTypeValue record to WAL + MemTable
Delete() → append kTypeDeletion record to WAL + MemTable
Update() → does not exist. Just Put() a new value.
           New value and old value coexist until compaction.

Nothing is ever modified in place.
Everything is always appended.
Old data cleaned up ONLY during compaction.

This design makes writes extremely fast.
Trade-off: reads must check for tombstones.
           compaction must merge versions.
```

---

## Connection to Your Project

During compaction — when your SuRF filter is built:
```
DoCompactionWork()
  merges old value + tombstone
  throws both away (key is deleted)
  only LIVE keys (not deleted) are written to new SSTable
  CreateFilter() called with only LIVE keys
```

Your SuRFPolicy::CreateFilter() receives only the current live keys —
deleted keys are already filtered out by compaction before your code runs.
You never need to handle tombstones in your filter implementation.

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d04_delete.cc \
    -o /workspace/project/demos/d04 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d04
```