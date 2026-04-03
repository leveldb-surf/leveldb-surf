// util/surf_filter.cc 
// Week 2: SuRF-based filter policy that replaces the Bloom filter

#include "leveldb/filter_policy.h"
#include "leveldb/slice.h"
#include "surf/surf.hpp" // SuRF headers were copied into include/surf/

#include <string>
#include <vector>

namespace leveldb {
    namespace {
        class SuRFPolicy : public FilterPolicy {
            public:
            ~SuRFPolicy() override = default;
            
            // ----------------------------------
            // Name()
            // Stored inside the metaindex block of every SSTable
            // On read, LevelDB looks for "filter." + Name().
            // If it does not match, then the filter is ignored, so this SHOULD NOT be changed
            const char* Name() const override {
                return "leveldb.SuRFFilter";
            }


            // -------------------------------------------
            // CreateFilter()
            // Called once per SSTable during compaction, all keys arrive here together instead of every 2KB
            // keys[0..n-1] are sorted keys (InternalFilterPolicy has stripped the 8-byte internal suffix already)
            // Append the serialized SuRF bytes to *dst
            void CreateFilter(const Slice* keys, int n, std::string* dst) const override {
                if (n == 0) return; // safe fallback so SuRF's constructor doesnt crash or produce meaningless trie

                // TODO: convert keys to the format SuRF expects
                std::vector<std::string> key_strs;
                key_strs.reserve(n);
                for(int i = 0; i < n; i++) {
                    key_strs.push_back(keys[i].ToString());
                }

                // TODO: construct SuRF from key_strs

                // TODO: serialize surf into dst
                // (check surf.hpp for the serialize API)
            }


            // --------------------------------------------
            // KeyMayMatch()
            // returns false  ---> key is DEFINITELY absent (must be correct)
            // returns true   ---> key might be present     (false positives OK)
            //
            // safety: if deserialization fails, return true (safety fallback)
            bool KeyMayMatch(const Slice& key, const Slice& filter) const override {
                if (filter.empty()) return true; // safe fallback

                // TODO: deserialize SuRF from filter bytes
                // surf::SuRF surf = surf::SuRF::deSerialize(...);

                //TODO: return surf lookup key

                return true; // placeholder - remove when implemented
            }


            // --------------------------------------------
            // RangeMayMatch() --> NEW METHOD
            // returns false  ---> NO key exists in [lo, hi] (must be correct)
            // returns true   ---> some key might be present (false positives OK)
            //
            // safety: if deserialization fails, return true (safety fallback)
            bool RangeMayMatch(const Slice& lo, const Slice& hi, const Slice& filter) const override {
                if (filter.empty()) return true; // safe fallback

                // TODO: deserialize SuRF from filter bytes
                // surf::SuRF surf = surf::SuRF::deSerialize(...);

                //TODO: return surf lookup range, lo and hi inclusive

                return true; // placeholder - remove when implemented
            }
        };
    } // namespace

    // Factory function - used in db_bench.cc (Week 4) and tests
    const FilterPolicy* NewSuRFFilterPolicy() {
        return new SuRFPolicy();
    }
} // namespace leveldb
