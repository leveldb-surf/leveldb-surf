# D1 — Open and Close a Database
# First demo. Everything starts here.

---

## What This Demo Teaches
- How to open a LevelDB database
- What Options are and why they matter
- What files LevelDB creates on disk and what each one does
- The Status object — how LevelDB reports success or failure
- Why you must always delete the db pointer at the end
- Stack vs Heap — the most important C++ concept for this project

---

## The Code

```cpp
#include <iostream>
#include "leveldb/db.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;

    leveldb::DB* db;
    leveldb::Status s = leveldb::DB::Open(options, "/tmp/mydb", &db);

    if (!s.ok()) {
        std::cout << "ERROR: " << s.ToString() << "\n";
        return 1;
    }
    std::cout << "Database opened at /tmp/mydb\n";

    std::cout << "\nFiles created by LevelDB:\n";
    system("ls -la /tmp/mydb/");

    delete db;
    std::cout << "\nDatabase closed cleanly\n";
    return 0;
}
```

---

## C++ Concepts Explained

### Namespaces — leveldb::
```cpp
leveldb::Options options;
leveldb::DB* db;
```
The `leveldb::` prefix is a namespace. Think of it like a last name.
If two libraries both have a class called `Options`, namespaces prevent collision.
`leveldb::Options` = the Options class inside the leveldb namespace.
Everything in LevelDB lives inside the `leveldb::` namespace.

---

### Struct — Options
```cpp
leveldb::Options options;
options.create_if_missing = true;
```
A struct is a collection of settings grouped together.
Imagine a form with checkboxes — you fill in what you need, leave rest as default.
`options.create_if_missing = true` means:
- If /tmp/mydb folder does not exist, create it
- If false and folder does not exist, Open() returns an error

Other useful Options fields (you will use these later):
```
options.write_buffer_size   how much RAM before flushing to SSTable (default 4MB)
options.block_cache         how much RAM for caching read data (default 8MB)
options.filter_policy       YOUR PLUG-IN POINT — Bloom or SuRF goes here
options.compression         Snappy compression on/off (default on)
```

---

### Pointer — DB*
```cpp
leveldb::DB* db;
```
The `*` means pointer. A pointer is a variable that holds a memory address.
`db` itself is only 8 bytes — it just stores the address of where the DB object lives.
The actual DB object is created by Open() on the heap.

---

### Stack vs Heap — THE most important C++ concept

Imagine two storage areas:

```
STACK                              HEAP
like a notepad on your desk        like a warehouse
automatic — written and erased     manual — you put things in
  when function starts/ends          and you must take them out
small and fast                     large and slower
Options options; lives here        DB object lives here
                                   created by DB::Open()
                                   destroyed by delete db
```

```cpp
leveldb::Options options;      // stack — automatic
leveldb::DB* db;               // pointer on stack (8 bytes)
DB::Open(..., &db);            // DB object created on HEAP
delete db;                     // YOU delete it from heap
```

If you forget `delete db`:
- The heap memory leaks (wasted until program exits)
- The LOCK file stays locked
- Next time you run the program: "IO error: LOCK already held"

---

### Static Method — DB::Open
```cpp
leveldb::Status s = leveldb::DB::Open(options, "/tmp/mydb", &db);
```
`DB::Open` is a static method. You call it on the CLASS, not on an object.
Normal method: `db->SomeMethod()`   — called on an existing object
Static method: `DB::Open()`         — called on the class to CREATE an object

The `&db` part:
`&` means "address of". We pass the address of our pointer variable.
Open() then writes the new DB object's address into that location.
After the call, `db` holds the address of the newly created database.
This is how C++ functions return multiple things — pass address of result variable.

---

### Status Object — Error Handling
```cpp
leveldb::Status s = leveldb::DB::Open(options, "/tmp/mydb", &db);
if (!s.ok()) {
    std::cout << "ERROR: " << s.ToString() << "\n";
    return 1;
}
```
LevelDB NEVER throws exceptions. It returns a Status from every operation.
Three important Status checks:
```
s.ok()           true = success
s.IsNotFound()   true = key does not exist (not an error, just absent)
s.IsCorruption() true = data on disk is damaged
s.ToString()     human readable message for any status
```
Always check status. Ignoring it means silent failures.

---

### delete db — Always Required
```cpp
delete db;
```
`delete` frees heap memory and runs the object's destructor.
LevelDB's destructor does three critical things:
1. Flushes pending writes from the write buffer to disk
2. Releases the LOCK file so another process can open the database
3. Frees all memory — block cache, filter, index blocks

RULE: Every `new` or `DB::Open()` must have a matching `delete`.

---

## Output Explained

```
Database opened at /tmp/mydb
```
Open() succeeded. s.ok() returned true.

```
total 20
drwxr-xr-x  .
drwxrwxrwt  ..
-rw-r--r--  000003.log       0 bytes
-rw-r--r--  CURRENT         16 bytes
-rw-r--r--  LOCK             0 bytes
-rw-r--r--  LOG            147 bytes
-rw-r--r--  MANIFEST-000002 50 bytes
```

000003.log — 0 bytes
- Write-Ahead Log (WAL). Every Put() goes here BEFORE going to SSTable.
- Purpose: crash recovery. If program dies, LevelDB reads this on restart.
- Empty (0 bytes) because we wrote zero keys.
- Number 000003 = LevelDB's global file sequence counter (starts at 1, increments).

CURRENT — 16 bytes
- Contains exactly one line: "MANIFEST-000002\n"
- Always points to the active MANIFEST file.
- LevelDB reads CURRENT first on every startup to find the right MANIFEST.

LOCK — 0 bytes
- An empty file. Its job is to be locked, not to store data.
- LevelDB calls flock() on it when the database opens.
- If another process tries to open same database, flock() fails = safe.
- Released when you delete db.

LOG — 147 bytes
- Human readable text log. Not the WAL.
- Run: cat /tmp/mydb/LOG  to see the startup messages inside.
- More entries added as writes and compaction happen.
- Useful for debugging.

MANIFEST-000002 — 50 bytes
- Tracks which SSTable .ldb files currently exist.
- Records the key range (smallest, largest) each SSTable covers.
- Updated every compaction.

No .ldb files
- SSTables only appear after writes + memory buffer flush.
- We wrote zero keys so nothing to flush.
- .ldb files appear in D2 (after puts) and clearly in D10 (after compaction).

```
Database closed cleanly
```
delete db ran successfully. Flush, unlock, free all done.

---

## The Mental Model

```
Your Program
    |
    | DB::Open(options, "/tmp/mydb", &db)
    v
LevelDB Object on Heap:
    ├── MemTable          <- write buffer, Put() writes land here first
    ├── Block Cache       <- recently read data, fast repeat access
    ├── Filter            <- Bloom or SuRF (loaded when SSTable opens)
    └── Index Blocks      <- table of contents for each open SSTable

On Disk /tmp/mydb/:
    ├── 000003.log        <- WAL, crash recovery
    ├── MANIFEST-000002   <- which SSTables exist + their key ranges
    ├── CURRENT           <- which MANIFEST to use
    ├── LOCK              <- prevents double-open
    ├── LOG               <- human readable events
    └── *.ldb             <- SSTables (empty now, appear after writes)
```

---

## Connection to Your Project

```cpp
options.filter_policy = NewSuRFFilterPolicy();   // Week 2
```

This one line in Options is the ONLY user-visible change when you finish the project.
D1 shows you exactly where that plug-in point is.
Everything else — compaction, SSTable writing, filter lookups — happens
automatically through the FilterPolicy interface you studied in Part B.

---

## Run Command

```bash
# Create the file
cat > /workspace/project/demos/d01_open_close.cc << 'EOF'
[paste code here]
EOF

# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d01_open_close.cc \
    -o /workspace/project/demos/d01 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d01
```
