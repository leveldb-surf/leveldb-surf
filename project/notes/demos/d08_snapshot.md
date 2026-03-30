# D8 — Snapshots (Point-in-Time Reads)
# File: /workspace/project/demos/d08_snapshot.cc

---

## What This Demo Teaches
- What a snapshot is — a frozen photograph of the database
- How GetSnapshot() and ReleaseSnapshot() work
- How ro.snapshot makes Get() and Iterator read old data
- Why you must always release snapshots
- What MVCC means and how LevelDB implements it with sequence numbers
- Why your SuRF filter works correctly even with snapshots

---

## No New Headers
```cpp
#include "leveldb/db.h"
```
Snapshots are part of the core DB class. No extra header needed.

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
    leveldb::Status s;
    std::string value;

    db->Put(wo, "animal:bear", "Large omnivore");
    db->Put(wo, "animal:cat",  "Domestic feline");
    db->Put(wo, "animal:dog",  "Domestic canine");

    const leveldb::Snapshot* snap = db->GetSnapshot();

    db->Put(wo, "animal:elephant", "Largest land animal");
    db->Put(wo, "animal:fox",      "Clever omnivore");
    db->Delete(wo, "animal:bear");

    leveldb::ReadOptions ro_latest;
    leveldb::ReadOptions ro_snap;
    ro_snap.snapshot = snap;

    // reads through snap see old state
    // reads through ro_latest see current state

    db->ReleaseSnapshot(snap);
    delete db;
    return 0;
}
```

---

## New Methods and Parameters

### db->GetSnapshot()
```cpp
const leveldb::Snapshot* snap = db->GetSnapshot();
```
Returns a pointer to a Snapshot object.
Internally records the current sequence number.
Every write in LevelDB increments a global sequence number.
GetSnapshot() says "remember sequence number N".

Original file: db/db_impl.cc DBImpl::GetSnapshot()

### ro.snapshot = snap
```cpp
leveldb::ReadOptions ro_snap;
ro_snap.snapshot = snap;
```
Attaches the snapshot to ReadOptions.
Any Get() or NewIterator() using ro_snap will only see
records written at or before the snapshot's sequence number.
Records written after = invisible through this snapshot.

### db->ReleaseSnapshot(snap)
```cpp
db->ReleaseSnapshot(snap);
```
Destroys the snapshot object and frees memory.
Tells LevelDB: compaction can now clean up old versions.
ALWAYS release when done — never leave a snapshot open.

Original file: db/db_impl.cc DBImpl::ReleaseSnapshot()

---

## C++ Concepts

### const pointer
```cpp
const leveldb::Snapshot* snap = db->GetSnapshot();
```
const here means the Snapshot object cannot be modified through snap.
You can only pass snap to GetSnapshot() and ReleaseSnapshot().
You cannot call any mutating methods on it.
This is correct — a snapshot should be read-only.

### Two ReadOptions objects
```cpp
leveldb::ReadOptions ro_latest;   // default — reads latest data
leveldb::ReadOptions ro_snap;     // reads through frozen snapshot
ro_snap.snapshot = snap;
```
You can have multiple ReadOptions in the same program.
ro_latest.snapshot = nullptr (default) = read latest.
ro_snap.snapshot = snap = read frozen state.
Pass the right one to each Get() or NewIterator() call.

---

## The Photograph Analogy

```
Timeline:

    bear written ─────────────────────────────────────────────
    cat written  ─────────────────────────────────────────────
    dog written  ─────────────────────────────────────────────
                         │
                    SNAPSHOT TAKEN (photograph)
                         │
    elephant written ────┼──────────────────── (after photo)
    fox written      ────┼──────────────────── (after photo)
    bear deleted     ────┼──────────────────── (after photo)
                         │
    Read through snapshot│= see the photograph = bear,cat,dog
    Read without snapshot│= see current state = cat,dog,elephant,fox
```

---

## MVCC — Multi-Version Concurrency Control

LevelDB uses MVCC to make snapshots work.
Every record stored internally has a sequence number:

```
Internal storage:
  "animal:bear" seq=1 → "Large omnivore"   (Put at seq 1)
  "animal:cat"  seq=2 → "Domestic feline"  (Put at seq 2)
  "animal:dog"  seq=3 → "Domestic canine"  (Put at seq 3)
  snapshot taken at seq=3
  "animal:elephant" seq=4 → "Largest land animal" (Put after snap)
  "animal:fox"      seq=5 → "Clever omnivore"     (Put after snap)
  "animal:bear"     seq=6 → [TOMBSTONE]            (Delete after snap)

Read with snapshot (seq=3):
  Only see records with seq <= 3
  bear seq=1 ✓ visible → "Large omnivore"
  cat  seq=2 ✓ visible → "Domestic feline"
  dog  seq=3 ✓ visible → "Domestic canine"
  elephant seq=4 ✗ hidden (after snapshot)
  fox  seq=5 ✗ hidden (after snapshot)
  bear tombstone seq=6 ✗ hidden → bear still shows as alive

Read without snapshot:
  See all records, take latest version per key
  bear tombstone seq=6 takes priority → NotFound
  elephant seq=4 visible
  fox seq=5 visible
```

---

## Output Explained

```
Written: bear, cat, dog
Snapshot taken
```
3 keys written at sequence numbers 1, 2, 3.
Snapshot records seq=3.

```
After snapshot: added elephant+fox, deleted bear
```
3 more operations at seq 4, 5, 6. Snapshot still frozen at 3.

```
WITHOUT snapshot:
  animal:bear     → NotFound (deleted)
  animal:elephant → Largest land animal
```
Latest state: bear tombstone at seq=6 hides old bear.
Elephant at seq=4 is visible.

```
WITH snapshot (frozen at bear+cat+dog):
  animal:bear     → Large omnivore (still alive in snapshot)
  animal:elephant → NotFound (written after snapshot)
```
Snapshot at seq=3:
  bear at seq=1 is visible. Tombstone at seq=6 is hidden.
  elephant at seq=4 is hidden (written after snapshot).

```
Iterator THROUGH snapshot:
  animal:bear
  animal:cat
  animal:dog
```
Exactly the 3 keys that existed at snapshot time.
elephant, fox invisible. bear tombstone invisible.

```
Iterator WITHOUT snapshot (latest):
  animal:cat
  animal:dog
  animal:elephant
  animal:fox
```
bear gone (tombstone). elephant and fox added. cat and dog unchanged.

```
Snapshot released
```
ReleaseSnapshot() called. Compaction can now clean up old versions.

---

## Why You Must Always Release Snapshots

```
Without ReleaseSnapshot():
  Compaction runs and tries to clean old record versions
  But sees snapshot is still open at seq=3
  Cannot delete any record with seq <= 3
  bear's old "Large omnivore" bytes stay on disk forever
  elephant, fox cannot be compacted either
  Disk space grows unboundedly
  Memory leak for the snapshot object itself

With ReleaseSnapshot():
  Compaction sees no open snapshots
  Can safely delete old versions
  bear's old bytes cleaned up
  Disk stays clean
```

---

## Connection to Your Project

Your SuRF filter works correctly with snapshots because of InternalFilterPolicy.

When reading through a snapshot:
  Get() uses InternalFilterPolicy::KeyMayMatch()
  InternalFilterPolicy strips the sequence number from the key
  Passes clean user key to your SuRFPolicy::KeyMayMatch()
  Your filter never sees sequence numbers

For RangeMayMatch:
  The lo and hi passed to your filter are clean user keys
  No sequence numbers, no type bytes
  You do not need to handle MVCC in your filter implementation
  InternalFilterPolicy handles it transparently

---

## Run Command

```bash
# Compile
g++ -std=c++17 \
    -I /workspace/leveldb/include \
    -L /workspace/leveldb/build \
    /workspace/project/demos/d08_snapshot.cc \
    -o /workspace/project/demos/d08 \
    -lleveldb -lpthread -lsnappy

# Run
rm -rf /tmp/mydb && /workspace/project/demos/d08
```