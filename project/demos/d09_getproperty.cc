#include <iostream>
#include "leveldb/db.h"

int main()
{
  leveldb::Options options;
  options.create_if_missing = true;
  // real is 4mb we use 4kb as buffer size to see the stats more clearly
  options.write_buffer_size = 4096;
  leveldb::DB* db;
  leveldb::DB::Open(options,"/tmp/testdb",&db);

  leveldb::WriteOptions wo;

  //Write Enough keys to see the stats , basically any random big numnber
  for(int i=0;i<100;i++)
  {
    std::string key = "key:" + std::to_string(i);
    std::string value = "value:" + std::to_string(i);
    db->Put(wo,key,value);
  }

  std::cout<<"Writting done for 100 keys\n\n";

  // Property 1L full stats table
  std::string stats;
  // which SSTable files exist at each level
  bool ok = db->GetProperty("leveldb.stats", &stats);
  std::cout<<"leveldb.stats:\n";
  if(ok)
  {
    std::cout<<stats<<"\n";
  }
  else
  {
    std::cout<<"Unable to get stats\n";
  }

  //Property 2 : SStable files per level
  std::string sstables;
  ok = db->GetProperty("leveldb.sstables", &sstables);
  std::cout<<"leveldb.sstables:\n";
  if(ok)
  {
    std::cout<<sstables<<"\n";
  }
  else
  {
    std::cout<<"Unable to get sstables\n";
  }


  // Property 3: file count at each level
  std::cout << "Files per level:\n";
  for (int level = 0; level < 7; level++)
  {
    std::string pop = "leveldb.num-files-at-level" + std::to_string(level);
    std::string result;
    ok = db->GetProperty(pop, &result);
    if (ok)
    {
      std::cout << "Level " << level << ": " << result << " files\n";
    }
    else
    {
      std::cout << "Unable to get file count for level " << level << "\n";
    }
  }

  //Property 4: memory usage
  std::string mem_usage;
  ok = db->GetProperty("leveldb.approximate-memory-usage", &mem_usage);
  std::cout<<"\nleveldb.approximate-memory-usage:"<<mem_usage<<"bytes\n";

  delete db;
  return 0;





}