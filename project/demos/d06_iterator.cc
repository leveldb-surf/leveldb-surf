#include <iostream>
#include "leveldb/db.h"
#include "leveldb/iterator.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;

    // Write keys in RANDOM order
    // LevelDB will sort them internally
    db->Put(wo, "animal:fox",     "Clever omnivore");
    db->Put(wo, "animal:bear",    "Large omnivore");
    db->Put(wo, "plant:oak",      "Hardwood tree");
    db->Put(wo, "animal:cat",     "Domestic feline");
    db->Put(wo, "sensor:10:00",   "temp=22.1");
    db->Put(wo, "animal:dog",     "Domestic canine");
    db->Put(wo, "plant:cactus",   "Desert plant");
    db->Put(wo, "sensor:10:05",   "temp=22.3");

    std::cout << "Written 8 keys in random order\n";
    std::cout << "LevelDB stores them sorted\n\n";
    // LevelDB keys are sorted lexicographically, so they will be ordered as:

    // Create an iterator
    // ro.fill_cache = false for large scans
    // avoids flooding block cache with sequential data
    leveldb::ReadOptions ro;
    ro.fill_cache = false;

    leveldb::Iterator* it = db->NewIterator(ro);

    // SeekToFirst - jumps to the smallest key
    // then walk and continue with Next untill valid() is false
    std::cout << "Iterating with SeekToFirst() and Next():\n";
    std::cout <<"all keys insorted order:\n";
    int count = 0;

    for (it->SeekToFirst(); it->Valid(); it->Next()) {
                std::cout << "  [" << ++count << "] "
                  << it->key().ToString()
                  << " = "
                  << it->value().ToString() << "\n";
    }

    // Always check iterator status after the loop
    // If IO error occurred mid-scan it->status() will be non-ok
    if (!it->status().ok()) {
        std::cout << "Iterator error: " << it->status().ToString() << "\n";
    }

        // Always delete the iterator — it holds resources
    delete it;

    std::cout << "\nTotal keys: " << count << "\n";
    std::cout << "Notice: sorted lexicographically\n";
    std::cout << "animal: < plant: < sensor: because a < p < s\n";
    std::cout << "Within animal: bear < cat < dog < fox\n";

    delete db;
    return 0;

    }

