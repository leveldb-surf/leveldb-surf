# D11 - Filter Policy Plug-in
# File: /workspace/project/demos/d11_filter_policy.cc
# This is the ONE line that changes in Week 2

---

## What This Demo Teaches
- How options.filter_policy wires your filter into LevelDB
- What filter_policy->Name() returns and why it must be consistent
- How KeyMayMatch() runs during Get() at table.cc line 225
- Why you must delete filter_policy AFTER delete db
- The exact one-line change that switches from Bloom to SuRF
- The complete plug-in interface you implement in Week 2

---

## New Header

```cpp
#include "leveldb/filter_policy.h"
```
File location: /workspace/leveldb/include/leveldb/filter_policy.h
This is the exact file you read in B1.
Gives you: NewBloomFilterPolicy() and the FilterPolicy interface.

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    options.write_buffer_size = 4096;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);

    std::cout << "Filter policy name: "
              << options.filter_policy->Name() << "\n";

    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;
    for (int i = 0; i < 100; i++) {
        std::string key   = "animal:" + std::to_string(i);
        std::string value = "value:"  + std::to_string(i);
        db->Put(wo, key, value);
    }

    db->CompactRange(nullptr, nullptr);

    leveldb::ReadOptions ro;
    std::string value;
    leveldb::Status s;

    s = db->Get(ro, "animal:42", &value);
    std::cout << s.ToString() << " = " << value << "\n";

    s = db->Get(ro, "animal:999", &value);
    std::cout << (s.IsNotFound() ? "NotFound" : value) << "\n";

    std::string sstables;
    db->GetProperty("leveldb.sstables", &sstables);
    std::cout << sstables;

    // ALWAYS delete filter_policy AFTER delete db
    delete db;
    delete options.filter_policy;

    return 0;
}
```

---

## The Most Important Line in Your Entire Project

```cpp
options.filter_policy = leveldb::NewBloomFilterPolicy(10);
```

This is the plug-in point. This one line controls which filter
LevelDB uses for ALL operations: writing, reading, range scans.

TODAY (Bloom filter):
```cpp
options.filter_policy = leveldb::NewBloomFilterPolicy(10);
```

WEEK 2 (your SuRF filter):
```cpp
options.filter_policy = NewSuRFFilterPolicy();
```

That is the ONLY user-visible change when your project is complete.
Everything else - compaction, SSTable writing, filter lookups,
range scans - happens automatically through the FilterPolicy interface.

---

## New Method - NewBloomFilterPolicy()

```cpp
leveldb::NewBloomFilterPolicy(10)
```
Factory function defined in util/bloom.cc.
Creates a BloomFilterPolicy object on the heap.
Returns a pointer to it as a FilterPolicy*.

Parameter: bits_per_key (int)
  How many bits to use per key in the filter.
  More bits = fewer false positives = more memory.

  10 bits per key: approximately 1% false positive rate
   8 bits per key: approximately 2% false positive rate
  14 bits per key: approximately 0.1% false positive rate

  The default for LevelDB is 10.
  The SuRF paper used similar settings for comparison.

You own this object. You must delete it.
Delete it AFTER delete db (not before).

---

## filter_policy->Name()

```cpp
options.filter_policy->Name()
// returns: "leveldb.BuiltinBloomFilter2"
```

Name() returns a string that is stored INSIDE every SSTable.
When LevelDB opens an SSTable to read it, it checks:
  Does the name stored in this SSTable match options.filter_policy->Name()?
  YES: load and use the filter
  NO:  ignore the filter entirely (fall back to opening everything)

This is why Name() must NEVER change between writes and reads.

Your SuRFPolicy::Name() must return "leveldb.SuRFFilter" consistently.
If you change it after writing SSTables, all existing SSTables
have their filters silently ignored. No error. Just slow reads.
This is one of the hardest bugs to track down.

---

## C++ Concepts

### Calling a method through a pointer
```cpp
options.filter_policy->Name()
```
filter_policy is a FilterPolicy* (pointer).
To call a method through a pointer use ->
Same pattern as db->Put(), db->Get(), it->Next().

### Deleting in the correct order
```cpp
delete db;                    // close database first
delete options.filter_policy; // then delete the filter
```
WRONG order:
```cpp
delete options.filter_policy; // filter deleted
delete db;                    // db tries to use deleted filter = crash
```
The database uses filter_policy internally until it is closed.
Always close the database first, then delete the filter.

---

## Output Explained

```
Filter policy name: leveldb.BuiltinBloomFilter2
```
This string is stored in every SSTable written by this database.
When you open the same database again, LevelDB verifies
the filter name matches. If you change to SuRF and open
old SSTables, their Bloom filters are ignored until
they are recompacted with SuRF.

```
Written 100 keys
Compaction done - filter block built inside SSTable
```
CompactRange() flushed MemTable and built the SSTable.
During compaction: BloomFilterPolicy::CreateFilter() was called
with all 100 keys. The resulting bit array is stored in
the filter block of 000005.ldb.
After Week 2: SuRFPolicy::CreateFilter() runs here instead.

```
Get animal:42 (exists):   OK = value:42
```
Get() found animal:42.
Internally at table.cc line 225:
  filter->KeyMayMatch(block_offset, "animal:42")
  BloomFilter hashed "animal:42" k times
  All k bit positions were ON
  Returned true
  LevelDB opened the data block and found the key.

```
Get animal:999 (missing): NotFound - Bloom filter blocked disk read
```
animal:999 was never written.
Internally at table.cc line 225:
  filter->KeyMayMatch(block_offset, "animal:999")
  BloomFilter hashed "animal:999" k times
  At least one bit position was OFF
  Returned false: definitely absent
  LevelDB skipped the data block entirely
  Returned NotFound without reading from disk.
This is the exact optimization you studied in B6.

```
 5:1149['animal:0' @ 1 : 1 .. 'animal:99' @ 100 : 1]
```
File 000005.ldb, 1149 bytes.
Smallest key: animal:0 at sequence 1.
Largest key: animal:99 at sequence 100.

Why animal:99 is the largest key:
  animal:0, animal:1, ..., animal:9, animal:10, ..., animal:99
  Lexicographic order: animal:9 < animal:99 because at position 8
  '9' > end-of-string so animal:99 comes after animal:9.

The 1149 bytes contain:
  Data blocks: the 100 key-value pairs (Snappy compressed)
  Filter block: the Bloom filter bit array built by CreateFilter()
  Index block: maps key ranges to data block locations
  Footer: points to index and filter

After Week 2 the filter block will contain your SuRF trie instead.

```
The filter block is INSIDE the .ldb file
After Week 2 it will contain SuRF instead of Bloom
```
The filter is NOT a separate file.
It is embedded inside the SSTable file.
FilterBlockBuilder::Finish() serializes it and
TableBuilder::Finish() writes it to the .ldb file.
You verified this in D10 when you watched the file appear.

```
Filter policy deleted cleanly
```
delete db closed the database.
delete options.filter_policy freed the BloomFilterPolicy object.
Correct order: db first, filter second.

---

## The Complete Plug-in Flow

```
options.filter_policy = NewBloomFilterPolicy(10)
    |
    v
DB::Open()
  Creates FilterBlockBuilder(filter_policy)    [table_builder.cc line 31]
    |
    v
Put() x 100 keys
  FilterBlockBuilder::AddKey(key)              [filter_block.cc]
    |
    v
CompactRange()
  FilterBlockBuilder::Finish()
    policy_->CreateFilter(all_keys, n, dst)    [filter_block.cc line 70]
         |
         v
    BloomFilterPolicy::CreateFilter()          [bloom.cc]
    (SuRFPolicy::CreateFilter() in Week 2)
  Filter bytes written to SSTable              [table_builder.cc]
    |
    v
Get("animal:999")
  filter->KeyMayMatch(offset, "animal:999")   [table.cc line 225]
       |
       v
  BloomFilterPolicy::KeyMayMatch()            [bloom.cc]
  Found 0-bit: return false
  Skip SSTable: no disk read
  Return NotFound
```

This entire flow is what you traced in Part B.
D11 shows it running in real life.

---

## What Changes in Week 2

In Week 2 you create util/surf_filter.cc with:

```cpp
class SuRFPolicy : public FilterPolicy {
public:
    const char* Name() const override {
        return "leveldb.SuRFFilter";        // stored in every SSTable
    }

    void CreateFilter(const Slice* keys, int n,
                      std::string* dst) const override {
        // Build SuRF trie from n sorted keys
        // Serialize to bytes
        // Append to dst
    }

    bool KeyMayMatch(const Slice& key,
                     const Slice& filter) const override {
        // Load SuRF from filter bytes
        // Check if key might exist
        // Return false only if definitely absent
    }

    bool RangeMayMatch(const Slice& lo, const Slice& hi,
                       const Slice& filter) const override {
        // Load SuRF from filter bytes
        // Check if any key in [lo, hi] might exist
        // Return false only if definitely no key in range
    }
};

const FilterPolicy* NewSuRFFilterPolicy() {
    return new SuRFPolicy();
}
```

Then in any program using this database:
```cpp
options.filter_policy = NewSuRFFilterPolicy();
// everything else stays exactly the same
```

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d11_filter_policy.cc \
    -o /workspace/project/demos/d11 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d11
```