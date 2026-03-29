#include <iostream>
#include "leveldb/db.h"

int main() {
    leveldb::Options options;
    options.create_if_missing = true;
    leveldb::DB* db;
    leveldb::DB::Open(options, "/tmp/mydb", &db);

    leveldb::WriteOptions wo;
    leveldb::ReadOptions ro;
    std::string value;
    leveldb::Status s;

    // Write 3 keys
    db->Put(wo, "animal:bear", "Large omnivore");
    db->Put(wo, "animal:cat",  "Domestic feline");
    db->Put(wo, "animal:dog",  "Domestic canine");
    std::cout << "Written: bear, cat, dog\n\n";

    // Read before delete
    db->Get(ro, "animal:bear", &value);
    std::cout << "Before delete: animal:bear = " << value << "\n";

    // Delete — writes a TOMBSTONE record, NOT actual removal
    // Old data stays on disk until compaction
    // WAL grows because tombstone is appended there too
    s = db->Delete(wo, "animal:bear");
    std::cout << "Delete animal:bear → " << s.ToString() << "\n";

    // Try to read deleted key
    s = db->Get(ro, "animal:bear", &value);
    if (s.IsNotFound()) {
        std::cout << "After delete: animal:bear → NotFound\n";
        std::cout << "  Tombstone hides the value\n";
        std::cout << "  Old bytes still on disk until compaction\n\n";
    }

    // Other keys are untouched
    db->Get(ro, "animal:cat", &value);
    std::cout << "animal:cat still exists: " << value << "\n";

    // Delete a key that never existed — NOT an error
    s = db->Delete(wo, "animal:zebra");
    std::cout << "\nDelete animal:zebra (never existed) - "<< s.ToString() << "\n";
    std::cout << "Deleting non-existent key = OK, not an error\n";

    // Check log file size — it grew because tombstone was appended
    std::cout << "\nLog file after deletes:\n";
    system("ls -la /tmp/mydb/000003.log");

    delete db;
    return 0;
}
