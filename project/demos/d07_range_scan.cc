#include <iostream>
#include "leveldb/db.h"
#include "leveldb/iterator.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;

    // Write a realistic dataset
    db->Put(wo, "animal:ant",      "Small insect");
    db->Put(wo, "animal:bear",     "Large omnivore");
    db->Put(wo, "animal:cat",      "Domestic feline");
    db->Put(wo, "animal:dog",      "Domestic canine");
    db->Put(wo, "animal:elephant", "Largest land animal");
    db->Put(wo, "animal:fox",      "Clever omnivore");
    db->Put(wo, "animal:giraffe",  "Tallest animal");
    db->Put(wo, "plant:cactus",    "Desert plant");
    db->Put(wo, "plant:oak",       "Hardwood tree");

    leveldb::ReadOptions ro;
    ro.fill_cache = false;
    leveldb::Iterator* it;
    std::string lo,hi,k;
    int count;

    // Range scan ------------------------ Prefix SCAN
    // Seek jumps directly to first key >= "animal:"
    // Stop when prefix no longer matches
    std::cout << "------------------------ Range 1: all keys with prefix 'animal:' ------------------------\n";
    it = db->NewIterator(ro);
    it->Seek("animal:");
    count = 0;
    while (it->Valid())
    {
        k = it->key().ToString();
        if (k.compare(0, 7, "animal:") != 0) break;
        // if (k.substr(0, 7) != "animal:") break;
         // Prefix no longer matches
        std::cout << " "<<k<<"\n";
        count++;
        it->Next();
    }
    std::cout << "Total: "<<count<<" keys\n";
    delete it;

    // Range scan ------------------------ Explicit SCAN [lo,hi)]
    // "give me all animals between cat and fox inclusive"
    // THIS IS EXACTLY WHAT RangeMayMatch(lo, hi) answers
    std::cout << "------------------------ Range 2: [animal:cat, animal:fox] ------------------------ \n";
    std::cout << "   This is what RangeMayMatch(lo,hi) optimizes\n";
    it = db->NewIterator(ro);
    lo = "animal:cat";
    hi = "animal:fox";
    it->Seek(lo);
    count = 0;
    while (it->Valid()) {
        k = it->key().ToString();
        if (k > hi) break;
        std::cout << "  " << k << " = " << it->value().ToString() << "\n";
        count++;
        it->Next();
    }
    std::cout << "  Found: " << count << " keys\n\n";
    delete it;

    // ------------------------ RANGE SCAN 3: empty range ------------------------
    // No keys exist between giraffe and hippo
    // Without SuRF: LevelDB still opens SSTables to check
    // With SuRF: RangeMayMatch returns false -> skip entirely
    std::cout << "------------------------ Range 3: [animal:giraffe, animal:hippo] ------------------------\n";
    std::cout << "   Empty range - no keys exist here\n";
    std::cout << "   SuRF skips SSTables with RangeMayMatch=false\n";
    it = db->NewIterator(ro);
    lo = "animal:giraffe";
    hi = "animal:hippo";
    it->Seek(lo);
    count = 0;
    while (it->Valid()) {
        k = it->key().ToString();
        if (k > hi) break;
        std::cout << "  " << k << "\n";
        count++;
        it->Next();
    }
    std::cout << "  Found: " << count << " keys (empty range proven)\n\n";
    delete it;
    // ------------------------ RANGE SCAN 4: cross-prefix range ------------------------
    // Range that crosses from animal: into plant:
    std::cout << "------------------------ Range 4: [animal:fox, plant:oak] ------------------------\n";
    std::cout << "   Crosses prefix boundary\n";
    it = db->NewIterator(ro);
    lo = "animal:fox";
    hi = "plant:oak";
    it->Seek(lo);
    count = 0;
    while (it->Valid()) {
        k = it->key().ToString();
        if (k > hi) break;
        std::cout << "  " << k << "\n";
        count++;
        it->Next();
    }
    std::cout << "  Found: " << count << " keys\n";
    delete it;

    delete db;
    return 0;
}