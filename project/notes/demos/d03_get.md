# D3 — Reading Data with Get()
# File: /workspace/project/demos/d03_get.cc

---

## What This Demo Teaches
- How Get() reads an exact key by name (point query)
- What ReadOptions are and their default values
- How Status works for missing keys — IsNotFound()
- That LevelDB keys are case sensitive
- How overwriting a key works — Get() always returns latest version
- Why rm -rf /tmp/mydb means D2 values do not appear in D3
- How the Bloom filter connects to Get() — table.cc line 225

---

## Why D2 Values Do Not Appear Here

The run command is:
```
rm -rf /tmp/mydb && /workspace/project/demos/d03
```

rm -rf /tmp/mydb deletes the ENTIRE database before D3 runs.
D3 creates a fresh empty database and writes its own keys.
Each demo is self-contained — it writes exactly what it needs.

If you want D3 to see D2 data:
  Run D2 first WITHOUT rm -rf
  Then run D3 WITHOUT rm -rf
  Both share the same /tmp/mydb database

---

## The Headers

```cpp
#include "leveldb/db.h"
```
Same as D1 and D2. This one header gives you everything for D3:
- leveldb::DB           the database class
- leveldb::Options      settings for opening
- leveldb::ReadOptions  settings for reads (NEW in D3)
- leveldb::WriteOptions settings for writes
- leveldb::Status       error reporting

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
    db->Put(wo, "animal:bear", "Large omnivore");
    db->Put(wo, "animal:cat",  "Domestic feline");
    db->Put(wo, "animal:dog",  "Domestic canine");
    db->Put(wo, "plant:oak",   "Hardwood tree");

    leveldb::ReadOptions ro;
    std::string value;
    leveldb::Status s;

    s = db->Get(ro, "animal:cat", &value);
    std::cout << "Get animal:cat   → " << s.ToString()
              << " | value = " << value << "\n";

    s = db->Get(ro, "plant:oak", &value);
    std::cout << "Get plant:oak    → " << s.ToString()
              << " | value = " << value << "\n";

    s = db->Get(ro, "animal:zebra", &value);
    if (s.IsNotFound()) {
        std::cout << "Get animal:zebra → NotFound (never written)\n";
        std::cout << "  Bloom filter blocked the disk read\n";
    }

    s = db->Get(ro, "Animal:cat", &value);
    if (s.IsNotFound()) {
        std::cout << "Get Animal:cat   → NotFound (wrong case)\n";
        std::cout << "  Keys are case sensitive\n";
    }

    db->Put(wo, "animal:cat", "Cat: UPDATED");
    s = db->Get(ro, "animal:cat", &value);
    std::cout << "Get animal:cat (after update) → " << value << "\n";

    delete db;
    return 0;
}
```

---

## C++ Concepts

### ReadOptions — ro
```cpp
leveldb::ReadOptions ro;
```
A struct of settings that controls HOW reads happen.
Defined in: /workspace/leveldb/include/leveldb/options.h

Fields and defaults:
```
ro.verify_checksums = false   do not verify data integrity on every read
                               set true to catch disk corruption (slower)
ro.fill_cache = true          put read data into block cache
                               set false for large scans to avoid flooding cache
ro.snapshot = nullptr         read the latest data
                               set to a Snapshot object to read old data (D8)
```
For D3 we use all defaults — just declare ro and pass it.

### Get() — 3 Parameters
```cpp
s = db->Get(ro, "animal:cat", &value);
```

Parameter 1: ro (ReadOptions)
  How to perform this read. We use defaults.

Parameter 2: "animal:cat" (const char* converted to Slice)
  The exact key to look up.
  Must match byte-for-byte. Case sensitive.
  "animal:cat" != "Animal:cat" != "animal:Cat"

Parameter 3: &value (std::string*)
  Address of your string variable.
  Get() writes the found value here.
  If key not found, value is unchanged.
  & means address-of (same pattern as &db in D1)

Return value: leveldb::Status
  s.ok()          = key found, value written into your string
  s.IsNotFound()  = key does not exist, value unchanged
  s.IsCorruption()= data on disk is damaged

### std::string value
```cpp
std::string value;
```
C++ built-in string class. Starts empty.
Get() fills it if the key is found.
If IsNotFound(), value stays empty (whatever it was before).
Always check s.IsNotFound() before reading value.

### IsNotFound() — Not an Error
```cpp
if (s.IsNotFound()) { ... }
```
IsNotFound is NOT an error. It means:
  Operation succeeded. Key simply does not exist.

Three different states:
```
s.ok()            operation succeeded, key was found
s.IsNotFound()    operation succeeded, key does not exist
s.IsCorruption()  something broken on disk — real error
```

### Case Sensitivity
```cpp
db->Get(ro, "Animal:cat", &value);  // NotFound
db->Get(ro, "animal:cat", &value);  // OK
```
LevelDB compares keys as raw bytes.
'A' = ASCII 65, 'a' = ASCII 97. Different bytes = different key.
"Animal:cat" and "animal:cat" are completely different keys.

---

## Output Explained

```
Get animal:cat   → OK | value = Domestic feline
```
Key found. s.ToString() = "OK". value = "Domestic feline" written by Get().

```
Get plant:oak    → OK | value = Hardwood tree
```
Key found. Different prefix (plant: vs animal:) — LevelDB finds it correctly.

```
Get animal:zebra → NotFound (never written)
  Bloom filter blocked the disk read
```
Key never written. s.IsNotFound() = true.
Internally at table.cc line 225 (the line you read in B6):
  BloomHash("animal:zebra") → compute k bit positions
  At least one bit was 0 → definitely absent
  SSTable skipped entirely → no disk read
This is the Bloom filter optimization running in real life.

```
Get Animal:cat   → NotFound (wrong case)
  Keys are case sensitive
```
'A' (65) != 'a' (97). Different key. NotFound is correct.

```
Get animal:cat (after update) → Cat: UPDATED
```
You Put() a new value for the same key.
LevelDB appended a NEW record — the old "Domestic feline" still exists on disk.
But Get() always finds the LATEST version first.
Old record stays until compaction removes it.

---

## The Bloom Filter Connection — What Happened Internally

When you called Get("animal:zebra"):
```
Step 1: Check MemTable in RAM
        "animal:zebra" not in MemTable

Step 2: For each candidate SSTable:
        filter->KeyMayMatch("animal:zebra", bloom_filter_bytes)
           ↓
        table.cc line 225 (you read this in B6):
        if (!filter->KeyMayMatch(handle.offset(), k)) {
            // Not found — skip SSTable
        }
           ↓
        BloomHash("animal:zebra") = H
        delta = rotate H right by 17 bits
        Check k bit positions:
          H % bits → check → bit is 0 → DEFINITELY ABSENT
        Return false immediately
           ↓
        Skip SSTable entirely — ZERO disk reads

Step 3: Return Status::NotFound
```
This is the exact optimization that table.cc line 225 provides.
Your SuRF will do the same for ranges — RangeMayMatch() at table_cache.cc.

---

## The Read Path Visual

```
db->Get(ro, "animal:cat", &value)
    |
    v
Check MemTable (RAM)
    found? → return value immediately (fastest path)
    not found? → continue
    |
    v
For each SSTable that might have the key:
    |
    v
    filter->KeyMayMatch(block_offset, "animal:cat")  ← table.cc line 225
        Bloom says NO  → skip SSTable (no disk read saved!)
        Bloom says YES → continue to next step
    |
    v
    Open SSTable data block
    Binary search for "animal:cat"
    Found → write to value string → return OK
    Not found → try next SSTable
    |
    v
All SSTables checked, not found → return NotFound
```

---

## Connection to Your Project

table.cc line 225:
```cpp
if (!filter->KeyMayMatch(handle.offset(), k)) {
    // Not found — filter said definitely absent
}
```
This is the EXISTING optimization for point queries.
Your project adds an equivalent for range queries in table_cache.cc:
```cpp
if (!filter->RangeMayMatch(lo, hi, filter_data)) {
    return NewEmptyIterator();  // skip entire SSTable
}
```
D3 shows you the point query optimization working in real life.
Your Week 3 work adds the range query equivalent.

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d03_get.cc \
    -o /workspace/project/demos/d03 \
    -lleveldb -lpthread -lsnappy

# Run fresh
rm -rf /tmp/mydb && /workspace/project/demos/d03

# Run using D2 data (no fresh start)
/workspace/project/demos/d03
```