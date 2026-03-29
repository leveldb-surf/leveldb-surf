#include <iostream>
#include "leveldb/db.h"

int main()
{
  // Open and write the keys
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* db;
  leveldb::DB::Open(options, "/tmp/mydb", &db);

  leveldb::WriteOptions wo;
  db ->Put(wo, "name", "Advanced DataBase Systems");
  db ->Put(wo, "University", "USC");
  db ->Put(wo, "animal:bear", "Large omnivore");
  db ->Put(wo, "animal:cat",  "Domestic feline");
  db ->Put(wo, "animal:dog",  "Domestic canine");
  db->Put(wo, "plant:oak",   "Hardwood tree");

  // ReadOptions - controls howthe read happens
  // We use all defaults , we need 3 parameters here : checksums,fill_cache and snapshots
  // ro.verify_checksums = false (default) — skip checksum verification
  // ro.fill_cache = true (default)        — cache reads in block cache
  // ro.snapshot = nullptr (default)       — read latest data

  leveldb::ReadOptions ro;

  //value is where Get() writes the result
  // starts with empty string and then Get() fills it with the value associated with the key
  std::string value;
  leveldb::Status s;

  // Get a key that EXISTS
  // Internally: checks Bloom filter, opens SSTable, finds key

  s = db->Get(ro, "animal:cat", &value);
  std::cout << "Get animal:cat   - " << s.ToString() << " | value = " << value << "\n";

  // Get another key that EXISTS
  s = db->Get(ro, "plant:oak", &value);
  std::cout << "Get plant:oak    - " << s.ToString() << " | value = " << value << "\n";

    // Get a key that does NOT exist
    // Bloom filter catches most missing keys without disk read
    // table.cc line 225: if (!filter->KeyMayMatch(...)) - skip SSTable
    s = db->Get(ro, "animal:zebra", &value);
    if (s.IsNotFound()) {
        std::cout << "Get animal:zebra → NotFound (never written)\n";
        std::cout << "  Bloom filter blocked the disk read\n";
    }

    // LevelDB keys are case sensitive
    // "Animal:cat" != "animal:cat"
    s = db->Get(ro, "Animal:cat", &value);
    if (s.IsNotFound()) {
        std::cout << "Get Animal:cat   → NotFound (wrong case)\n";
        std::cout << "  Keys are case sensitive\n";
    }

    // Overwrite a key then read it
    // Old value stays on disk until compaction
    // Get() always returns the LATEST value
    db->Put(wo, "animal:cat", "Cat: UPDATED");
    s = db->Get(ro, "animal:cat", &value);
    std::cout << "Get animal:cat (after update) → " << value << "\n";





    delete db;
    return 0;

}