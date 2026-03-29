# D2 — Writing Data with Put()
# File: /workspace/project/demos/d02_put.cc

---

## What This Demo Teaches
- How Put() writes a key-value pair
- What WriteOptions are and what sync means
- What a Slice is (LevelDB's way of passing strings)
- Where the data goes after Put() — WAL first, then MemTable
- Why .ldb SSTable files still do not appear yet
- Why the log file grew from 0 bytes (D1) to 227 bytes (D2)

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"         // DB, Options, Status

int main() {
    // Open the database (same as D1)
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    // WriteOptions controls HOW each write happens
    leveldb::WriteOptions wo;
    wo.sync = false;            // explained below

    leveldb::Status s;

    // Put(WriteOptions, key, value)
    // Both key and value are automatically converted to Slice
    s = db->Put(wo, "name",         "Dhrish");
    std::cout << "Put name:        " << s.ToString() << "\n";

    s = db->Put(wo, "university",   "USC");
    std::cout << "Put university:  " << s.ToString() << "\n";

    s = db->Put(wo, "animal:bear",  "Large omnivore");
    std::cout << "Put animal:bear: " << s.ToString() << "\n";

    s = db->Put(wo, "animal:cat",   "Domestic feline");
    std::cout << "Put animal:cat:  " << s.ToString() << "\n";

    s = db->Put(wo, "animal:dog",   "Domestic canine");
    std::cout << "Put animal:dog:  " << s.ToString() << "\n";

    std::cout << "\nFiles after writing:\n";
    system("ls -la /tmp/mydb/");

    delete db;
    return 0;
}
```

---

## The Headers — What Each One Gives You

```cpp
#include "leveldb/db.h"
```
This one header gives you everything for basic operations:
- leveldb::DB          the database class
- leveldb::Options     settings for opening
- leveldb::Status      error reporting
- leveldb::Slice       LevelDB's string type

As you add more features in later demos you add more headers:
```
"leveldb/write_batch.h"   for D5 WriteBatch
"leveldb/iterator.h"      for D6 Iterator
"leveldb/filter_policy.h" for D11 filter plug-in
```

---

## C++ Concepts

### Arrow Operator — db->Put()
```cpp
s = db->Put(wo, "name", "Dhrish");
```
`db` is a pointer (holds a memory address, not the object itself).
To call a method on a pointer you use `->` not `.`
- `.` is for objects:  `options.create_if_missing = true`
- `->` is for pointers: `db->Put(...)` means "follow the pointer, then call Put"

Think of it like:
- options is the actual house     → options.door opens the door
- db is the address on a map      → db->door follows the map to the house, opens door

### WriteOptions — wo
```cpp
leveldb::WriteOptions wo;
wo.sync = false;
```
`WriteOptions` is a struct with settings for a single write operation.

sync = false (default):
- Write goes to OS memory buffer first
- OS decides when to actually write to physical disk
- Very fast — microseconds
- Small risk: if power cuts between write and OS flush, data lost
- Fine for benchmarking and most applications

sync = true:
- Calls fsync() after every write
- Forces OS to immediately write to physical disk
- Safe — survives power cuts
- Very slow — ~100x slower than sync=false
- Only use when you cannot afford to lose even one write

For your project: always use sync=false for benchmarking.
The SuRF paper also used sync=false for fair comparison.

### Slice — LevelDB's String Type
When you write:
```cpp
db->Put(wo, "animal:bear", "Large omnivore");
```
The strings "animal:bear" and "Large omnivore" are automatically
converted to `leveldb::Slice` objects. A Slice is simply:
```
struct Slice {
    const char* data;   // pointer to the actual bytes
    size_t size;        // how many bytes
};
```
It is NOT a copy of the string — just a pointer + length.
This is fast because LevelDB never copies string data unnecessarily.

Important: Slice does not own the memory it points to.
The string "animal:bear" lives in your program's memory.
The Slice just points to it. If the original string is deleted,
the Slice becomes a dangling pointer (bug). For demos this is fine
because string literals live for the entire program lifetime.

### Why Prefixed Keys — animal:bear, animal:cat
LevelDB sorts ALL keys together lexicographically.
Using a prefix like "animal:" groups related keys together on disk.
```
Sorted order:
  animal:bear
  animal:cat
  animal:dog
  name
  university
```
a < n < u in ASCII so all "animal:" keys come first.
This prefix design enables efficient range scans:
  Seek("animal:") → scan forward until key no longer starts with "animal:"
  Only reads the "animal:" section, skips "name" and "university" entirely.
This is exactly the range scan pattern your project optimizes.

---

## What Happens When You Call Put()

Think of it like sending a letter with two copies:

```
db->Put(wo, "animal:bear", "Large omnivore")
    |
    v
Step 1: Write to WAL (000003.log) first
        Binary record appended to the log file
        This is crash safety — if program dies here,
        record can be recovered from the log on restart
    |
    v
Step 2: Write to MemTable (in memory)
        The key-value pair goes into a sorted in-memory buffer
        Very fast — just a memory write
        NOT on disk yet as an SSTable
    |
    v
    Put() returns Status::OK()

Later (when MemTable fills up to 4MB):
    MemTable frozen → background thread flushes to SSTable (.ldb file)
    WAL entries for flushed data become obsolete → old log deleted
```

---

## Output Explained

```
Put name:        OK
Put university:  OK
Put animal:bear: OK
Put animal:cat:  OK
Put animal:dog:  OK
```
Every Put() returned Status::OK(). s.ToString() prints "OK" for success.

```
000003.log    227 bytes   (was 0 bytes in D1)
```
Grew from 0 to 227 bytes because we wrote 5 key-value pairs.
Each Put() appended a binary record to the WAL.
227 bytes = 5 records × (key + value + LevelDB record header overhead).

```
CURRENT       16 bytes    (unchanged from D1)
LOCK           0 bytes    (unchanged)
LOG          147 bytes    (unchanged)
MANIFEST-000002 50 bytes  (unchanged)
```
No compaction happened. No new SSTables created.
MANIFEST only changes when SSTables are created or deleted.

```
No .ldb files
```
The MemTable default size is 4MB.
5 small keys (a few hundred bytes) barely touches the buffer.
Buffer not full = no flush = no SSTable yet.
.ldb files appear in D10 when we force compaction.

---

## The Write Path Visual

```
db->Put("animal:bear", "Large omnivore")
           |
           v
    WAL (000003.log)          <- written FIRST, crash safety
    [record appended]         <- 227 bytes after 5 puts
           |
           v
    MemTable (RAM)            <- sorted in-memory buffer
    animal:bear → Large omnivore
    animal:cat  → Domestic feline
    animal:dog  → Domestic canine
    name        → Dhrish
    university  → USC
           |
           | (when MemTable hits 4MB)
           v
    SSTable (.ldb file)       <- NOT YET, buffer not full
    [compaction thread]       <- background, you don't control it
    [filter built here]       <- CreateFilter() called here
                                 YOUR SuRFPolicy runs here in Week 2
```

---

## Connection to Your Project

The line in the write path:
```
[filter built here] <- CreateFilter() called during compaction
```
When MemTable flushes to an SSTable, FilterBlockBuilder::AddKey() is
called for every key, then FilterBlockBuilder::Finish() calls
policy_->CreateFilter() with ALL the keys.

In Week 2 you implement SuRFPolicy::CreateFilter() which receives
exactly these keys — the same ones you just Put() here in D2.

---

## Run Command

```bash
# File is already at /workspace/project/demos/d02_put.cc

# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d02_put.cc \
    -o /workspace/project/demos/d02 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d02
```