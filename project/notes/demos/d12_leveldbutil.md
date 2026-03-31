# D12 - Inspecting SSTables with leveldbutil
# No .cc file - this demo uses the leveldbutil command line tool

---

## What This Demo Teaches
- What leveldbutil is and where it lives
- How to dump the contents of an SSTable file
- How to read the leveldbutil output - key, sequence number, type, value
- Why leveldbutil expects a .ldb file path not a folder path
- How to scan a database with an iterator as an alternative
- How this connects to your Week 2 debugging workflow
- How to verify your SuRF filter was written correctly

---

## No .cc File for This Demo

D12 uses two tools:
1. leveldbutil: a command line binary that ships with LevelDB
2. A small C++ scan program written in /tmp (not saved to project)

leveldbutil location: /workspace/leveldb/build/leveldbutil
This was compiled when you built LevelDB in the Docker image.

---

## What leveldbutil Is

leveldbutil is LevelDB's own SSTable inspection tool.
It opens a .ldb file and prints every record inside it in
human-readable form. Written by the LevelDB authors at Google.

Use cases:
  Verify your filter was written correctly into the SSTable
  Debug compaction issues
  Inspect what keys are in a specific SSTable file
  Check sequence numbers and record types

Original file: leveldbutil.cc in the LevelDB source
It calls Table::Open() and walks through keys using an iterator.
This is the exact same code path as your range scans.

---

## Important: File Path Not Folder Path

leveldbutil expects a path to a specific .ldb SSTable file.
It does NOT accept the database folder path.

WRONG:
```
leveldbutil dump /tmp/d12db/
Result: Invalid argument: /tmp/d12db/: unknown file type
```

CORRECT:
```
leveldbutil dump /tmp/d12db/000005.ldb
Result: all keys printed
```

How to find the .ldb filename:
```bash
ls -la /tmp/d12db/*.ldb
```
Then use the filename you see in the dump command.

---

## The Commands

### Step 1: See what SSTable files exist
```bash
ls -la /tmp/d12db/
```
Look for files ending in .ldb.
In this demo: 000005.ldb (351 bytes).

### Step 2: Dump the SSTable file
```bash
/workspace/leveldb/build/leveldbutil dump /tmp/d12db/000005.ldb
```
Prints every record inside the SSTable in sorted key order.

### Step 3: Scan database with iterator (alternative)
```bash
# Compile and run a small C++ scan program
# Used to show range scans with the full database
```

---

## Output Explained - leveldbutil dump

```
'animal:bear' @ 1 : val => 'Large omnivore'
'animal:cat' @ 2 : val => 'Domestic feline'
'animal:dog' @ 3 : val => 'Domestic canine'
'animal:elephant' @ 4 : val => 'Largest land animal'
'animal:fox' @ 5 : val => 'Clever omnivore'
'plant:cactus' @ 6 : val => 'Desert plant'
'plant:oak' @ 7 : val => 'Hardwood tree'
```

Each line has 4 parts. Using animal:bear as the example:

```
'animal:bear'   @   1   :   val   =>   'Large omnivore'
     |              |         |              |
  the key      sequence    record        the value
               number      type
```

The key: 'animal:bear'
  The actual user key you Put() into the database.
  Quoted with single quotes by leveldbutil for display.

The sequence number: @ 1
  LevelDB's global write counter.
  animal:bear was the 1st write to this database (seq=1).
  animal:cat was the 2nd write (seq=2). And so on.
  Sequence numbers are how snapshots work (D8).
  A snapshot at seq=3 sees only records with seq <= 3.

The record type: val
  val means kTypeValue - a real value record.
  del means kTypeDeletion - a tombstone record.
  In this output all records are val because compaction
  already removed the tombstones (temp:a, temp:b, temp:c from D10).

The value: 'Large omnivore'
  The actual value you stored.

### The sorted order
```
animal:bear
animal:cat
animal:dog
animal:elephant
animal:fox
plant:cactus
plant:oak
```
Keys are in lexicographic sorted order.
This is exactly how they are stored in the data blocks.
animal: < plant: because 'a' < 'p'.
Within animal: alphabetical order.

### Why only 7 records, not more
We wrote exactly 7 keys and they all fit in one SSTable.
Compaction merged everything into this single file.
The filter block inside contains the Bloom filter for all 7 keys.
After Week 2: the filter block will contain the SuRF trie instead.

---

## Output Explained - Iterator Scan

```
All keys in database:
  animal:bear = Large omnivore
  animal:cat = Domestic feline
  animal:dog = Domestic canine
  animal:elephant = Largest land animal
  animal:fox = Clever omnivore
  plant:cactus = Desert plant
  plant:oak = Hardwood tree
```
Same 7 keys, same sorted order.
This uses the normal iterator path (two_level_iterator.cc).
Same result as leveldbutil but through the full LevelDB API.

```
Range scan [animal:cat, animal:fox]:
  animal:cat = Domestic feline
  animal:dog = Domestic canine
  animal:elephant = Largest land animal
  animal:fox = Clever omnivore
```
Seek("animal:cat") landed on cat.
Loop stopped after fox.
elephant included because "animal:cat" < "animal:elephant" < "animal:fox".
This is the same range scan as D7.

---

## The SSTable File Structure - What is Inside 000005.ldb

leveldbutil showed you the key-value records.
But the .ldb file contains more than just keys and values.

```
000005.ldb (351 bytes) contains:
  Data blocks:
    key-value pairs compressed with Snappy
    animal:bear=Large omnivore, ..., plant:oak=Hardwood tree
    This is what leveldbutil dump showed you

  Filter block:
    The Bloom filter bit array built by CreateFilter()
    Built during CompactRange() in the setup program
    Used by table.cc line 225 for KeyMayMatch() during Get()
    AFTER WEEK 2: this will be your SuRF trie instead

  Index block:
    Maps key ranges to data block byte offsets
    Used to find the right data block during Seek()

  Metaindex block:
    Maps "filter.leveldb.BuiltinBloomFilter2" to filter block location
    Name must match options.filter_policy->Name() at read time

  Footer (48 bytes):
    Points to index block and metaindex block
    Always at the end of the file
    Always the same size
    LevelDB reads this first when opening the file
```

leveldbutil only shows you the data block contents.
The filter block, index block, and footer are also there
but leveldbutil does not print them directly.

---

## How to Use This in Week 2 for Debugging

After you implement SuRFPolicy::CreateFilter() in Week 2:

Step 1: Write keys and compact:
```cpp
options.filter_policy = NewSuRFFilterPolicy();
db->Put(...); // write some keys
db->CompactRange(nullptr, nullptr); // builds SuRF filter
```

Step 2: Check the SSTable file exists and has reasonable size:
```bash
ls -la /tmp/mydb/*.ldb
```
If the file is much larger than with Bloom filter: SuRF is using too much space.
If the file is the same tiny size as before: SuRF may not have been written.

Step 3: Dump the SSTable to verify keys were written correctly:
```bash
/workspace/leveldb/build/leveldbutil dump /tmp/mydb/000005.ldb
```
You should see all your keys in sorted order.
If keys are missing: CreateFilter() may have lost some.

Step 4: Test Get() for existing and missing keys:
```cpp
db->Get(ro, existing_key, &value);   // should return OK
db->Get(ro, missing_key,  &value);   // should return NotFound
```
If an existing key returns NotFound: false negative in your filter = data loss.
Fix this first before anything else.

Step 5: Run all LevelDB tests:
```bash
bash /workspace/benchmarks/rebuild.sh
```
All 210 tests must still pass after your changes.

---

## Connection to Your Project - The Full Circle

D12 closes the loop on everything you learned in Part B and Part D:

```
B1 filter_policy.h:  You read the interface
D11 filter_policy:   You saw it plug in via options.filter_policy

B2 bloom.cc:         You read CreateFilter() and KeyMayMatch()
D11:                 You saw KeyMayMatch() run at table.cc line 225

B3/B4 filter_block:  You read the 2KB problem
D10:                 You saw CompactRange() trigger CreateFilter()

B5 table_builder:    You read how filter is written during compaction
D10/D11:             You watched the .ldb file appear with filter inside

B6 table.cc:         You read InternalGet() line 225
D11:                 You saw NotFound prove Bloom filter blocked disk read

B7 version_set:      You read AddIterators() and FileMetaData
D7:                  You saw Seek() and range scan working

B8 table_cache:      You read NewIterator() as your hook point
D7/D12:              You saw the iterator walking SSTables

B9 grep:             You found RangeMayMatch = 0 results
Week 2:              You add RangeMayMatch to surf_filter.cc
```

leveldbutil is the tool that verifies your Week 2 work is correct.
Run it after compaction to confirm your SuRF filter was built.

---

## Run Commands

```bash
# See what SSTable files exist
ls -la /tmp/d12db/

# Dump contents of SSTable file (use actual filename from ls output)
/workspace/leveldb/build/leveldbutil dump /tmp/d12db/000005.ldb

# The --from and --to flags do NOT work in this version of leveldbutil
# Use a C++ iterator scan for range-specific output instead
```