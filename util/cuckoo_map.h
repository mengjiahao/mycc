
#ifndef MYCC_UTIL_CUCKOO_MAP_H_
#define MYCC_UTIL_CUCKOO_MAP_H_

#include <math.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

/** namespace CuckooCache provides high performance cache primitives
 *
 * Summary:
 *
 * 1) bit_packed_atomic_flags is bit-packed atomic flags for garbage collection
 *
 * 2) cache is a cache which is performant in memory usage and lookup speed. It
 * is lockfree for erase operations. Elements are lazily erased on the next
 * insert.
 */

namespace CuckooCache
{
/** bit_packed_atomic_flags implements a container for garbage collection flags
 * that is only thread unsafe on calls to setup. This class bit-packs collection
 * flags for memory efficiency.
 *
 * All operations are std::memory_order_relaxed so external mechanisms must
 * ensure that writes and reads are properly synchronized.
 *
 * On setup(n), all bits up to n are marked as collected.
 *
 * Under the hood, because it is an 8-bit type, it makes sense to use a multiple
 * of 8 for setup, but it will be safe if that is not the case as well.
 *
 */
class bit_packed_atomic_flags
{
  std::unique_ptr<std::atomic<uint8_t>[]> mem;

public:
  /** No default constructor as there must be some size */
  bit_packed_atomic_flags() = delete;

  /**
     * bit_packed_atomic_flags constructor creates memory to sufficiently
     * keep track of garbage collection information for size entries.
     *
     * @param size the number of elements to allocate space for
     *
     * @post bit_set, bit_unset, and bit_is_set function properly forall x. x <
     * size
     * @post All calls to bit_is_set (without subsequent bit_unset) will return
     * true.
     */
  explicit bit_packed_atomic_flags(uint32_t size)
  {
    // pad out the size if needed
    size = (size + 7) / 8;
    mem.reset(new std::atomic<uint8_t>[size]);
    for (uint32_t i = 0; i < size; ++i)
      mem[i].store(0xFF);
  };

  /** setup marks all entries and ensures that bit_packed_atomic_flags can store
     * at least size entries
     *
     * @param b the number of elements to allocate space for
     * @post bit_set, bit_unset, and bit_is_set function properly forall x. x <
     * b
     * @post All calls to bit_is_set (without subsequent bit_unset) will return
     * true.
     */
  inline void setup(uint32_t b)
  {
    bit_packed_atomic_flags d(b);
    std::swap(mem, d.mem);
  }

  /** bit_set sets an entry as discardable.
     *
     * @param s the index of the entry to bit_set.
     * @post immediately subsequent call (assuming proper external memory
     * ordering) to bit_is_set(s) == true.
     *
     */
  inline void bit_set(uint32_t s)
  {
    mem[s >> 3].fetch_or(1 << (s & 7), std::memory_order_relaxed);
  }

  /**  bit_unset marks an entry as something that should not be overwritten
     *
     * @param s the index of the entry to bit_unset.
     * @post immediately subsequent call (assuming proper external memory
     * ordering) to bit_is_set(s) == false.
     */
  inline void bit_unset(uint32_t s)
  {
    mem[s >> 3].fetch_and(~(1 << (s & 7)), std::memory_order_relaxed);
  }

  /** bit_is_set queries the table for discardability at s
     *
     * @param s the index of the entry to read.
     * @returns if the bit at index s was set.
     * */
  inline bool bit_is_set(uint32_t s) const
  {
    return (1 << (s & 7)) & mem[s >> 3].load(std::memory_order_relaxed);
  }
};

/** cache implements a cache with properties similar to a cuckoo-set
 *
 *  The cache is able to hold up to (~(uint32_t)0) - 1 elements.
 *
 *  Read Operations:
 *      - contains(*, false)
 *
 *  Read+Erase Operations:
 *      - contains(*, true)
 *
 *  Erase Operations:
 *      - allow_erase()
 *
 *  Write Operations:
 *      - setup()
 *      - setup_bytes()
 *      - insert()
 *      - please_keep()
 *
 *  Synchronization Free Operations:
 *      - invalid()
 *      - compute_hashes()
 *
 * User Must Guarantee:
 *
 * 1) Write Requires synchronized access (e.g., a lock)
 * 2) Read Requires no concurrent Write, synchronized with the last insert.
 * 3) Erase requires no concurrent Write, synchronized with last insert.
 * 4) An Erase caller must release all memory before allowing a new Writer.
 *
 *
 * Note on function names:
 *   - The name "allow_erase" is used because the real discard happens later.
 *   - The name "please_keep" is used because elements may be erased anyways on insert.
 *
 * @tparam Element should be a movable and copyable type
 * @tparam Hash should be a function/callable which takes a template parameter
 * hash_select and an Element and extracts a hash from it. Should return
 * high-entropy uint32_t hashes for `Hash h; h<0>(e) ... h<7>(e)`.
 */
template <typename Element, typename Hash>
class cache
{
private:
  /** table stores all the elements */
  std::vector<Element> table;

  /** size stores the total available slots in the hash table */
  uint32_t size;

  /** The bit_packed_atomic_flags array is marked mutable because we want
     * garbage collection to be allowed to occur from const methods */
  mutable bit_packed_atomic_flags collection_flags;

  /** epoch_flags tracks how recently an element was inserted into
     * the cache. true denotes recent, false denotes not-recent. See insert()
     * method for full semantics.
     */
  mutable std::vector<bool> epoch_flags;

  /** epoch_heuristic_counter is used to determine when an epoch might be aged
     * & an expensive scan should be done.  epoch_heuristic_counter is
     * decremented on insert and reset to the new number of inserts which would
     * cause the epoch to reach epoch_size when it reaches zero.
     */
  uint32_t epoch_heuristic_counter;

  /** epoch_size is set to be the number of elements supposed to be in a
     * epoch. When the number of non-erased elements in an epoch
     * exceeds epoch_size, a new epoch should be started and all
     * current entries demoted. epoch_size is set to be 45% of size because
     * we want to keep load around 90%, and we support 3 epochs at once --
     * one "dead" which has been erased, one "dying" which has been marked to be
     * erased next, and one "living" which new inserts add to.
     */
  uint32_t epoch_size;

  /** depth_limit determines how many elements insert should try to replace.
     * Should be set to log2(n)*/
  uint8_t depth_limit;

  /** hash_function is a const instance of the hash function. It cannot be
     * static or initialized at call time as it may have internal state (such as
     * a nonce).
     * */
  const Hash hash_function;

  /** compute_hashes is convenience for not having to write out this
     * expression everywhere we use the hash values of an Element.
     *
     * We need to map the 32-bit input hash onto a hash bucket in a range [0, size) in a
     *  manner which preserves as much of the hash's uniformity as possible.  Ideally
     *  this would be done by bitmasking but the size is usually not a power of two.
     *
     * The naive approach would be to use a mod -- which isn't perfectly uniform but so
     *  long as the hash is much larger than size it is not that bad.  Unfortunately,
     *  mod/division is fairly slow on ordinary microprocessors (e.g. 90-ish cycles on
     *  haswell, ARM doesn't even have an instruction for it.); when the divisor is a
     *  constant the compiler will do clever tricks to turn it into a multiply+add+shift,
     *  but size is a run-time value so the compiler can't do that here.
     *
     * One option would be to implement the same trick the compiler uses and compute the
     *  constants for exact division based on the size, as described in "{N}-bit Unsigned
     *  Division via {N}-bit Multiply-Add" by Arch D. Robison in 2005. But that code is
     *  somewhat complicated and the result is still slower than other options:
     *
     * Instead we treat the 32-bit random number as a Q32 fixed-point number in the range
     *  [0,1) and simply multiply it by the size.  Then we just shift the result down by
     *  32-bits to get our bucket number.  The results has non-uniformity the same as a
     *  mod, but it is much faster to compute. More about this technique can be found at
     *  http://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
     *
     * The resulting non-uniformity is also more equally distributed which would be
     *  advantageous for something like linear probing, though it shouldn't matter
     *  one way or the other for a cuckoo table.
     *
     * The primary disadvantage of this approach is increased intermediate precision is
     *  required but for a 32-bit random number we only need the high 32 bits of a
     *  32*32->64 multiply, which means the operation is reasonably fast even on a
     *  typical 32-bit processor.
     *
     * @param e the element whose hashes will be returned
     * @returns std::array<uint32_t, 8> of deterministic hashes derived from e
     */
  inline std::array<uint32_t, 8> compute_hashes(const Element &e) const
  {
    return {{(uint32_t)((hash_function.template operator()<0>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<1>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<2>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<3>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<4>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<5>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<6>(e) * (uint64_t)size) >> 32),
             (uint32_t)((hash_function.template operator()<7>(e) * (uint64_t)size) >> 32)}};
  }

  /* end
     * @returns a constexpr index that can never be inserted to */
  constexpr uint32_t invalid() const
  {
    return ~(uint32_t)0;
  }

  /** allow_erase marks the element at index n as discardable. Threadsafe
     * without any concurrent insert.
     * @param n the index to allow erasure of
     */
  inline void allow_erase(uint32_t n) const
  {
    collection_flags.bit_set(n);
  }

  /** please_keep marks the element at index n as an entry that should be kept.
     * Threadsafe without any concurrent insert.
     * @param n the index to prioritize keeping
     */
  inline void please_keep(uint32_t n) const
  {
    collection_flags.bit_unset(n);
  }

  /** epoch_check handles the changing of epochs for elements stored in the
     * cache. epoch_check should be run before every insert.
     *
     * First, epoch_check decrements and checks the cheap heuristic, and then does
     * a more expensive scan if the cheap heuristic runs out. If the expensive
     * scan succeeds, the epochs are aged and old elements are allow_erased. The
     * cheap heuristic is reset to retrigger after the worst case growth of the
     * current epoch's elements would exceed the epoch_size.
     */
  void epoch_check()
  {
    if (epoch_heuristic_counter != 0)
    {
      --epoch_heuristic_counter;
      return;
    }
    // count the number of elements from the latest epoch which
    // have not been erased.
    uint32_t epoch_unused_count = 0;
    for (uint32_t i = 0; i < size; ++i)
      epoch_unused_count += epoch_flags[i] &&
                            !collection_flags.bit_is_set(i);
    // If there are more non-deleted entries in the current epoch than the
    // epoch size, then allow_erase on all elements in the old epoch (marked
    // false) and move all elements in the current epoch to the old epoch
    // but do not call allow_erase on their indices.
    if (epoch_unused_count >= epoch_size)
    {
      for (uint32_t i = 0; i < size; ++i)
        if (epoch_flags[i])
          epoch_flags[i] = false;
        else
          allow_erase(i);
      epoch_heuristic_counter = epoch_size;
    }
    else
      // reset the epoch_heuristic_counter to next do a scan when worst
      // case behavior (no intermittent erases) would exceed epoch size,
      // with a reasonable minimum scan size.
      // Ordinarily, we would have to sanity check std::min(epoch_size,
      // epoch_unused_count), but we already know that `epoch_unused_count
      // < epoch_size` in this branch
      epoch_heuristic_counter = std::max(1u, std::max(epoch_size / 16,
                                                      epoch_size - epoch_unused_count));
  }

public:
  /** You must always construct a cache with some elements via a subsequent
     * call to setup or setup_bytes, otherwise operations may segfault.
     */
  cache() : table(), size(), collection_flags(0), epoch_flags(),
            epoch_heuristic_counter(), epoch_size(), depth_limit(0), hash_function()
  {
  }

  /** setup initializes the container to store no more than new_size
     * elements.
     *
     * setup should only be called once.
     *
     * @param new_size the desired number of elements to store
     * @returns the maximum number of elements storable
     **/
  uint32_t setup(uint32_t new_size)
  {
    // depth_limit must be at least one otherwise errors can occur.
    depth_limit = static_cast<uint8_t>(std::log2(static_cast<float>(std::max((uint32_t)2, new_size))));
    size = std::max<uint32_t>(2, new_size);
    table.resize(size);
    collection_flags.setup(size);
    epoch_flags.resize(size);
    // Set to 45% as described above
    epoch_size = std::max((uint32_t)1, (45 * size) / 100);
    // Initially set to wait for a whole epoch
    epoch_heuristic_counter = epoch_size;
    return size;
  }

  /** setup_bytes is a convenience function which accounts for internal memory
     * usage when deciding how many elements to store. It isn't perfect because
     * it doesn't account for any overhead (struct size, MallocUsage, collection
     * and epoch flags). This was done to simplify selecting a power of two
     * size. In the expected use case, an extra two bits per entry should be
     * negligible compared to the size of the elements.
     *
     * @param bytes the approximate number of bytes to use for this data
     * structure.
     * @returns the maximum number of elements storable (see setup()
     * documentation for more detail)
     */
  uint32_t setup_bytes(size_t bytes)
  {
    return setup(bytes / sizeof(Element));
  }

  /** insert loops at most depth_limit times trying to insert a hash
     * at various locations in the table via a variant of the Cuckoo Algorithm
     * with eight hash locations.
     *
     * It drops the last tried element if it runs out of depth before
     * encountering an open slot.
     *
     * Thus
     *
     * insert(x);
     * return contains(x, false);
     *
     * is not guaranteed to return true.
     *
     * @param e the element to insert
     * @post one of the following: All previously inserted elements and e are
     * now in the table, one previously inserted element is evicted from the
     * table, the entry attempted to be inserted is evicted.
     *
     */
  inline void insert(Element e)
  {
    epoch_check();
    uint32_t last_loc = invalid();
    bool last_epoch = true;
    std::array<uint32_t, 8> locs = compute_hashes(e);
    // Make sure we have not already inserted this element
    // If we have, make sure that it does not get deleted
    for (uint32_t loc : locs)
      if (table[loc] == e)
      {
        please_keep(loc);
        epoch_flags[loc] = last_epoch;
        return;
      }
    for (uint8_t depth = 0; depth < depth_limit; ++depth)
    {
      // First try to insert to an empty slot, if one exists
      for (uint32_t loc : locs)
      {
        if (!collection_flags.bit_is_set(loc))
          continue;
        table[loc] = std::move(e);
        please_keep(loc);
        epoch_flags[loc] = last_epoch;
        return;
      }
      /** Swap with the element at the location that was
            * not the last one looked at. Example:
            *
            * 1) On first iteration, last_loc == invalid(), find returns last, so
            *    last_loc defaults to locs[0].
            * 2) On further iterations, where last_loc == locs[k], last_loc will
            *    go to locs[k+1 % 8], i.e., next of the 8 indices wrapping around
            *    to 0 if needed.
            *
            * This prevents moving the element we just put in.
            *
            * The swap is not a move -- we must switch onto the evicted element
            * for the next iteration.
            */
      last_loc = locs[(1 + (std::find(locs.begin(), locs.end(), last_loc) - locs.begin())) & 7];
      std::swap(table[last_loc], e);
      // Can't std::swap a std::vector<bool>::reference and a bool&.
      bool epoch = last_epoch;
      last_epoch = epoch_flags[last_loc];
      epoch_flags[last_loc] = epoch;

      // Recompute the locs -- unfortunately happens one too many times!
      locs = compute_hashes(e);
    }
  }

  /* contains iterates through the hash locations for a given element
     * and checks to see if it is present.
     *
     * contains does not check garbage collected state (in other words,
     * garbage is only collected when the space is needed), so:
     *
     * insert(x);
     * if (contains(x, true))
     *     return contains(x, false);
     * else
     *     return true;
     *
     * executed on a single thread will always return true!
     *
     * This is a great property for re-org performance for example.
     *
     * contains returns a bool set true if the element was found.
     *
     * @param e the element to check
     * @param erase
     *
     * @post if erase is true and the element is found, then the garbage collect
     * flag is set
     * @returns true if the element is found, false otherwise
     */
  inline bool contains(const Element &e, const bool erase) const
  {
    std::array<uint32_t, 8> locs = compute_hashes(e);
    for (uint32_t loc : locs)
      if (table[loc] == e)
      {
        if (erase)
          allow_erase(loc);
        return true;
      }
    return false;
  }
};

} // namespace CuckooCache

// Class for efficiently storing key->value mappings when the size is
// known in advance and the keys are pre-hashed into uint64s.
// Keys should have "good enough" randomness (be spread across the
// entire 64 bit space).
//
// Important:  Clients wishing to use deterministic keys must
// ensure that their keys fall in the range 0 .. (uint64max-1);
// the table uses 2^64-1 as the "not occupied" flag.
//
// Inserted keys must be unique, and there are no update
// or delete functions (until some subsequent use of this table
// requires them).
//
// Threads must synchronize their access to a PresizedCuckooMap.
//
// The cuckoo hash table is 4-way associative (each "bucket" has 4
// "slots" for key/value entries).  Uses breadth-first-search to find
// a good cuckoo path with less data movement (see
// http://www.cs.cmu.edu/~dga/papers/cuckoo-eurosys14.pdf )

namespace presized_cuckoo_map
{

// Utility function to compute (x * y) >> 64, or "multiply high".
// On x86-64, this is a single instruction, but not all platforms
// support the __uint128_t type, so we provide a generic
// implementation as well.
inline uint64_t multiply_high_u64(uint64_t x, uint64_t y)
{
#if defined(__SIZEOF_INT128__)
  return (uint64_t)(((__uint128_t)x * (__uint128_t)y) >> 64);
#else
  // For platforms without int128 support, do it the long way.
  uint64_t x_lo = x & 0xffffffff;
  uint64_t x_hi = x >> 32;
  uint64_t buckets_lo = y & 0xffffffff;
  uint64_t buckets_hi = y >> 32;
  uint64_t prod_hi = x_hi * buckets_hi;
  uint64_t prod_lo = x_lo * buckets_lo;
  uint64_t prod_mid1 = x_hi * buckets_lo;
  uint64_t prod_mid2 = x_lo * buckets_hi;
  uint64_t carry =
      ((prod_mid1 & 0xffffffff) + (prod_mid2 & 0xffffffff) + (prod_lo >> 32)) >>
      32;
  return prod_hi + (prod_mid1 >> 32) + (prod_mid2 >> 32) + carry;
#endif
}

} // namespace presized_cuckoo_map

template <class value>
class PresizedCuckooMap
{
public:
  // The key type is fixed as a pre-hashed key for this specialized use.
  typedef uint64_t key_type;

  explicit PresizedCuckooMap(uint64_t num_entries) { Clear(num_entries); }

  void Clear(uint64_t num_entries)
  {
    cpq_.reset(new CuckooPathQueue());
    double n(num_entries);
    n /= kLoadFactor;
    num_buckets_ = (static_cast<uint64_t>(n) / kSlotsPerBucket);
    // Very small cuckoo tables don't work, because the probability
    // of having same-bucket hashes is large.  We compromise for those
    // uses by having a larger static starting size.
    num_buckets_ += 32;
    Bucket empty_bucket;
    for (int32_t i = 0; i < kSlotsPerBucket; i++)
    {
      empty_bucket.keys[i] = kUnusedSlot;
    }
    buckets_.clear();
    buckets_.resize(num_buckets_, empty_bucket);
  }

  // Returns false if k is already in table or if the table
  // is full; true otherwise.
  bool InsertUnique(const key_type k, const value &v)
  {
    uint64_t tk = key_transform(k);
    uint64_t b1 = fast_map_to_buckets(tk);
    uint64_t b2 = fast_map_to_buckets(h2(tk));

    // Merged find and duplicate checking.
    uint64_t target_bucket = 0;
    int32_t target_slot = kNoSpace;

    for (auto bucket : {b1, b2})
    {
      Bucket *bptr = &buckets_[bucket];
      for (int32_t slot = 0; slot < kSlotsPerBucket; slot++)
      {
        if (bptr->keys[slot] == k)
        { // Duplicates are not allowed.
          return false;
        }
        else if (target_slot == kNoSpace && bptr->keys[slot] == kUnusedSlot)
        {
          target_bucket = bucket;
          target_slot = slot;
        }
      }
    }

    if (target_slot != kNoSpace)
    {
      InsertInternal(tk, v, target_bucket, target_slot);
      return true;
    }

    return CuckooInsert(tk, v, b1, b2);
  }

  // Returns true if found.  Sets *out = value.
  bool Find(const key_type k, value *out) const
  {
    uint64_t tk = key_transform(k);
    return FindInBucket(k, fast_map_to_buckets(tk), out) ||
           FindInBucket(k, fast_map_to_buckets(h2(tk)), out);
  }

  int64_t MemoryUsed() const
  {
    return sizeof(PresizedCuckooMap<value>) + sizeof(CuckooPathQueue);
  }

private:
  static constexpr int32_t kSlotsPerBucket = 4;

  // The load factor is chosen slightly conservatively for speed and
  // to avoid the need for a table rebuild on insertion failure.
  // 0.94 is achievable, but 0.85 is faster and keeps the code simple
  // at the cost of a small amount of memory.
  // NOTE:  0 < kLoadFactor <= 1.0
  static constexpr double kLoadFactor = 0.85;

  // Cuckoo insert:  The maximum number of entries to scan should be ~400
  // (Source:  Personal communication with Michael Mitzenmacher;  empirical
  // experiments validate.).  After trying 400 candidate locations, declare
  // the table full - it's probably full of unresolvable cycles.  Less than
  // 400 reduces max occupancy;  much more results in very poor performance
  // around the full point.  For (2,4) a max BFS path len of 5 results in ~682
  // nodes to visit, calculated below, and is a good value.

  static constexpr uint8_t kMaxBFSPathLen = 5;

  // Constants for BFS cuckoo path search:
  // The visited list must be maintained for all but the last level of search
  // in order to trace back the path.  The BFS search has two roots
  // and each can go to a total depth (including the root) of 5.
  // The queue must be sized for 2 * \sum_{k=0...4}{kSlotsPerBucket^k} = 682.
  // The visited queue, however, does not need to hold the deepest level,
  // and so it is sized 2 * \sum{k=0...3}{kSlotsPerBucket^k} = 170
  static constexpr int32_t kMaxQueueSize = 682;
  static constexpr int32_t kVisitedListSize = 170;

  static constexpr int32_t kNoSpace = -1; // SpaceAvailable return
  static constexpr uint64_t kUnusedSlot = ~(0ULL);

  // Buckets are organized with key_types clustered for access speed
  // and for compactness while remaining aligned.
  struct Bucket
  {
    key_type keys[kSlotsPerBucket];
    value values[kSlotsPerBucket];
  };

  // Insert uses the BFS optimization (search before moving) to reduce
  // the number of cache lines dirtied during search.

  struct CuckooPathEntry
  {
    uint64_t bucket;
    int32_t depth;
    int32_t parent;      // To index in the visited array.
    int32_t parent_slot; // Which slot in our parent did we come from?  -1 == root.
  };

  // CuckooPathQueue is a trivial circular queue for path entries.
  // The caller is responsible for not inserting more than kMaxQueueSize
  // entries.  Each PresizedCuckooMap has one (heap-allocated) CuckooPathQueue
  // that it reuses across inserts.
  class CuckooPathQueue
  {
  public:
    CuckooPathQueue() : head_(0), tail_(0) {}

    void push_back(CuckooPathEntry e)
    {
      queue_[tail_] = e;
      tail_ = (tail_ + 1) % kMaxQueueSize;
    }

    CuckooPathEntry pop_front()
    {
      CuckooPathEntry &e = queue_[head_];
      head_ = (head_ + 1) % kMaxQueueSize;
      return e;
    }

    bool empty() const { return head_ == tail_; }

    bool full() const { return ((tail_ + 1) % kMaxQueueSize) == head_; }

    void reset() { head_ = tail_ = 0; }

  private:
    CuckooPathEntry queue_[kMaxQueueSize];
    int32_t head_;
    int32_t tail_;
  };

  typedef std::array<CuckooPathEntry, kMaxBFSPathLen> CuckooPath;

  // Callers are expected to have pre-hashed the keys into a uint64_t
  // and are expected to be able to handle (a very low rate) of
  // collisions, OR must ensure that their keys are always in
  // the range 0 - (uint64max - 1).  This transforms 'not found flag'
  // keys into something else.
  inline uint64_t key_transform(const key_type k) const
  {
    return k + (k == kUnusedSlot);
  }

  // h2 performs a very quick mix of h to generate the second bucket hash.
  // Assumes there is plenty of remaining entropy in the initial h.
  inline uint64_t h2(uint64_t h) const
  {
    const uint64_t m = 0xc6a4a7935bd1e995;
    return m * ((h >> 32) | (h << 32));
  }

  // alt_bucket identifies the "other" bucket for key k, where
  // other is "the one that isn't bucket b"
  inline uint64_t alt_bucket(key_type k, uint64_t b) const
  {
    if (fast_map_to_buckets(k) != b)
    {
      return fast_map_to_buckets(k);
    }
    return fast_map_to_buckets(h2(k));
  }

  inline void InsertInternal(key_type k, const value &v, uint64_t b, int32_t slot)
  {
    Bucket *bptr = &buckets_[b];
    bptr->keys[slot] = k;
    bptr->values[slot] = v;
  }

  // For the associative cuckoo table, check all of the slots in
  // the bucket to see if the key is present.
  bool FindInBucket(key_type k, uint64_t b, value *out) const
  {
    const Bucket &bref = buckets_[b];
    for (int32_t i = 0; i < kSlotsPerBucket; i++)
    {
      if (bref.keys[i] == k)
      {
        *out = bref.values[i];
        return true;
      }
    }
    return false;
  }

  //  returns either kNoSpace or the index of an
  //  available slot (0 <= slot < kSlotsPerBucket)
  inline int32_t SpaceAvailable(uint64_t bucket) const
  {
    const Bucket &bref = buckets_[bucket];
    for (int32_t i = 0; i < kSlotsPerBucket; i++)
    {
      if (bref.keys[i] == kUnusedSlot)
      {
        return i;
      }
    }
    return kNoSpace;
  }

  inline void CopyItem(uint64_t src_bucket, int32_t src_slot, uint64_t dst_bucket,
                       int32_t dst_slot)
  {
    Bucket &src_ref = buckets_[src_bucket];
    Bucket &dst_ref = buckets_[dst_bucket];
    dst_ref.keys[dst_slot] = src_ref.keys[src_slot];
    dst_ref.values[dst_slot] = src_ref.values[src_slot];
  }

  bool CuckooInsert(key_type k, const value &v, uint64_t b1, uint64_t b2)
  {
    int32_t visited_end = 0;
    cpq_->reset();

    cpq_->push_back({b1, 1, 0, 0}); // Note depth starts at 1.
    cpq_->push_back({b2, 1, 0, 0});

    while (!cpq_->empty())
    {
      CuckooPathEntry e = cpq_->pop_front();
      int32_t free_slot;
      free_slot = SpaceAvailable(e.bucket);
      if (free_slot != kNoSpace)
      {
        while (e.depth > 1)
        {
          // "copy" instead of "swap" because one entry is always zero.
          // After, write target key/value over top of last copied entry.
          CuckooPathEntry parent = visited_[e.parent];
          CopyItem(parent.bucket, e.parent_slot, e.bucket, free_slot);
          free_slot = e.parent_slot;
          e = parent;
        }
        InsertInternal(k, v, e.bucket, free_slot);
        return true;
      }
      else
      {
        if (e.depth < (kMaxBFSPathLen))
        {
          auto parent_index = visited_end;
          visited_[visited_end] = e;
          visited_end++;
          // Don't always start with the same slot, to even out the path depth.
          int32_t start_slot = (k + e.bucket) % kSlotsPerBucket;
          const Bucket &bref = buckets_[e.bucket];
          for (int32_t i = 0; i < kSlotsPerBucket; i++)
          {
            int32_t slot = (start_slot + i) % kSlotsPerBucket;
            uint64_t next_bucket = alt_bucket(bref.keys[slot], e.bucket);
            // Optimization:  Avoid single-step cycles (from e, don't
            // add a child node that is actually e's parent).
            uint64_t e_parent_bucket = visited_[e.parent].bucket;
            if (next_bucket != e_parent_bucket)
            {
              cpq_->push_back({next_bucket, e.depth + 1, parent_index, slot});
            }
          }
        }
      }
    }

    printf("Cuckoo path finding failed: Table too small?\n");
    return false;
  }

  inline uint64_t fast_map_to_buckets(uint64_t x) const
  {
    // Map x (uniform in 2^64) to the range [0, num_buckets_ -1]
    // using Lemire's alternative to modulo reduction:
    // http://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction/
    // Instead of x % N, use (x * N) >> 64.
    return presized_cuckoo_map::multiply_high_u64(x, num_buckets_);
  }

  // Set upon initialization: num_entries / kLoadFactor / kSlotsPerBucket.
  uint64_t num_buckets_;
  std::vector<Bucket> buckets_;

  std::unique_ptr<CuckooPathQueue> cpq_;
  CuckooPathEntry visited_[kVisitedListSize];

  DISALLOW_COPY_AND_ASSIGN(PresizedCuckooMap);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CUCKOO_MAP_H_