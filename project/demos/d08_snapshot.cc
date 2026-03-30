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

    // Write initial data
    db->Put(wo, "animal:bear", "Large omnivore");
    db->Put(wo, "animal:cat",  "Domestic feline");
    db->Put(wo, "animal:dog",  "Domestic canine");
    std::cout << "Written: bear, cat, dog\n\n";

    // Take a snapshot — freeze the database state right now
    // snap records the current sequence number internally
    // All reads through snap will see: bear, cat, dog
    // No matter what happens to the database after this line

    const leveldb::Snapshot* snap = db->GetSnapshot();
    std::cout << "Snapshot taken\n\n";

    // Now write MORE data and DELETE a key AFTER the snapshot
    db->Put(wo, "animal:elephant", "Largest land animal");
    db->Put(wo, "animal:fox",      "Clever omnivore");
    db->Delete(wo, "animal:bear");
    std::cout << "After snapshot: added elephant+fox, deleted bear\n\n";

        // Read WITHOUT snapshot — sees latest state
    // bear deleted, elephant and fox exist
    leveldb::ReadOptions ro_latest;
    s = db->Get(ro_latest, "animal:bear", &value);
    std::cout << "WITHOUT snapshot:\n";
    std::cout << "  animal:bear     - " << (s.IsNotFound() ? "NotFound (deleted)" : value) << "\n";
    s = db->Get(ro_latest, "animal:elephant", &value);
    std::cout << "  animal:elephant - " << value << "\n\n";

    // Read WITH snapshot — sees frozen state (bear exists, elephant does not)
    leveldb::ReadOptions ro_snap;
    ro_snap.snapshot = snap;
    s = db->Get(ro_snap, "animal:bear", &value);
    std::cout << "WITH snapshot (frozen at bear+cat+dog):\n";
    std::cout << "  animal:bear     - " << value << " (still alive in snapshot)\n";
    s = db->Get(ro_snap, "animal:elephant", &value);
    std::cout << "  animal:elephant - " << (s.IsNotFound() ? "NotFound (written after snapshot)" : value) << "\n\n";

    // Iterator through snapshot - only sees bear, cat, dog
    std::cout << "Iterator THROUGH snapshot:\n";
    leveldb::Iterator* it = db->NewIterator(ro_snap);
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::cout << "  " << it->key().ToString() << "\n";
    }
    delete it;

    std::cout << "\nIterator WITHOUT snapshot (latest):\n";
    it = db->NewIterator(ro_latest);
    for (it->SeekToFirst(); it->Valid(); it->Next()) {
        std::cout << "  " << it->key().ToString() << "\n";
    }
    delete it;

    // ALWAYS release the snapshot when done
    // Holding it prevents compaction from cleaning old versions
    db->ReleaseSnapshot(snap);
    std::cout << "\nSnapshot released\n";

    delete db;
    return 0;

  }