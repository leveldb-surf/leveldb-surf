#include<iostream>
#include "leveldb/db.h"

int main()
{
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::DB* db;
  leveldb::DB::Open(options, "/tmp/mydb", &db);

  //WtiteOptions controls How the write operation behaves.
  // By default, the write operation is asynchronous and
  // may return before the data is actually written to disk.
  // If you want to ensure that the data is written to disk before the write operation returns,
  // you can set the sync option to true.

  // sync = false (default): The write operation may return before the data is actually written to disk.
  // sync = true: The write operation will block until the data is written to disk, ensuring durability.

    // sync = false (default): write goes to OS buffer first (fast)
    // sync = true: calls fsync() after every write (safe but ~100x slower)

    //Put(options, key, value) = store key-value pair with write behavior control

  leveldb::WriteOptions wo;
  wo.sync = false;

  leveldb :: Status s ;

  s = db->Put(wo, "name", "Advanced DataBase Systems");
  std::cout<<"Put name: "<<s.ToString()<<"\n";

  s = db->Put(wo, "University", "USC");
  std::cout<<"Put University: "<<s.ToString()<<"\n";

  s = db->Put(wo, "animal:bear", "Large omnivore");
  std::cout << "Put animal:bear: " << s.ToString() << "\n";

  s = db->Put(wo, "animal:cat",  "Domestic feline");
  std::cout << "Put animal:cat:  " << s.ToString() << "\n";

  s = db->Put(wo, "animal:dog",  "Domestic canine");
  std::cout << "Put animal:dog:  " << s.ToString() << "\n";

  std::cout<<"\nFiles created in /tmp/mydb:\n";
  system("ls -la /tmp/mydb");

  delete db;
  return 0;


}