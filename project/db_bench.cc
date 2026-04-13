// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include <sys/types.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#include "leveldb/cache.h"
#include "leveldb/comparator.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/write_batch.h"
#include "port/port.h"
#include "util/crc32c.h"
#include "util/histogram.h"
#include "util/mutexlock.h"
#include "util/random.h"
#include "util/testutil.h"

// Comma-separated list of operations to run in the specified order
//   Actual benchmarks:
//      fillseq       -- write N values in sequential key order in async mode
//      fillrandom    -- write N values in random key order in async mode
//      overwrite     -- overwrite N values in random key order in async mode
//      fillsync      -- write N/100 values in random key order in sync mode
//      fill100K      -- write N/1000 100K values in random order in async mode
//      deleteseq     -- delete N keys in sequential order
//      deleterandom  -- delete N keys in random order
//      readseq       -- read N times sequentially
//      readreverse   -- read N times in reverse order
//      readrandom    -- read N times in random order
//      readmissing   -- read N missing keys in random order
//      readhot       -- read N times in random order from 1% section of DB
//      seekrandom    -- N random seeks
//      seekordered   -- N ordered seeks
//      open          -- cost of opening a DB
//      crc32c        -- repeated crc32c of 4K of data
//
//   SuRF: range scan benchmarks (added for SuRF project)
//      surfscan      -- range scan with 100% miss rate (all empty ranges)
//      surfscan100   -- range scan with 100% miss rate
//      surfscan75    -- range scan with 75% miss rate, 25% hit
//      surfscan50    -- range scan with 50% miss rate, 50% hit
//      surfscan25    -- range scan with 25% miss rate, 75% hit
//      surfscan0     -- range scan with 0% miss rate (all ranges hit keys)
//      surfscan_wide -- range scan with 100% miss, wide range (width=100)
//
//   Meta operations:
//      compact     -- Compact the entire DB
//      stats       -- Print DB stats
//      sstables    -- Print sstable info
//      heapprofile -- Dump a heap profile (if supported by this port)
static const char* FLAGS_benchmarks =
    "fillseq,"
    "fillsync,"
    "fillrandom,"
    "overwrite,"
    "readrandom,"
    "readrandom,"  // Extra run to allow previous compactions to quiesce
    "readseq,"
    "readreverse,"
    "compact,"
    "readrandom,"
    "readseq,"
    "readreverse,"
    "fill100K,"
    "crc32c,"
    "snappycomp,"
    "snappyuncomp,"
    "zstdcomp,"
    "zstduncomp,";

// Number of key/values to place in database
static int FLAGS_num = 1000000;

// Number of read operations to do.  If negative, do FLAGS_num reads.
static int FLAGS_reads = -1;

// Number of concurrent threads to run.
static int FLAGS_threads = 1;

// Size of each value
static int FLAGS_value_size = 100;

// Arrange to generate values that shrink to this fraction of
// their original size after compression
static double FLAGS_compression_ratio = 0.5;

// Print histogram of operation timings
static bool FLAGS_histogram = false;

// Count the number of string comparisons performed
static bool FLAGS_comparisons = false;

// Number of bytes to buffer in memtable before compacting
// (initialized to default value by "main")
static int FLAGS_write_buffer_size = 0;

// Number of bytes written to each file.
// (initialized to default value by "main")
static int FLAGS_max_file_size = 0;

// Approximate size of user data packed per block (before compression.
// (initialized to default value by "main")
static int FLAGS_block_size = 0;

// Number of bytes to use as a cache of uncompressed data.
// Negative means use default settings.
static int FLAGS_cache_size = -1;

// Maximum number of files to keep open at the same time (use default if == 0)
static int FLAGS_open_files = 0;

// Bloom filter bits per key.
// Negative means use default settings.
static int FLAGS_bloom_bits = -1;

// Common key prefix length.
static int FLAGS_key_prefix = 0;

// If true, do not destroy the existing database.  If you set this
// flag and also specify a benchmark that wants a fresh database, that
// benchmark will fail.
static bool FLAGS_use_existing_db = false;

// If true, reuse existing log/MANIFEST files when re-opening a database.
static bool FLAGS_reuse_logs = false;

// If true, use compression.
static bool FLAGS_compression = true;

// Use the db with the following name.
static const char* FLAGS_db = nullptr;

// Optional JSONL metrics output file path.
static const char* FLAGS_metrics_out = nullptr;

// ZSTD compression level to try out
static int FLAGS_zstd_compression_level = 1;

// SuRF: filter type selection flag
// "bloom" = original Bloom filter (10 bits/key)
// "surf"  = SuRF range filter
// Allows switching filters from command line without recompiling:
//   ./db_bench --filter=surf --benchmarks=fillrandom,surfscan100
//   ./db_bench --filter=bloom --benchmarks=fillrandom,surfscan100
static const char* FLAGS_filter = "bloom";

namespace leveldb {

namespace {
leveldb::Env* g_env = nullptr;

class CountComparator : public Comparator {
 public:
  CountComparator(const Comparator* wrapped) : wrapped_(wrapped) {}
  ~CountComparator() override {}
  int Compare(const Slice& a, const Slice& b) const override {
    count_.fetch_add(1, std::memory_order_relaxed);
    return wrapped_->Compare(a, b);
  }
  const char* Name() const override { return wrapped_->Name(); }
  void FindShortestSeparator(std::string* start,
                             const Slice& limit) const override {
    wrapped_->FindShortestSeparator(start, limit);
  }

  void FindShortSuccessor(std::string* key) const override {
    return wrapped_->FindShortSuccessor(key);
  }

  size_t comparisons() const { return count_.load(std::memory_order_relaxed); }

  void reset() { count_.store(0, std::memory_order_relaxed); }

 private:
  mutable std::atomic<size_t> count_{0};
  const Comparator* const wrapped_;
};

}  // namespace

class JsonlWriter {
 public:
  JsonlWriter() : file_(nullptr) {}
  ~JsonlWriter() { Close(); }

  bool Open(const char* path) {
    file_ = std::fopen(path, "w");
    return file_ != nullptr;
  }

  void Close() {
    if (file_ != nullptr) {
      std::fclose(file_);
      file_ = nullptr;
    }
  }

  bool IsOpen() const { return file_ != nullptr; }

  bool WriteLine(const std::string& json_line) {
    if (file_ == nullptr) {
      return false;
    }
    if (std::fputs(json_line.c_str(), file_) == EOF) {
      return false;
    }
    if (std::fputc('\n', file_) == EOF) {
      return false;
    }
    return std::fflush(file_) == 0;
  }

  static std::string EscapeString(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (unsigned char c : value) {
      switch (c) {
        case '"': escaped.append("\\\""); break;
        case '\\': escaped.append("\\\\"); break;
        case '\b': escaped.append("\\b"); break;
        case '\f': escaped.append("\\f"); break;
        case '\n': escaped.append("\\n"); break;
        case '\r': escaped.append("\\r"); break;
        case '\t': escaped.append("\\t"); break;
        default:
          if (c < 0x20) {
            char buf[7];
            std::snprintf(buf, sizeof(buf), "\\u%04x", c);
            escaped.append(buf);
          } else {
            escaped.push_back(c);
          }
          break;
      }
    }
    return escaped;
  }

 private:
  FILE* file_;
};

namespace {

// Helper for quickly generating random data.
class RandomGenerator {
 private:
  std::string data_;
  int pos_;

 public:
  RandomGenerator() {
    // We use a limited amount of data over and over again and ensure
    // that it is larger than the compression window (32KB), and also
    // large enough to serve all typical value sizes we want to write.
    Random rnd(301);
    std::string piece;
    while (data_.size() < 1048576) {
      // Add a short fragment that is as compressible as specified
      // by FLAGS_compression_ratio.
      test::CompressibleString(&rnd, FLAGS_compression_ratio, 100, &piece);
      data_.append(piece);
    }
    pos_ = 0;
  }

  Slice Generate(size_t len) {
    if (pos_ + len > data_.size()) {
      pos_ = 0;
      assert(len < data_.size());
    }
    pos_ += len;
    return Slice(data_.data() + pos_ - len, len);
  }
};

class KeyBuffer {
 public:
  KeyBuffer() {
    assert(FLAGS_key_prefix < sizeof(buffer_));
    memset(buffer_, 'a', FLAGS_key_prefix);
  }
  KeyBuffer& operator=(KeyBuffer& other) = delete;
  KeyBuffer(KeyBuffer& other) = delete;

  void Set(int k) {
    std::snprintf(buffer_ + FLAGS_key_prefix,
                  sizeof(buffer_) - FLAGS_key_prefix, "%016d", k);
  }

  Slice slice() const { return Slice(buffer_, FLAGS_key_prefix + 16); }

 private:
  char buffer_[1024];
};

#if defined(__linux)
static Slice TrimSpace(Slice s) {
  size_t start = 0;
  while (start < s.size() && isspace(s[start])) {
    start++;
  }
  size_t limit = s.size();
  while (limit > start && isspace(s[limit - 1])) {
    limit--;
  }
  return Slice(s.data() + start, limit - start);
}
#endif

static void AppendWithSpace(std::string* str, Slice msg) {
  if (msg.empty()) return;
  if (!str->empty()) {
    str->push_back(' ');
  }
  str->append(msg.data(), msg.size());
}

class Stats {
 private:
  double start_;
  double finish_;
  double seconds_;
  int done_;
  int next_report_;
  int64_t bytes_;
  double last_op_finish_;
  Histogram hist_;
  std::string message_;

 public:
  Stats() { Start(); }

  void Start() {
    next_report_ = 100;
    hist_.Clear();
    done_ = 0;
    bytes_ = 0;
    seconds_ = 0;
    message_.clear();
    start_ = finish_ = last_op_finish_ = g_env->NowMicros();
  }

  void Merge(const Stats& other) {
    hist_.Merge(other.hist_);
    done_ += other.done_;
    bytes_ += other.bytes_;
    seconds_ += other.seconds_;
    if (other.start_ < start_) start_ = other.start_;
    if (other.finish_ > finish_) finish_ = other.finish_;

    // Just keep the messages from one thread
    if (message_.empty()) message_ = other.message_;
  }

  void Stop() {
    finish_ = g_env->NowMicros();
    seconds_ = (finish_ - start_) * 1e-6;
  }

  void AddMessage(Slice msg) { AppendWithSpace(&message_, msg); }

  void FinishedSingleOp() {
    if (FLAGS_histogram) {
      double now = g_env->NowMicros();
      double micros = now - last_op_finish_;
      hist_.Add(micros);
      if (micros > 20000) {
        std::fprintf(stderr, "long op: %.1f micros%30s\r", micros, "");
        std::fflush(stderr);
      }
      last_op_finish_ = now;
    }

    done_++;
    if (done_ >= next_report_) {
      if (next_report_ < 1000)
        next_report_ += 100;
      else if (next_report_ < 5000)
        next_report_ += 500;
      else if (next_report_ < 10000)
        next_report_ += 1000;
      else if (next_report_ < 50000)
        next_report_ += 5000;
      else if (next_report_ < 100000)
        next_report_ += 10000;
      else if (next_report_ < 500000)
        next_report_ += 50000;
      else
        next_report_ += 100000;
      std::fprintf(stderr, "... finished %d ops%30s\r", done_, "");
      std::fflush(stderr);
    }
  }

  void AddBytes(int64_t n) { bytes_ += n; }

  void Report(const Slice& name) {
    // Pretend at least one op was done in case we are running a benchmark
    // that does not call FinishedSingleOp().
    if (done_ < 1) done_ = 1;

    std::string extra;
    if (bytes_ > 0) {
      // Rate is computed on actual elapsed time, not the sum of per-thread
      // elapsed times.
      double elapsed = (finish_ - start_) * 1e-6;
      char rate[100];
      std::snprintf(rate, sizeof(rate), "%6.1f MB/s",
                    (bytes_ / 1048576.0) / elapsed);
      extra = rate;
    }
    AppendWithSpace(&extra, message_);

    std::fprintf(stdout, "%-12s : %11.3f micros/op;%s%s\n",
                 name.ToString().c_str(), seconds_ * 1e6 / done_,
                 (extra.empty() ? "" : " "), extra.c_str());
    if (FLAGS_histogram) {
      std::fprintf(stdout, "Microseconds per op:\n%s\n",
                   hist_.ToString().c_str());
    }
    std::fflush(stdout);
  }
};

// State shared by all concurrent executions of the same benchmark.
struct SharedState {
  port::Mutex mu;
  port::CondVar cv GUARDED_BY(mu);
  int total GUARDED_BY(mu);

  // Each thread goes through the following states:
  //    (1) initializing
  //    (2) waiting for others to be initialized
  //    (3) running
  //    (4) done

  int num_initialized GUARDED_BY(mu);
  int num_done GUARDED_BY(mu);
  bool start GUARDED_BY(mu);

  SharedState(int total)
      : cv(&mu), total(total), num_initialized(0), num_done(0), start(false) {}
};

// Per-thread state for concurrent executions of the same benchmark.
struct ThreadState {
  int tid;      // 0..n-1 when running in n threads
  Random rand;  // Has different seeds for different threads
  Stats stats;
  SharedState* shared;

  ThreadState(int index, int seed) : tid(index), rand(seed), shared(nullptr) {}
};

void Compress(
    ThreadState* thread, std::string name,
    std::function<bool(const char*, size_t, std::string*)> compress_func) {
  RandomGenerator gen;
  Slice input = gen.Generate(Options().block_size);
  int64_t bytes = 0;
  int64_t produced = 0;
  bool ok = true;
  std::string compressed;
  while (ok && bytes < 1024 * 1048576) {  // Compress 1G
    ok = compress_func(input.data(), input.size(), &compressed);
    produced += compressed.size();
    bytes += input.size();
    thread->stats.FinishedSingleOp();
  }

  if (!ok) {
    thread->stats.AddMessage("(" + name + " failure)");
  } else {
    char buf[100];
    std::snprintf(buf, sizeof(buf), "(output: %.1f%%)",
                  (produced * 100.0) / bytes);
    thread->stats.AddMessage(buf);
    thread->stats.AddBytes(bytes);
  }
}

void Uncompress(
    ThreadState* thread, std::string name,
    std::function<bool(const char*, size_t, std::string*)> compress_func,
    std::function<bool(const char*, size_t, char*)> uncompress_func) {
  RandomGenerator gen;
  Slice input = gen.Generate(Options().block_size);
  std::string compressed;
  bool ok = compress_func(input.data(), input.size(), &compressed);
  int64_t bytes = 0;
  char* uncompressed = new char[input.size()];
  while (ok && bytes < 1024 * 1048576) {  // Compress 1G
    ok = uncompress_func(compressed.data(), compressed.size(), uncompressed);
    bytes += input.size();
    thread->stats.FinishedSingleOp();
  }
  delete[] uncompressed;

  if (!ok) {
    thread->stats.AddMessage("(" + name + " failure)");
  } else {
    thread->stats.AddBytes(bytes);
  }
}

}  // namespace

class Benchmark {
 private:
  Cache* cache_;
  const FilterPolicy* filter_policy_;
  DB* db_;
  int num_;
  int value_size_;
  int entries_per_batch_;
  WriteOptions write_options_;
  int reads_;
  int heap_counter_;
  CountComparator count_comparator_;
  int total_thread_count_;
  JsonlWriter* metrics_writer_;
  port::Mutex metrics_mu_;
  std::atomic<uint64_t> next_query_id_;

  void PrintHeader() {
    const int kKeySize = 16 + FLAGS_key_prefix;
    PrintEnvironment();
    std::fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
    std::fprintf(
        stdout, "Values:     %d bytes each (%d bytes after compression)\n",
        FLAGS_value_size,
        static_cast<int>(FLAGS_value_size * FLAGS_compression_ratio + 0.5));
    std::fprintf(stdout, "Entries:    %d\n", num_);
    std::fprintf(stdout, "RawSize:    %.1f MB (estimated)\n",
                 ((static_cast<int64_t>(kKeySize + FLAGS_value_size) * num_) /
                  1048576.0));
    std::fprintf(
        stdout, "FileSize:   %.1f MB (estimated)\n",
        (((kKeySize + FLAGS_value_size * FLAGS_compression_ratio) * num_) /
         1048576.0));
    PrintWarnings();
    // SuRF: print which filter is active so benchmark output is self-documenting
    std::fprintf(stdout, "Filter:     %s\n", FLAGS_filter);
    std::fprintf(stdout, "------------------------------------------------\n");
  }

  void PrintWarnings() {
#if defined(__GNUC__) && !defined(__OPTIMIZE__)
    std::fprintf(
        stdout,
        "WARNING: Optimization is disabled: benchmarks unnecessarily slow\n");
#endif
#ifndef NDEBUG
    std::fprintf(
        stdout,
        "WARNING: Assertions are enabled; benchmarks unnecessarily slow\n");
#endif

    // See if snappy is working by attempting to compress a compressible string
    const char text[] = "yyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy";
    std::string compressed;
    if (!port::Snappy_Compress(text, sizeof(text), &compressed)) {
      std::fprintf(stdout, "WARNING: Snappy compression is not enabled\n");
    } else if (compressed.size() >= sizeof(text)) {
      std::fprintf(stdout, "WARNING: Snappy compression is not effective\n");
    }
  }

  void PrintEnvironment() {
    std::fprintf(stderr, "LevelDB:    version %d.%d\n", kMajorVersion,
                 kMinorVersion);

#if defined(__linux)
    time_t now = time(nullptr);
    std::fprintf(stderr, "Date:       %s",
                 ctime(&now));  // ctime() adds newline

    FILE* cpuinfo = std::fopen("/proc/cpuinfo", "r");
    if (cpuinfo != nullptr) {
      char line[1000];
      int num_cpus = 0;
      std::string cpu_type;
      std::string cache_size;
      while (fgets(line, sizeof(line), cpuinfo) != nullptr) {
        const char* sep = strchr(line, ':');
        if (sep == nullptr) {
          continue;
        }
        Slice key = TrimSpace(Slice(line, sep - 1 - line));
        Slice val = TrimSpace(Slice(sep + 1));
        if (key == "model name") {
          ++num_cpus;
          cpu_type = val.ToString();
        } else if (key == "cache size") {
          cache_size = val.ToString();
        }
      }
      std::fclose(cpuinfo);
      std::fprintf(stderr, "CPU:        %d * %s\n", num_cpus, cpu_type.c_str());
      std::fprintf(stderr, "CPUCache:   %s\n", cache_size.c_str());
    }
#endif
  }

 public:
  Benchmark()
      : cache_(FLAGS_cache_size >= 0 ? NewLRUCache(FLAGS_cache_size) : nullptr),
        // SuRF: select filter based on --filter flag
        // "surf"  -> NewSuRFFilterPolicy() for range query support
        // "bloom" -> NewBloomFilterPolicy(10) original Bloom filter
        // This avoids recompiling to switch filters during benchmarking
        filter_policy_(strcmp(FLAGS_filter, "surf") == 0
                           ? NewSuRFFilterPolicy()
                           : NewBloomFilterPolicy(10)),
        db_(nullptr),
        num_(FLAGS_num),
        value_size_(FLAGS_value_size),
        entries_per_batch_(1),
        reads_(FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads),
        heap_counter_(0),
        count_comparator_(BytewiseComparator()),
        total_thread_count_(0),
        metrics_writer_(nullptr),
        next_query_id_(0) {
    std::vector<std::string> files;
    g_env->GetChildren(FLAGS_db, &files);
    for (size_t i = 0; i < files.size(); i++) {
      if (Slice(files[i]).starts_with("heap-")) {
        g_env->RemoveFile(std::string(FLAGS_db) + "/" + files[i]);
      }
    }
    if (!FLAGS_use_existing_db) {
      DestroyDB(FLAGS_db, Options());
    }
  }

  ~Benchmark() {
    delete db_;
    delete cache_;
    delete filter_policy_;
  }

  void SetMetricsWriter(JsonlWriter* writer) { metrics_writer_ = writer; }

  void RecordQueryEvent(const char* benchmark_name, const char* query_type,
                        const Slice* lo, const Slice* hi,
                        bool actual_match, double latency_us,
                        int64_t timestamp_us) {
    if (metrics_writer_ == nullptr) {
      return;
    }

    uint64_t query_id = next_query_id_.fetch_add(1, std::memory_order_relaxed);
    std::string line = "{";
    line += "\"query_id\":" + std::to_string(query_id);
    line += ",\"benchmark_name\":\"" +
            JsonlWriter::EscapeString(benchmark_name) + "\"";
    line += ",\"filter_type\":\"" +
            JsonlWriter::EscapeString(FLAGS_filter) + "\"";
    line += ",\"query_type\":\"" +
            JsonlWriter::EscapeString(query_type) + "\"";
    line += ",\"latency_us\":" + std::to_string(latency_us);
    line += ",\"timestamp_us\":" + std::to_string(timestamp_us);
    if (lo != nullptr) {
      line += ",\"query_lo\":\"" +
              JsonlWriter::EscapeString(lo->ToString()) + "\"";
    }
    if (hi != nullptr) {
      line += ",\"query_hi\":\"" +
              JsonlWriter::EscapeString(hi->ToString()) + "\"";
    }
    line += ",\"actual_match\":" +
            std::string(actual_match ? "true" : "false");
    line += "}";

    {
      MutexLock l(&metrics_mu_);
      metrics_writer_->WriteLine(line);
    }
  }

  void Run() {
    PrintHeader();
    Open();

    const char* benchmarks = FLAGS_benchmarks;
    while (benchmarks != nullptr) {
      const char* sep = strchr(benchmarks, ',');
      Slice name;
      if (sep == nullptr) {
        name = benchmarks;
        benchmarks = nullptr;
      } else {
        name = Slice(benchmarks, sep - benchmarks);
        benchmarks = sep + 1;
      }

      // Reset parameters that may be overridden below
      num_ = FLAGS_num;
      reads_ = (FLAGS_reads < 0 ? FLAGS_num : FLAGS_reads);
      value_size_ = FLAGS_value_size;
      entries_per_batch_ = 1;
      write_options_ = WriteOptions();

      void (Benchmark::*method)(ThreadState*) = nullptr;
      bool fresh_db = false;
      int num_threads = FLAGS_threads;

      if (name == Slice("open")) {
        method = &Benchmark::OpenBench;
        num_ /= 10000;
        if (num_ < 1) num_ = 1;
      } else if (name == Slice("fillseq")) {
        fresh_db = true;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillbatch")) {
        fresh_db = true;
        entries_per_batch_ = 1000;
        method = &Benchmark::WriteSeq;
      } else if (name == Slice("fillrandom")) {
        fresh_db = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("overwrite")) {
        fresh_db = false;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fillsync")) {
        fresh_db = true;
        num_ /= 1000;
        write_options_.sync = true;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("fill100K")) {
        fresh_db = true;
        num_ /= 1000;
        value_size_ = 100 * 1000;
        method = &Benchmark::WriteRandom;
      } else if (name == Slice("readseq")) {
        method = &Benchmark::ReadSequential;
      } else if (name == Slice("readreverse")) {
        method = &Benchmark::ReadReverse;
      } else if (name == Slice("readrandom")) {
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("readmissing")) {
        method = &Benchmark::ReadMissing;
      } else if (name == Slice("seekrandom")) {
        method = &Benchmark::SeekRandom;
      } else if (name == Slice("seekordered")) {
        method = &Benchmark::SeekOrdered;
      // ================================================================
      // SuRF: range scan benchmarks with variable miss rates
      // Each benchmark generates queries where X% land outside inserted
      // key range (guaranteed empty = miss) and (100-X)% land inside
      // (likely to find keys = hit).
      //
      // surfscan / surfscan100 : 100% miss — all queries above key space
      // surfscan75             :  75% miss — 3/4 queries above key space
      // surfscan50             :  50% miss — half above, half inside
      // surfscan25             :  25% miss — 1/4 above, 3/4 inside
      // surfscan0              :   0% miss — all queries inside key space
      // surfscan_wide          : 100% miss — wide range (width=100 keys)
      //
      // SuRF advantage increases with miss rate: higher miss rate means
      // more SSTables can be skipped via RangeMayMatch returning false.
      // ================================================================
      } else if (name == Slice("surfscan") || name == Slice("surfscan100")) {
        method = &Benchmark::SuRFRangeScan100;
      } else if (name == Slice("surfscan75")) {
        method = &Benchmark::SuRFRangeScan75;
      } else if (name == Slice("surfscan50")) {
        method = &Benchmark::SuRFRangeScan50;
      } else if (name == Slice("surfscan25")) {
        method = &Benchmark::SuRFRangeScan25;
      } else if (name == Slice("surfscan0")) {
        method = &Benchmark::SuRFRangeScan0;
      } else if (name == Slice("surfscan_wide")) {
        method = &Benchmark::SuRFRangeScanWide;
      // SuRF: end of range scan benchmarks
      // ================================================================
      } else if (name == Slice("readhot")) {
        method = &Benchmark::ReadHot;
      } else if (name == Slice("readrandomsmall")) {
        reads_ /= 1000;
        method = &Benchmark::ReadRandom;
      } else if (name == Slice("deleteseq")) {
        method = &Benchmark::DeleteSeq;
      } else if (name == Slice("deleterandom")) {
        method = &Benchmark::DeleteRandom;
      } else if (name == Slice("readwhilewriting")) {
        num_threads++;  // Add extra thread for writing
        method = &Benchmark::ReadWhileWriting;
      } else if (name == Slice("compact")) {
        method = &Benchmark::Compact;
      } else if (name == Slice("crc32c")) {
        method = &Benchmark::Crc32c;
      } else if (name == Slice("snappycomp")) {
        method = &Benchmark::SnappyCompress;
      } else if (name == Slice("snappyuncomp")) {
        method = &Benchmark::SnappyUncompress;
      } else if (name == Slice("zstdcomp")) {
        method = &Benchmark::ZstdCompress;
      } else if (name == Slice("zstduncomp")) {
        method = &Benchmark::ZstdUncompress;
      } else if (name == Slice("heapprofile")) {
        HeapProfile();
      } else if (name == Slice("stats")) {
        PrintStats("leveldb.stats");
      } else if (name == Slice("sstables")) {
        PrintStats("leveldb.sstables");
      } else {
        if (!name.empty()) {  // No error message for empty name
          std::fprintf(stderr, "unknown benchmark '%s'\n",
                       name.ToString().c_str());
        }
      }

      if (fresh_db) {
        if (FLAGS_use_existing_db) {
          std::fprintf(stdout, "%-12s : skipped (--use_existing_db is true)\n",
                       name.ToString().c_str());
          method = nullptr;
        } else {
          delete db_;
          db_ = nullptr;
          DestroyDB(FLAGS_db, Options());
          Open();
        }
      }

      if (method != nullptr) {
        RunBenchmark(num_threads, name, method);
      }
    }
  }

 private:
  struct ThreadArg {
    Benchmark* bm;
    SharedState* shared;
    ThreadState* thread;
    void (Benchmark::*method)(ThreadState*);
  };

  static void ThreadBody(void* v) {
    ThreadArg* arg = reinterpret_cast<ThreadArg*>(v);
    SharedState* shared = arg->shared;
    ThreadState* thread = arg->thread;
    {
      MutexLock l(&shared->mu);
      shared->num_initialized++;
      if (shared->num_initialized >= shared->total) {
        shared->cv.SignalAll();
      }
      while (!shared->start) {
        shared->cv.Wait();
      }
    }

    thread->stats.Start();
    (arg->bm->*(arg->method))(thread);
    thread->stats.Stop();

    {
      MutexLock l(&shared->mu);
      shared->num_done++;
      if (shared->num_done >= shared->total) {
        shared->cv.SignalAll();
      }
    }
  }

  void RunBenchmark(int n, Slice name,
                    void (Benchmark::*method)(ThreadState*)) {
    SharedState shared(n);

    ThreadArg* arg = new ThreadArg[n];
    for (int i = 0; i < n; i++) {
      arg[i].bm = this;
      arg[i].method = method;
      arg[i].shared = &shared;
      ++total_thread_count_;
      // Seed the thread's random state deterministically based upon thread
      // creation across all benchmarks. This ensures that the seeds are unique
      // but reproducible when rerunning the same set of benchmarks.
      arg[i].thread = new ThreadState(i, /*seed=*/1000 + total_thread_count_);
      arg[i].thread->shared = &shared;
      g_env->StartThread(ThreadBody, &arg[i]);
    }

    shared.mu.Lock();
    while (shared.num_initialized < n) {
      shared.cv.Wait();
    }

    shared.start = true;
    shared.cv.SignalAll();
    while (shared.num_done < n) {
      shared.cv.Wait();
    }
    shared.mu.Unlock();

    for (int i = 1; i < n; i++) {
      arg[0].thread->stats.Merge(arg[i].thread->stats);
    }
    arg[0].thread->stats.Report(name);
    if (FLAGS_comparisons) {
      fprintf(stdout, "Comparisons: %zu\n", count_comparator_.comparisons());
      count_comparator_.reset();
      fflush(stdout);
    }

    for (int i = 0; i < n; i++) {
      delete arg[i].thread;
    }
    delete[] arg;
  }

  void Crc32c(ThreadState* thread) {
    // Checksum about 500MB of data total
    const int size = 4096;
    const char* label = "(4K per op)";
    std::string data(size, 'x');
    int64_t bytes = 0;
    uint32_t crc = 0;
    while (bytes < 500 * 1048576) {
      crc = crc32c::Value(data.data(), size);
      thread->stats.FinishedSingleOp();
      bytes += size;
    }
    // Print so result is not dead
    std::fprintf(stderr, "... crc=0x%x\r", static_cast<unsigned int>(crc));

    thread->stats.AddBytes(bytes);
    thread->stats.AddMessage(label);
  }

  void SnappyCompress(ThreadState* thread) {
    Compress(thread, "snappy", &port::Snappy_Compress);
  }

  void SnappyUncompress(ThreadState* thread) {
    Uncompress(thread, "snappy", &port::Snappy_Compress,
               &port::Snappy_Uncompress);
  }

  void ZstdCompress(ThreadState* thread) {
    Compress(thread, "zstd",
             [](const char* input, size_t length, std::string* output) {
               return port::Zstd_Compress(FLAGS_zstd_compression_level, input,
                                          length, output);
             });
  }

  void ZstdUncompress(ThreadState* thread) {
    Uncompress(
        thread, "zstd",
        [](const char* input, size_t length, std::string* output) {
          return port::Zstd_Compress(FLAGS_zstd_compression_level, input,
                                     length, output);
        },
        &port::Zstd_Uncompress);
  }

  void Open() {
    assert(db_ == nullptr);
    Options options;
    options.env = g_env;
    options.create_if_missing = !FLAGS_use_existing_db;
    options.block_cache = cache_;
    options.write_buffer_size = FLAGS_write_buffer_size;
    options.max_file_size = FLAGS_max_file_size;
    options.block_size = FLAGS_block_size;
    if (FLAGS_comparisons) {
      options.comparator = &count_comparator_;
    }
    options.max_open_files = FLAGS_open_files;
    options.filter_policy = filter_policy_;
    options.reuse_logs = FLAGS_reuse_logs;
    options.compression =
        FLAGS_compression ? kSnappyCompression : kNoCompression;
    Status s = DB::Open(options, FLAGS_db, &db_);
    if (!s.ok()) {
      std::fprintf(stderr, "open error: %s\n", s.ToString().c_str());
      std::exit(1);
    }
  }

  void OpenBench(ThreadState* thread) {
    for (int i = 0; i < num_; i++) {
      delete db_;
      Open();
      thread->stats.FinishedSingleOp();
    }
  }

  void WriteSeq(ThreadState* thread) { DoWrite(thread, true); }

  void WriteRandom(ThreadState* thread) { DoWrite(thread, false); }

  void DoWrite(ThreadState* thread, bool seq) {
    if (num_ != FLAGS_num) {
      char msg[100];
      std::snprintf(msg, sizeof(msg), "(%d ops)", num_);
      thread->stats.AddMessage(msg);
    }

    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    int64_t bytes = 0;
    KeyBuffer key;
    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
        const int k = seq ? i + j : thread->rand.Uniform(FLAGS_num);
        key.Set(k);
        batch.Put(key.slice(), gen.Generate(value_size_));
        bytes += value_size_ + key.slice().size();
        thread->stats.FinishedSingleOp();
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
    thread->stats.AddBytes(bytes);
  }

  void ReadSequential(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions());
    int i = 0;
    int64_t bytes = 0;
    for (iter->SeekToFirst(); i < reads_ && iter->Valid(); iter->Next()) {
      int64_t start = g_env->NowMicros();
      bytes += iter->key().size() + iter->value().size();
      int64_t end = g_env->NowMicros();
      RecordQueryEvent("readseq", "sequential_scan", nullptr, nullptr, true,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadReverse(ThreadState* thread) {
    Iterator* iter = db_->NewIterator(ReadOptions());
    int i = 0;
    int64_t bytes = 0;
    for (iter->SeekToLast(); i < reads_ && iter->Valid(); iter->Prev()) {
      int64_t start = g_env->NowMicros();
      bytes += iter->key().size() + iter->value().size();
      int64_t end = g_env->NowMicros();
      RecordQueryEvent("readreverse", "reverse_scan", nullptr, nullptr, true,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
      ++i;
    }
    delete iter;
    thread->stats.AddBytes(bytes);
  }

  void ReadRandom(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    int found = 0;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      const int k = thread->rand.Uniform(FLAGS_num);
      key.Set(k);
      int64_t start = g_env->NowMicros();
      bool ok = db_->Get(options, key.slice(), &value).ok();
      int64_t end = g_env->NowMicros();
      if (ok) {
        found++;
      }
      RecordQueryEvent("readrandom", "point_get", nullptr, nullptr, ok,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
    }
    char msg[100];
    std::snprintf(msg, sizeof(msg), "(%d of %d found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void ReadMissing(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      const int k = thread->rand.Uniform(FLAGS_num);
      key.Set(k);
      Slice s = Slice(key.slice().data(), key.slice().size() - 1);
      int64_t start = g_env->NowMicros();
      bool ok = db_->Get(options, s, &value).ok();
      int64_t end = g_env->NowMicros();
      RecordQueryEvent("readmissing", "point_get", nullptr, nullptr, ok,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
    }
  }

  void ReadHot(ThreadState* thread) {
    ReadOptions options;
    std::string value;
    const int range = (FLAGS_num + 99) / 100;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      const int k = thread->rand.Uniform(range);
      key.Set(k);
      int64_t start = g_env->NowMicros();
      bool ok = db_->Get(options, key.slice(), &value).ok();
      int64_t end = g_env->NowMicros();
      RecordQueryEvent("readhot", "point_get", nullptr, nullptr, ok,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
    }
  }

  void SeekRandom(ThreadState* thread) {
    ReadOptions options;
    int found = 0;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      int64_t start = g_env->NowMicros();
      Iterator* iter = db_->NewIterator(options);
      const int k = thread->rand.Uniform(FLAGS_num);
      key.Set(k);
      iter->Seek(key.slice());
      bool ok = iter->Valid() && iter->key() == key.slice();
      int64_t end = g_env->NowMicros();
      if (ok) found++;
      RecordQueryEvent("seekrandom", "point_seek", nullptr, nullptr, ok,
                       static_cast<double>(end - start), end);
      delete iter;
      thread->stats.FinishedSingleOp();
    }
    char msg[100];
    snprintf(msg, sizeof(msg), "(%d of %d found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  void SeekOrdered(ThreadState* thread) {
    ReadOptions options;
    Iterator* iter = db_->NewIterator(options);
    int found = 0;
    int k = 0;
    KeyBuffer key;
    for (int i = 0; i < reads_; i++) {
      k = (k + (thread->rand.Uniform(100))) % FLAGS_num;
      key.Set(k);
      int64_t start = g_env->NowMicros();
      iter->Seek(key.slice());
      bool ok = iter->Valid() && iter->key() == key.slice();
      int64_t end = g_env->NowMicros();
      if (ok) found++;
      RecordQueryEvent("seekordered", "point_seek", nullptr, nullptr, ok,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
    }
    delete iter;
    char msg[100];
    std::snprintf(msg, sizeof(msg), "(%d of %d found)", found, num_);
    thread->stats.AddMessage(msg);
  }

  // ====================================================================
  // SuRF: range scan benchmarks
  //
  // Core logic: DoSuRFRangeScan() performs range scans with a configurable
  // miss percentage and range width.
  //
  // miss_pct controls what fraction of queries target empty ranges:
  //   - miss query:  k = FLAGS_num + rand(FLAGS_num)  → above all inserted keys
  //   - hit query:   k = rand(FLAGS_num)              → inside inserted key range
  //
  // range_width controls how many keys wide each query range is:
  //   - narrow (10):  more selective, more likely to miss → benefits SuRF more
  //   - wide (100):   less selective, shows SuRF on broader queries
  //
  // The lo/hi bounds are set in ReadOptions, which activates RangeMayMatch
  // in AddIterators → TableCache::NewIterator → table->RangeMayMatch(lo, hi).
  // Without lo/hi, RangeMayMatch is never called (this is why seekrandom
  // shows no SuRF advantage).
  // ====================================================================
  void DoSuRFRangeScan(ThreadState* thread, const char* benchmark_name,
                         int miss_pct, int range_width) {
    ReadOptions options;
    int found = 0;
    int total_scanned = 0;
    int miss_count = 0;
    int hit_count = 0;
    KeyBuffer lo_key;
    KeyBuffer hi_key;

    for (int i = 0; i < reads_; i++) {
      int k;
      // SuRF: decide whether this query is a miss (empty range) or hit
      // miss: query range starts above all inserted keys → guaranteed empty
      // hit:  query range starts inside inserted key space → likely finds keys
      if (miss_pct >= 100 || (miss_pct > 0 && thread->rand.Uniform(100) < static_cast<unsigned int>(miss_pct))) {
        // Miss: query above all inserted keys (FLAGS_num to 2*FLAGS_num)
        k = FLAGS_num + thread->rand.Uniform(FLAGS_num);
        miss_count++;
      } else {
        // Hit: query inside inserted key range (0 to FLAGS_num-1)
        k = thread->rand.Uniform(FLAGS_num);
        hit_count++;
      }

      lo_key.Set(k);
      const int hi_k = k + range_width;
      hi_key.Set(hi_k);

      // SuRF: set range bounds — this is what activates RangeMayMatch
      // in AddIterators → TableCache::NewIterator
      options.lo = lo_key.slice();
      options.hi = hi_key.slice();

      int64_t start = g_env->NowMicros();
      Iterator* iter = db_->NewIterator(options);
      iter->Seek(lo_key.slice());

      int scanned = 0;
      while (iter->Valid() &&
             iter->key().compare(hi_key.slice()) <= 0 &&
             scanned < 100) {
        found++;
        scanned++;
        iter->Next();
      }
      int64_t end = g_env->NowMicros();
      total_scanned += scanned;
      delete iter;
      RecordQueryEvent(benchmark_name, "range_scan", &lo_key.slice(),
                       &hi_key.slice(), scanned > 0,
                       static_cast<double>(end - start), end);
      thread->stats.FinishedSingleOp();
    }

    char msg[200];
    std::snprintf(msg, sizeof(msg),
                  "(%d keys scanned, miss=%d hit=%d, width=%d)",
                  total_scanned, miss_count, hit_count, range_width);
    thread->stats.AddMessage(msg);
  }

  // SuRF: 100% miss rate — all queries above inserted key range
  // This is the best case for SuRF: every SSTable can potentially be skipped
  void SuRFRangeScan100(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan100", /*miss_pct=*/100,
                    /*range_width=*/10);
  }

  // SuRF: 75% miss rate — 3 out of 4 queries are empty ranges
  // Realistic for time-series workloads querying recent windows
  void SuRFRangeScan75(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan75", /*miss_pct=*/75,
                    /*range_width=*/10);
  }

  // SuRF: 50% miss rate — half queries empty, half hit keys
  // Balanced workload showing intermediate SuRF benefit
  void SuRFRangeScan50(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan50", /*miss_pct=*/50,
                    /*range_width=*/10);
  }

  // SuRF: 25% miss rate — most queries find keys, few are empty
  // SuRF advantage should be small here since most ranges hit data
  void SuRFRangeScan25(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan25", /*miss_pct=*/25,
                    /*range_width=*/10);
  }

  // SuRF: 0% miss rate — all queries inside inserted key range
  // Worst case for SuRF: no SSTables can be skipped, SuRF overhead visible
  void SuRFRangeScan0(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan0", /*miss_pct=*/0,
                    /*range_width=*/10);
  }

  // SuRF: 100% miss rate with wide range (100 keys instead of 10)
  // Tests whether SuRF advantage holds on broader range queries
  void SuRFRangeScanWide(ThreadState* thread) {
    DoSuRFRangeScan(thread, "surfscan_wide", /*miss_pct=*/100,
                    /*range_width=*/100);
  }

  void DoDelete(ThreadState* thread, bool seq) {
    RandomGenerator gen;
    WriteBatch batch;
    Status s;
    KeyBuffer key;
    for (int i = 0; i < num_; i += entries_per_batch_) {
      batch.Clear();
      for (int j = 0; j < entries_per_batch_; j++) {
        const int k = seq ? i + j : (thread->rand.Uniform(FLAGS_num));
        key.Set(k);
        batch.Delete(key.slice());
        thread->stats.FinishedSingleOp();
      }
      s = db_->Write(write_options_, &batch);
      if (!s.ok()) {
        std::fprintf(stderr, "del error: %s\n", s.ToString().c_str());
        std::exit(1);
      }
    }
  }

  void DeleteSeq(ThreadState* thread) { DoDelete(thread, true); }

  void DeleteRandom(ThreadState* thread) { DoDelete(thread, false); }

  void ReadWhileWriting(ThreadState* thread) {
    if (thread->tid > 0) {
      ReadRandom(thread);
    } else {
      // Special thread that keeps writing until other threads are done.
      RandomGenerator gen;
      KeyBuffer key;
      while (true) {
        {
          MutexLock l(&thread->shared->mu);
          if (thread->shared->num_done + 1 >= thread->shared->num_initialized) {
            // Other threads have finished
            break;
          }
        }

        const int k = thread->rand.Uniform(FLAGS_num);
        key.Set(k);
        Status s =
            db_->Put(write_options_, key.slice(), gen.Generate(value_size_));
        if (!s.ok()) {
          std::fprintf(stderr, "put error: %s\n", s.ToString().c_str());
          std::exit(1);
        }
      }

      // Do not count any of the preceding work/delay in stats.
      thread->stats.Start();
    }
  }

  void Compact(ThreadState* thread) { db_->CompactRange(nullptr, nullptr); }

  void PrintStats(const char* key) {
    std::string stats;
    if (!db_->GetProperty(key, &stats)) {
      stats = "(failed)";
    }
    std::fprintf(stdout, "\n%s\n", stats.c_str());
  }

  static void WriteToFile(void* arg, const char* buf, int n) {
    reinterpret_cast<WritableFile*>(arg)->Append(Slice(buf, n));
  }

  void HeapProfile() {
    char fname[100];
    std::snprintf(fname, sizeof(fname), "%s/heap-%04d", FLAGS_db,
                  ++heap_counter_);
    WritableFile* file;
    Status s = g_env->NewWritableFile(fname, &file);
    if (!s.ok()) {
      std::fprintf(stderr, "%s\n", s.ToString().c_str());
      return;
    }
    bool ok = port::GetHeapProfile(WriteToFile, file);
    delete file;
    if (!ok) {
      std::fprintf(stderr, "heap profiling not supported\n");
      g_env->RemoveFile(fname);
    }
  }
};

}  // namespace leveldb

int main(int argc, char** argv) {
  FLAGS_write_buffer_size = leveldb::Options().write_buffer_size;
  FLAGS_max_file_size = leveldb::Options().max_file_size;
  FLAGS_block_size = leveldb::Options().block_size;
  FLAGS_open_files = leveldb::Options().max_open_files;
  std::string default_db_path;

  for (int i = 1; i < argc; i++) {
    double d;
    int n;
    char junk;
    if (leveldb::Slice(argv[i]).starts_with("--benchmarks=")) {
      FLAGS_benchmarks = argv[i] + strlen("--benchmarks=");
    } else if (sscanf(argv[i], "--compression_ratio=%lf%c", &d, &junk) == 1) {
      FLAGS_compression_ratio = d;
    } else if (sscanf(argv[i], "--histogram=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_histogram = n;
    } else if (sscanf(argv[i], "--comparisons=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_comparisons = n;
    } else if (sscanf(argv[i], "--use_existing_db=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_use_existing_db = n;
    } else if (sscanf(argv[i], "--reuse_logs=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_reuse_logs = n;
    } else if (sscanf(argv[i], "--compression=%d%c", &n, &junk) == 1 &&
               (n == 0 || n == 1)) {
      FLAGS_compression = n;
    } else if (sscanf(argv[i], "--num=%d%c", &n, &junk) == 1) {
      FLAGS_num = n;
    } else if (sscanf(argv[i], "--reads=%d%c", &n, &junk) == 1) {
      FLAGS_reads = n;
    } else if (sscanf(argv[i], "--threads=%d%c", &n, &junk) == 1) {
      FLAGS_threads = n;
    } else if (sscanf(argv[i], "--value_size=%d%c", &n, &junk) == 1) {
      FLAGS_value_size = n;
    } else if (sscanf(argv[i], "--write_buffer_size=%d%c", &n, &junk) == 1) {
      FLAGS_write_buffer_size = n;
    } else if (sscanf(argv[i], "--max_file_size=%d%c", &n, &junk) == 1) {
      FLAGS_max_file_size = n;
    } else if (sscanf(argv[i], "--block_size=%d%c", &n, &junk) == 1) {
      FLAGS_block_size = n;
    } else if (sscanf(argv[i], "--key_prefix=%d%c", &n, &junk) == 1) {
      FLAGS_key_prefix = n;
    } else if (sscanf(argv[i], "--cache_size=%d%c", &n, &junk) == 1) {
      FLAGS_cache_size = n;
    } else if (sscanf(argv[i], "--bloom_bits=%d%c", &n, &junk) == 1) {
      FLAGS_bloom_bits = n;
    } else if (sscanf(argv[i], "--open_files=%d%c", &n, &junk) == 1) {
      FLAGS_open_files = n;
    } else if (strncmp(argv[i], "--db=", 5) == 0) {
      FLAGS_db = argv[i] + 5;
    } else if (strncmp(argv[i], "--filter=", 9) == 0) {
      FLAGS_filter = argv[i] + 9;
    } else if (strncmp(argv[i], "--metrics_out=", 14) == 0) {
      FLAGS_metrics_out = argv[i] + 14;
    } else {
      std::fprintf(stderr, "Invalid flag '%s'\n", argv[i]);
      std::exit(1);
    }
  }

  leveldb::g_env = leveldb::Env::Default();

  // Choose a location for the test database if none given with --db=<path>
  if (FLAGS_db == nullptr) {
    leveldb::g_env->GetTestDirectory(&default_db_path);
    default_db_path += "/dbbench";
    FLAGS_db = default_db_path.c_str();
  }

  leveldb::JsonlWriter metrics_writer;
  if (FLAGS_metrics_out != nullptr) {
    if (!metrics_writer.Open(FLAGS_metrics_out)) {
      std::fprintf(stderr, "Cannot open metrics output file '%s'\n", FLAGS_metrics_out);
      std::exit(1);
    }
  }

  leveldb::Benchmark benchmark;
  benchmark.SetMetricsWriter(FLAGS_metrics_out ? &metrics_writer : nullptr);
  benchmark.Run();
  return 0;
}