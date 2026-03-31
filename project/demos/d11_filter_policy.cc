#include <iostream>
#include "leveldb/db.h"
#include "leveldb/filter_policy.h"   // NewBloomFilterPolicy lives here
                                      // filter_policy.h = file you read in B1

int main() {
    // THIS is the plug-in point for your entire project
    // options.filter_policy is the ONE line that changes in Week 2
    // Today: NewBloomFilterPolicy(10)
    // Week 2: NewSuRFFilterPolicy()
    leveldb::Options options;
    options.create_if_missing = true;
    options.write_buffer_size = 4096;
    options.filter_policy = leveldb::NewBloomFilterPolicy(10);
    // 10 = bits per key
    // 10 bits per key gives approximately 1% false positive rate
    // more bits = fewer false positives = more memory used

        std::cout << "Filter policy name: "
              << options.filter_policy->Name() << "\n";
    // Name() returns "leveldb.BuiltinBloomFilter2"
    // YOUR SuRF will return "leveldb.SuRFFilter"
    // Name must match between write time and read time
    // Mismatch = filter silently ignored = slow reads

    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;

    // Write keys - filter will be built during compaction
    for (int i = 0; i < 100; i++) {
        std::string key   = "animal:" + std::to_string(i);
        std::string value = "value:"  + std::to_string(i);
        db->Put(wo, key, value);
    }
    std::cout << "Written 100 keys\n";

    // Force compaction - this is when CreateFilter() runs
    // BloomFilterPolicy::CreateFilter() called here today
    // SuRFPolicy::CreateFilter() called here in Week 2
    db->CompactRange(nullptr, nullptr);
    std::cout << "Compaction done - filter block built inside SSTable\n\n";

      // Now test the filter in action
    leveldb::ReadOptions ro;
    std::string value;
    leveldb::Status s;

    // Point query - KeyMayMatch() called internally
    // table.cc line 225: filter->KeyMayMatch(block_offset, key)
    s = db->Get(ro, "animal:42", &value);
    std::cout << "Get animal:42 (exists):     "
              << s.ToString() << " = " << value << "\n";

    // Missing key - Bloom filter should catch this
    // Bloom hashes "animal:999", finds a 0-bit, returns false
    // SSTable skipped entirely - no disk read
    s = db->Get(ro, "animal:999", &value);
    std::cout << "Get animal:999 (missing):   " << (s.IsNotFound() ? "NotFound - Bloom filter blocked disk read": value) << "\n";

    // Check the SSTable to verify filter is stored inside
    std::string sstables;
    db->GetProperty("leveldb.sstables", &sstables);
    std::cout << "\nSSTable with filter block inside:\n" << sstables << "\n";

    std::cout << "The filter block is INSIDE the .ldb file\n";
    std::cout << "After Week 2 it will contain SuRF instead of Bloom\n";

    // ALWAYS delete filter_policy AFTER delete db
    // db uses filter_policy until it is closed
    delete db;
    delete options.filter_policy;
    std::cout << "\nFilter policy deleted cleanly\n";

    return 0;


  }
