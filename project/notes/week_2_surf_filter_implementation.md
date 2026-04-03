# Week 2 Notes — SuRF Filter Implementation

## What Week 2 Is About
Replace LevelDB's Bloom filter with SuRF (Succinct Range Filter).
Bloom filter can only answer point queries ("is this exact key here?").
SuRF can also answer range queries ("does any key between lo and hi exist here?").
This lets LevelDB skip entire SSTables during range scans instead of opening them wastefully.

---

## Files Modified / Created

### `project/filter_policy.h` (modified from original)
Two changes from the original LevelDB file:

**1. Added `RangeMayMatch` as a virtual method inside the `FilterPolicy` class, right after `KeyMayMatch`:**
```cpp
virtual bool RangeMayMatch(const Slice& lo, const Slice& hi,
                           const Slice& filter) const {
  return true;
}
```
- Not pure virtual (no `= 0`) because old filters like Bloom don't implement it.
  The default `return true` means "maybe has keys" — safe fallback, never causes data loss.
- No `LEVELDB_EXPORT` needed — virtual methods inside a class are exported automatically
  because the class itself already has `LEVELDB_EXPORT`.

**2. Added factory function declaration at the bottom alongside `NewBloomFilterPolicy`:**
```cpp
LEVELDB_EXPORT const FilterPolicy* NewSuRFFilterPolicy();
```
- `LEVELDB_EXPORT` is required here because this is a free (standalone) function,
  not a class method. Free functions must be explicitly exported.
- This is just the declaration. The actual body lives in `surf_filter.cc`.
- Same pattern as Bloom: declared in `filter_policy.h`, defined in `bloom.cc`.
  We do the same: declared here, defined in `surf_filter.cc`.

---

### `project/surf_filter.cc` (new file)
Full SuRF filter policy implementation. Structure:

**Anonymous namespace wrapping `SuRFPolicy` class:**
- Makes `SuRFPolicy` invisible outside this file. Only `NewSuRFFilterPolicy()` is visible.
- Same pattern as `bloom.cc`.

**`Name()`**
```cpp
return "leveldb.SuRFFilter";
```
- Stored in every SSTable's metaindex block under `"filter.leveldb.SuRFFilter"`.
- On read, LevelDB looks for this exact string. Mismatch = filter silently ignored.
- Never change this once data has been written with it.

**`CreateFilter(keys, n, dst)`**
- Called once per SSTable during compaction (after the Week 2 filter_block.cc fix,
  all keys arrive here together instead of in 2KB chunks).
- `keys[0..n-1]` are sorted user keys. InternalFilterPolicy has already stripped
  the 8-byte internal suffix (sequence number + type), so keys are clean.
- Guard `if (n == 0) return` prevents SuRF constructor crash on empty input.
- Converts `Slice*` array to `vector<string>` because SuRF expects that format.
- TODOs: construct `surf::SuRF surf(key_strs)`, serialize into `dst`.

**`KeyMayMatch(key, filter)`**
- `false` = key definitely absent (must always be correct — never a false negative).
- `true` = key might be present (false positives are acceptable).
- `if (filter.empty()) return true` — safe fallback if deserialization fails.
- TODO: deserialize SuRF from `filter` bytes, call `surf.lookupKey(key.ToString())`.

**`RangeMayMatch(lo, hi, filter)`**
- Same safety contract as `KeyMayMatch` but for a range.
- `false` = no key in [lo, hi] (must always be correct).
- `true` = some key might exist (false positives acceptable).
- `if (filter.empty()) return true` — safe fallback.
- TODO: deserialize SuRF, call `surf.lookupRange(lo, true, hi, true)`.
- This is the new method that enables SSTable skipping on range scans.

**Factory function (outside anonymous namespace, inside `leveldb` namespace):**
```cpp
const FilterPolicy* NewSuRFFilterPolicy() {
    return new SuRFPolicy();
}
```
- Returns `new SuRFPolicy()` — same pattern as `NewBloomFilterPolicy` in `bloom.cc`.
- Caller owns the pointer and is responsible for deleting it.
- Used in `db_bench.cc` (Week 4) to switch benchmarks from Bloom to SuRF.

---

## Current Status
- [x] `filter_policy.h` — `RangeMayMatch` added, `NewSuRFFilterPolicy` declared
- [x] `surf_filter.cc` — skeleton compiles, all methods stubbed with safe `return true`
- [ ] `surf_filter.cc` — fill in real SuRF constructor, serialize, deserialize, lookups
- [ ] `filter_block.cc` — fix 2KB problem (buffer all keys, call CreateFilter once in Finish)
