#include <iostream>
#include "leveldb/db.h"

int main()
{
  leveldb ::Options options;
  options.create_if_missing = true;
  // 
  leveldb::DB *db;
  leveldb::Status s = leveldb::DB::Open(options, "/tmp/mydb", &db);

  if (!s.ok())
  {
    std::cout<<"ERROR: "<<s.ToString()<<"\n";
    return 1;
  }

  std::cout <<"Database opened successfully at /tmp/mydb\n";

  std::cout<<"\n Files created in /tmp/mydb:\n";
  system("ls -la /tmp/mydb");

  delete db;
  std::cout<<"\nDatabase closed successfully.\n";
  return 0;

}