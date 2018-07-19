
#ifndef MYCC_UTIL_MAP_UTIL_H_
#define MYCC_UTIL_MAP_UTIL_H_

#include <assert.h>
#include <math.h>
#include <string.h>
#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <ostream>
#include <utility>
#include <unordered_map>
#include <utility>
#include <vector>
#include "math_util.h"
#include "types_util.h"
#include "hash_util.h"

namespace mycc
{
namespace util
{

template <class T>
struct DereferencingComparator
{
  bool operator()(const T a, const T b) const { return *a < *b; }
};

/** STL-like map container that only keeps the N elements with the highest value. */
template <typename K, typename V>
class LimitedMap
{
public:
  typedef K key_type;
  typedef V mapped_type;
  typedef std::pair<const key_type, mapped_type> value_type;
  typedef typename std::map<K, V>::const_iterator const_iterator;
  typedef typename std::map<K, V>::size_type size_type;

protected:
  std::map<K, V> map;
  typedef typename std::map<K, V>::iterator iterator;
  std::multimap<V, iterator> rmap;
  typedef typename std::multimap<V, iterator>::iterator rmap_iterator;
  size_type nMaxSize;

public:
  explicit LimitedMap(size_type nMaxSizeIn)
  {
    assert(nMaxSizeIn > 0);
    nMaxSize = nMaxSizeIn;
  }
  const_iterator begin() const { return map.begin(); }
  const_iterator end() const { return map.end(); }
  size_type size() const { return map.size(); }
  bool empty() const { return map.empty(); }
  const_iterator find(const key_type &k) const { return map.find(k); }
  size_type count(const key_type &k) const { return map.count(k); }
  void insert(const value_type &x)
  {
    std::pair<iterator, bool> ret = map.insert(x);
    if (ret.second)
    {
      if (map.size() > nMaxSize)
      {
        map.erase(rmap.begin()->second);
        rmap.erase(rmap.begin());
      }
      rmap.insert(make_pair(x.second, ret.first));
    }
  }
  void erase(const key_type &k)
  {
    iterator itTarget = map.find(k);
    if (itTarget == map.end())
      return;
    std::pair<rmap_iterator, rmap_iterator> itPair = rmap.equal_range(itTarget->second);
    for (rmap_iterator it = itPair.first; it != itPair.second; ++it)
      if (it->second == itTarget)
      {
        rmap.erase(it);
        map.erase(itTarget);
        return;
      }
    // Shouldn't ever get here
    assert(0);
  }
  void update(const_iterator itIn, const mapped_type &v)
  {
    // Using map::erase() with empty range instead of map::find() to get a non-const iterator,
    // since it is a constant time operation in C++11. For more details, see
    // https://stackoverflow.com/questions/765148/how-to-remove-constness-of-const-iterator
    iterator itTarget = map.erase(itIn, itIn);

    if (itTarget == map.end())
      return;
    std::pair<rmap_iterator, rmap_iterator> itPair = rmap.equal_range(itTarget->second);
    for (rmap_iterator it = itPair.first; it != itPair.second; ++it)
      if (it->second == itTarget)
      {
        rmap.erase(it);
        itTarget->second = v;
        rmap.insert(make_pair(v, itTarget));
        return;
      }
    // Shouldn't ever get here
    assert(0);
  }
  size_type max_size() const { return nMaxSize; }
  size_type max_size(size_type s)
  {
    assert(s > 0);
    while (map.size() > s)
    {
      map.erase(rmap.begin()->second);
      rmap.erase(rmap.begin());
    }
    nMaxSize = s;
    return nMaxSize;
  }
};

/* Map whose keys are pointers, but are compared by their dereferenced values.
 *
 * Differs from a plain std::map<const K*, T, DereferencingComparator<K*> > in
 * that methods that take a key for comparison take a K rather than taking a K*
 * (taking a K* would be confusing, since it's the value rather than the address
 * of the object for comparison that matters due to the dereferencing comparator).
 *
 * Objects pointed to by keys must not be modified in any way that changes the
 * result of DereferencingComparator.
 */

template <class K, class T>
class IndirectMap
{
private:
  typedef std::map<const K *, T, DereferencingComparator<const K *>> base;
  base m;

public:
  typedef typename base::iterator iterator;
  typedef typename base::const_iterator const_iterator;
  typedef typename base::size_type size_type;
  typedef typename base::value_type value_type;

  // passthrough (pointer interface)
  std::pair<iterator, bool> insert(const value_type &value) { return m.insert(value); }

  // pass address (value interface)
  iterator find(const K &key) { return m.find(&key); }
  const_iterator find(const K &key) const { return m.find(&key); }
  iterator lower_bound(const K &key) { return m.lower_bound(&key); }
  const_iterator lower_bound(const K &key) const { return m.lower_bound(&key); }
  size_type erase(const K &key) { return m.erase(&key); }
  size_type count(const K &key) const { return m.count(&key); }

  // passthrough
  bool empty() const { return m.empty(); }
  size_type size() const { return m.size(); }
  size_type max_size() const { return m.max_size(); }
  void clear() { m.clear(); }
  iterator begin() { return m.begin(); }
  iterator end() { return m.end(); }
  const_iterator begin() const { return m.begin(); }
  const_iterator end() const { return m.end(); }
  const_iterator cbegin() const { return m.cbegin(); }
  const_iterator cend() const { return m.cend(); }
};

// Internal representation for FlatMap and FlatSet.
//
// The representation is an open-addressed hash table.  Conceptually,
// the representation is a flat array of entries.  However we
// structure it as an array of buckets where each bucket holds
// kWidth entries along with metadata for the kWidth entries.  The
// metadata marker is
//
//  (a) kEmpty: the entry is empty
//  (b) kDeleted: the entry has been deleted
//  (c) other: the entry is occupied and has low-8 bits of its hash.
//      These hash bits can be used to avoid potentially expensive
//      key comparisons.
//
// FlatMap passes in a bucket that contains keys and values, FlatSet
// passes in a bucket that does not contain values.
template <typename Key, typename Bucket, class Hash, class Eq>
class FlatRep
{
public:
  // kWidth is the number of entries stored in a bucket.
  static const uint32_t kBase = 3;
  static const uint32_t kWidth = (1 << kBase);

  FlatRep(uint64_t N, const Hash &hf, const Eq &eq) : hash_(hf), equal_(eq)
  {
    Init(N);
  }
  explicit FlatRep(const FlatRep &src) : hash_(src.hash_), equal_(src.equal_)
  {
    Init(src.size());
    CopyEntries(src.array_, src.end_, CopyEntry());
  }
  ~FlatRep()
  {
    clear_no_resize();
    delete[] array_;
  }

  // Simple accessors.
  uint64_t size() const { return not_empty_ - deleted_; }
  uint64_t bucket_count() const { return mask_ + 1; }
  Bucket *start() const { return array_; }
  Bucket *limit() const { return end_; }
  const Hash &hash_function() const { return hash_; }
  const Eq &key_eq() const { return equal_; }

  // Overwrite contents of *this with contents of src.
  void CopyFrom(const FlatRep &src)
  {
    if (this != &src)
    {
      clear_no_resize();
      delete[] array_;
      Init(src.size());
      CopyEntries(src.array_, src.end_, CopyEntry());
    }
  }

  void clear_no_resize()
  {
    for (Bucket *b = array_; b != end_; b++)
    {
      for (uint32_t i = 0; i < kWidth; i++)
      {
        if (b->marker[i] >= 2)
        {
          b->Destroy(i);
          b->marker[i] = kEmpty;
        }
      }
    }
    not_empty_ = 0;
    deleted_ = 0;
  }

  void clear()
  {
    clear_no_resize();
    grow_ = 0; // Consider shrinking in MaybeResize()
    MaybeResize();
  }

  void swap(FlatRep &x)
  {
    using std::swap;
    swap(array_, x.array_);
    swap(end_, x.end_);
    swap(lglen_, x.lglen_);
    swap(mask_, x.mask_);
    swap(not_empty_, x.not_empty_);
    swap(deleted_, x.deleted_);
    swap(grow_, x.grow_);
    swap(shrink_, x.shrink_);
  }

  struct SearchResult
  {
    bool found;
    Bucket *b;
    uint32_t index;
  };

  // Hash value is partitioned as follows:
  // 1. Bottom 8 bits are stored in bucket to help speed up comparisons.
  // 2. Next 3 bits give index inside bucket.
  // 3. Remaining bits give bucket number.

  // Find bucket/index for key k.
  SearchResult Find(const Key &k) const
  {
    uint64_t h = hash_(k);
    const uint32_t marker = Marker(h & 0xff);
    uint64_t index = (h >> 8) & mask_; // Holds bucket num and index-in-bucket
    uint32_t num_probes = 1;           // Needed for quadratic probing
    while (true)
    {
      uint32_t bi = index & (kWidth - 1);
      Bucket *b = &array_[index >> kBase];
      const uint32_t x = b->marker[bi];
      if (x == marker && equal_(b->key(bi), k))
      {
        return {true, b, bi};
      }
      else if (x == kEmpty)
      {
        return {false, nullptr, 0};
      }
      index = NextIndex(index, num_probes);
      num_probes++;
    }
  }

  // Find bucket/index for key k, creating a new one if necessary.
  //
  // KeyType is a template parameter so that k's type is deduced and it
  // becomes a universal reference which allows the key initialization
  // below to use an rvalue constructor if available.
  template <typename KeyType>
  SearchResult FindOrInsert(KeyType &&k)
  {
    uint64_t h = hash_(k);
    const uint32_t marker = Marker(h & 0xff);
    uint64_t index = (h >> 8) & mask_; // Holds bucket num and index-in-bucket
    uint32_t num_probes = 1;           // Needed for quadratic probing
    Bucket *del = nullptr;             // First encountered deletion for kInsert
    uint32_t di = 0;
    while (true)
    {
      uint32_t bi = index & (kWidth - 1);
      Bucket *b = &array_[index >> kBase];
      const uint32_t x = b->marker[bi];
      if (x == marker && equal_(b->key(bi), k))
      {
        return {true, b, bi};
      }
      else if (!del && x == kDeleted)
      {
        // Remember deleted index to use for insertion.
        del = b;
        di = bi;
      }
      else if (x == kEmpty)
      {
        if (del)
        {
          // Store in the first deleted slot we encountered
          b = del;
          bi = di;
          deleted_--; // not_empty_ does not change
        }
        else
        {
          not_empty_++;
        }
        b->marker[bi] = marker;
        new (&b->key(bi)) Key(std::forward<KeyType>(k));
        return {false, b, bi};
      }
      index = NextIndex(index, num_probes);
      num_probes++;
    }
  }

  void Erase(Bucket *b, uint32_t i)
  {
    b->Destroy(i);
    b->marker[i] = kDeleted;
    deleted_++;
    grow_ = 0; // Consider shrinking on next insert
  }

  void Prefetch(const Key &k) const
  {
    uint64_t h = hash_(k);
    uint64_t index = (h >> 8) & mask_; // Holds bucket num and index-in-bucket
    uint32_t bi = index & (kWidth - 1);
    Bucket *b = &array_[index >> kBase];
    BUILTIN_PREFETCH(&b->marker[bi], 0, 3);
    BUILTIN_PREFETCH(&b->storage.key[bi], 0, 3);
  }

  inline void MaybeResize()
  {
    if (not_empty_ < grow_)
    {
      return; // Nothing to do
    }
    if (grow_ == 0)
    {
      // Special value set by erase to cause shrink on next insert.
      if (size() >= shrink_)
      {
        // Not small enough to shrink.
        grow_ = static_cast<uint64_t>(bucket_count() * 0.8);
        if (not_empty_ < grow_)
          return;
      }
    }
    Resize(size() + 1);
  }

  void Resize(uint64_t N)
  {
    Bucket *old = array_;
    Bucket *old_end = end_;
    Init(N);
    CopyEntries(old, old_end, MoveEntry());
    delete[] old;
  }

private:
  enum
  {
    kEmpty = 0,
    kDeleted = 1
  }; // Special markers for an entry.

  Hash hash_;          // User-supplied hasher
  Eq equal_;           // User-supplied comparator
  uint8_t lglen_;      // lg(#buckets)
  Bucket *array_;      // array of length (1 << lglen_)
  Bucket *end_;        // Points just past last bucket in array_
  uint64_t mask_;      // (# of entries in table) - 1
  uint64_t not_empty_; // Count of entries with marker != kEmpty
  uint64_t deleted_;   // Count of entries with marker == kDeleted
  uint64_t grow_;      // Grow array when not_empty_ >= grow_
  uint64_t shrink_;    // Shrink array when size() < shrink_

  // Avoid kEmpty and kDeleted markers when computing hash values to
  // store in Bucket::marker[].
  static uint32_t Marker(uint32_t hb) { return hb + (hb < 2 ? 2 : 0); }

  void Init(uint64_t N)
  {
    // Make enough room for N elements.
    uint64_t lg = 0; // Smallest table is just one bucket.
    while (N >= 0.8 * ((1 << lg) * kWidth))
    {
      lg++;
    }
    const uint64_t n = (1 << lg);
    Bucket *array = new Bucket[n];
    for (uint64_t i = 0; i < n; i++)
    {
      Bucket *b = &array[i];
      memset(b->marker, kEmpty, kWidth);
    }
    const uint64_t capacity = (1 << lg) * kWidth;
    lglen_ = lg;
    mask_ = capacity - 1;
    array_ = array;
    end_ = array + n;
    not_empty_ = 0;
    deleted_ = 0;
    grow_ = static_cast<uint64_t>(capacity * 0.8);
    if (lg == 0)
    {
      // Already down to one bucket; no more shrinking.
      shrink_ = 0;
    }
    else
    {
      shrink_ = static_cast<uint64_t>(grow_ * 0.4); // Must be less than 0.5
    }
  }

  // Used by FreshInsert when we should copy from source.
  struct CopyEntry
  {
    inline void operator()(Bucket *dst, uint32_t dsti, Bucket *src, uint32_t srci)
    {
      dst->CopyFrom(dsti, src, srci);
    }
  };

  // Used by FreshInsert when we should move from source.
  struct MoveEntry
  {
    inline void operator()(Bucket *dst, uint32_t dsti, Bucket *src, uint32_t srci)
    {
      dst->MoveFrom(dsti, src, srci);
      src->Destroy(srci);
      src->marker[srci] = kDeleted;
    }
  };

  template <typename Copier>
  void CopyEntries(Bucket *start, Bucket *end, Copier copier)
  {
    for (Bucket *b = start; b != end; b++)
    {
      for (uint32_t i = 0; i < kWidth; i++)
      {
        if (b->marker[i] >= 2)
        {
          FreshInsert(b, i, copier);
        }
      }
    }
  }

  // Create an entry for the key numbered src_index in *src and return
  // its bucket/index.  Used for insertion into a fresh table.  We
  // assume that there are no deletions, and k does not already exist
  // in the table.
  template <typename Copier>
  void FreshInsert(Bucket *src, uint32_t src_index, Copier copier)
  {
    uint64_t h = hash_(src->key(src_index));
    const uint32_t marker = Marker(h & 0xff);
    uint64_t index = (h >> 8) & mask_; // Holds bucket num and index-in-bucket
    uint32_t num_probes = 1;           // Needed for quadratic probing
    while (true)
    {
      uint32_t bi = index & (kWidth - 1);
      Bucket *b = &array_[index >> kBase];
      const uint32_t x = b->marker[bi];
      if (x == 0)
      {
        b->marker[bi] = marker;
        not_empty_++;
        copier(b, bi, src, src_index);
        return;
      }
      index = NextIndex(index, num_probes);
      num_probes++;
    }
  }

  inline uint64_t NextIndex(uint64_t i, uint32_t num_probes) const
  {
    // Quadratic probing.
    return (i + num_probes) & mask_;
  }
};

// FlatMap<K,V,...> provides a map from K to V.
//
// The map is implemented using an open-addressed hash table.  A
// single array holds entire map contents and collisions are resolved
// by probing at a sequence of locations in the array.
template <typename Key, typename Val, class Hash = hash<Key>,
          class Eq = std::equal_to<Key>>
class FlatMap
{
private:
  // Forward declare some internal types needed in public section.
  struct Bucket;

  // We cannot use std::pair<> since internal representation stores
  // keys and values in separate arrays, so we make a custom struct
  // that holds references to the internal key, value elements.
  //
  // We define the struct as private ValueType, and typedef it as public
  // value_type, to work around a gcc bug when compiling the iterators.
  struct ValueType
  {
    typedef Key first_type;
    typedef Val second_type;

    const Key &first;
    Val &second;
    ValueType(const Key &k, Val &v) : first(k), second(v) {}
  };

public:
  typedef Key key_type;
  typedef Val mapped_type;
  typedef Hash hasher;
  typedef Eq key_equal;
  typedef uint64_t size_type;
  typedef ptrdiff_t difference_type;
  typedef ValueType value_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;

  FlatMap() : FlatMap(1) {}

  explicit FlatMap(uint64_t N, const Hash &hf = Hash(), const Eq &eq = Eq())
      : rep_(N, hf, eq) {}

  FlatMap(const FlatMap &src) : rep_(src.rep_) {}

  template <typename InputIter>
  FlatMap(InputIter first, InputIter last, uint64_t N = 1,
          const Hash &hf = Hash(), const Eq &eq = Eq())
      : FlatMap(N, hf, eq)
  {
    insert(first, last);
  }

  FlatMap(std::initializer_list<std::pair<const Key, Val>> init, uint64_t N = 1,
          const Hash &hf = Hash(), const Eq &eq = Eq())
      : FlatMap(init.begin(), init.end(), N, hf, eq) {}

  FlatMap &operator=(const FlatMap &src)
  {
    rep_.CopyFrom(src.rep_);
    return *this;
  }

  ~FlatMap() {}

  void swap(FlatMap &x) { rep_.swap(x.rep_); }
  void clear_no_resize() { rep_.clear_no_resize(); }
  void clear() { rep_.clear(); }
  void reserve(uint64_t N) { rep_.Resize(std::max(N, size())); }
  void rehash(uint64_t N) { rep_.Resize(std::max(N, size())); }
  void resize(uint64_t N) { rep_.Resize(std::max(N, size())); }
  uint64_t size() const { return rep_.size(); }
  bool empty() const { return size() == 0; }
  uint64_t bucket_count() const { return rep_.bucket_count(); }
  hasher hash_function() const { return rep_.hash_function(); }
  key_equal key_eq() const { return rep_.key_eq(); }

  class iterator
  {
  public:
    typedef typename FlatMap::difference_type difference_type;
    typedef typename FlatMap::value_type value_type;
    typedef typename FlatMap::pointer pointer;
    typedef typename FlatMap::reference reference;
    typedef ::std::forward_iterator_tag iterator_category;

    iterator() : b_(nullptr), end_(nullptr), i_(0) {}

    // Make iterator pointing at first element at or after b.
    iterator(Bucket *b, Bucket *end) : b_(b), end_(end), i_(0) { SkipUnused(); }

    // Make iterator pointing exactly at ith element in b, which must exist.
    iterator(Bucket *b, Bucket *end, uint32_t i) : b_(b), end_(end), i_(i)
    {
      FillValue();
    }

    reference operator*() { return *val(); }
    pointer operator->() { return val(); }
    bool operator==(const iterator &x) const
    {
      return b_ == x.b_ && i_ == x.i_;
    }
    bool operator!=(const iterator &x) const { return !(*this == x); }
    iterator &operator++()
    {
      assert(b_ != end_);
      i_++;
      SkipUnused();
      return *this;
    }
    iterator operator++(int /*indicates postfix*/)
    {
      iterator tmp(*this);
      ++*this;
      return tmp;
    }

  private:
    friend class FlatMap;
    Bucket *b_;
    Bucket *end_;
    uint32_t i_;
    char space_[sizeof(value_type)];

    pointer val() { return reinterpret_cast<pointer>(space_); }
    void FillValue() { new (space_) value_type(b_->key(i_), b_->val(i_)); }
    void SkipUnused()
    {
      while (b_ < end_)
      {
        if (i_ >= Rep::kWidth)
        {
          i_ = 0;
          b_++;
        }
        else if (b_->marker[i_] < 2)
        {
          i_++;
        }
        else
        {
          FillValue();
          break;
        }
      }
    }
  };

  class const_iterator
  {
  private:
    mutable iterator rep_; // Share state and logic with non-const iterator.
  public:
    typedef typename FlatMap::difference_type difference_type;
    typedef typename FlatMap::value_type value_type;
    typedef typename FlatMap::const_pointer pointer;
    typedef typename FlatMap::const_reference reference;
    typedef ::std::forward_iterator_tag iterator_category;

    const_iterator() : rep_() {}
    const_iterator(Bucket *start, Bucket *end) : rep_(start, end) {}
    const_iterator(Bucket *b, Bucket *end, uint32_t i) : rep_(b, end, i) {}

    reference operator*() const { return *rep_.val(); }
    pointer operator->() const { return rep_.val(); }
    bool operator==(const const_iterator &x) const { return rep_ == x.rep_; }
    bool operator!=(const const_iterator &x) const { return rep_ != x.rep_; }
    const_iterator &operator++()
    {
      ++rep_;
      return *this;
    }
    const_iterator operator++(int /*indicates postfix*/)
    {
      const_iterator tmp(*this);
      ++*this;
      return tmp;
    }
  };

  iterator begin() { return iterator(rep_.start(), rep_.limit()); }
  iterator end() { return iterator(rep_.limit(), rep_.limit()); }
  const_iterator begin() const
  {
    return const_iterator(rep_.start(), rep_.limit());
  }
  const_iterator end() const
  {
    return const_iterator(rep_.limit(), rep_.limit());
  }

  uint64_t count(const Key &k) const { return rep_.Find(k).found ? 1 : 0; }
  iterator find(const Key &k)
  {
    auto r = rep_.Find(k);
    return r.found ? iterator(r.b, rep_.limit(), r.index) : end();
  }
  const_iterator find(const Key &k) const
  {
    auto r = rep_.Find(k);
    return r.found ? const_iterator(r.b, rep_.limit(), r.index) : end();
  }

  Val &at(const Key &k)
  {
    auto r = rep_.Find(k);
    assert(r.found);
    return r.b->val(r.index);
  }
  const Val &at(const Key &k) const
  {
    auto r = rep_.Find(k);
    assert(r.found);
    return r.b->val(r.index);
  }

  template <typename P>
  std::pair<iterator, bool> insert(const P &p)
  {
    return Insert(p.first, p.second);
  }
  std::pair<iterator, bool> insert(const std::pair<const Key, Val> &p)
  {
    return Insert(p.first, p.second);
  }
  template <typename InputIter>
  void insert(InputIter first, InputIter last)
  {
    for (; first != last; ++first)
    {
      insert(*first);
    }
  }

  Val &operator[](const Key &k) { return IndexOp(k); }
  Val &operator[](Key &&k) { return IndexOp(std::forward<Key>(k)); }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args &&... args)
  {
    return InsertPair(std::make_pair(std::forward<Args>(args)...));
  }

  uint64_t erase(const Key &k)
  {
    auto r = rep_.Find(k);
    if (!r.found)
      return 0;
    rep_.Erase(r.b, r.index);
    return 1;
  }
  iterator erase(iterator pos)
  {
    rep_.Erase(pos.b_, pos.i_);
    ++pos;
    return pos;
  }
  iterator erase(iterator pos, iterator last)
  {
    for (; pos != last; ++pos)
    {
      rep_.Erase(pos.b_, pos.i_);
    }
    return pos;
  }

  std::pair<iterator, iterator> equal_range(const Key &k)
  {
    auto pos = find(k);
    if (pos == end())
    {
      return std::make_pair(pos, pos);
    }
    else
    {
      auto next = pos;
      ++next;
      return std::make_pair(pos, next);
    }
  }
  std::pair<const_iterator, const_iterator> equal_range(const Key &k) const
  {
    auto pos = find(k);
    if (pos == end())
    {
      return std::make_pair(pos, pos);
    }
    else
    {
      auto next = pos;
      ++next;
      return std::make_pair(pos, next);
    }
  }

  bool operator==(const FlatMap &x) const
  {
    if (size() != x.size())
      return false;
    for (auto &p : x)
    {
      auto i = find(p.first);
      if (i == end())
        return false;
      if (i->second != p.second)
        return false;
    }
    return true;
  }
  bool operator!=(const FlatMap &x) const { return !(*this == x); }

  // If key exists in the table, prefetch the associated value.  This
  // is a hint, and may have no effect.
  void prefetch_value(const Key &key) const { rep_.Prefetch(key); }

private:
  using Rep = FlatRep<Key, Bucket, Hash, Eq>;

  // Bucket stores kWidth <marker, key, value> triples.
  // The data is organized as three parallel arrays to reduce padding.
  struct Bucket
  {
    uint8_t marker[Rep::kWidth];

    // Wrap keys and values in union to control construction and destruction.
    union Storage {
      struct
      {
        Key key[Rep::kWidth];
        Val val[Rep::kWidth];
      };
      Storage() {}
      ~Storage() {}
    } storage;

    Key &key(uint32_t i)
    {
      //DCHECK_GE(marker[i], 2);
      return storage.key[i];
    }
    Val &val(uint32_t i)
    {
      //DCHECK_GE(marker[i], 2);
      return storage.val[i];
    }
    template <typename V>
    void InitVal(uint32_t i, V &&v)
    {
      new (&storage.val[i]) Val(std::forward<V>(v));
    }
    void Destroy(uint32_t i)
    {
      storage.key[i].Key::~Key();
      storage.val[i].Val::~Val();
    }
    void MoveFrom(uint32_t i, Bucket *src, uint32_t src_index)
    {
      new (&storage.key[i]) Key(std::move(src->storage.key[src_index]));
      new (&storage.val[i]) Val(std::move(src->storage.val[src_index]));
    }
    void CopyFrom(uint32_t i, Bucket *src, uint32_t src_index)
    {
      new (&storage.key[i]) Key(src->storage.key[src_index]);
      new (&storage.val[i]) Val(src->storage.val[src_index]);
    }
  };

  template <typename Pair>
  std::pair<iterator, bool> InsertPair(Pair &&p)
  {
    return Insert(std::forward<decltype(p.first)>(p.first),
                  std::forward<decltype(p.second)>(p.second));
  }

  template <typename K, typename V>
  std::pair<iterator, bool> Insert(K &&k, V &&v)
  {
    rep_.MaybeResize();
    auto r = rep_.FindOrInsert(std::forward<K>(k));
    const bool inserted = !r.found;
    if (inserted)
    {
      r.b->InitVal(r.index, std::forward<V>(v));
    }
    return {iterator(r.b, rep_.limit(), r.index), inserted};
  }

  template <typename K>
  Val &IndexOp(K &&k)
  {
    rep_.MaybeResize();
    auto r = rep_.FindOrInsert(std::forward<K>(k));
    Val *vptr = &r.b->val(r.index);
    if (!r.found)
    {
      new (vptr) Val(); // Initialize value in new slot.
    }
    return *vptr;
  }

  Rep rep_;
};

// FlatSet<K,...> provides a set of K.
//
// The map is implemented using an open-addressed hash table.  A
// single array holds entire map contents and collisions are resolved
// by probing at a sequence of locations in the array.
template <typename Key, class Hash = hash<Key>, class Eq = std::equal_to<Key>>
class FlatSet
{
private:
  // Forward declare some internal types needed in public section.
  struct Bucket;

public:
  typedef Key key_type;
  typedef Key value_type;
  typedef Hash hasher;
  typedef Eq key_equal;
  typedef uint64_t size_type;
  typedef ptrdiff_t difference_type;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;
  typedef value_type &reference;
  typedef const value_type &const_reference;

  FlatSet() : FlatSet(1) {}

  explicit FlatSet(uint64_t N, const Hash &hf = Hash(), const Eq &eq = Eq())
      : rep_(N, hf, eq) {}

  FlatSet(const FlatSet &src) : rep_(src.rep_) {}

  template <typename InputIter>
  FlatSet(InputIter first, InputIter last, uint64_t N = 1,
          const Hash &hf = Hash(), const Eq &eq = Eq())
      : FlatSet(N, hf, eq)
  {
    insert(first, last);
  }

  FlatSet(std::initializer_list<value_type> init, uint64_t N = 1,
          const Hash &hf = Hash(), const Eq &eq = Eq())
      : FlatSet(init.begin(), init.end(), N, hf, eq) {}

  FlatSet &operator=(const FlatSet &src)
  {
    rep_.CopyFrom(src.rep_);
    return *this;
  }

  ~FlatSet() {}

  void swap(FlatSet &x) { rep_.swap(x.rep_); }
  void clear_no_resize() { rep_.clear_no_resize(); }
  void clear() { rep_.clear(); }
  void reserve(uint64_t N) { rep_.Resize(std::max(N, size())); }
  void rehash(uint64_t N) { rep_.Resize(std::max(N, size())); }
  void resize(uint64_t N) { rep_.Resize(std::max(N, size())); }
  uint64_t size() const { return rep_.size(); }
  bool empty() const { return size() == 0; }
  uint64_t bucket_count() const { return rep_.bucket_count(); }
  hasher hash_function() const { return rep_.hash_function(); }
  key_equal key_eq() const { return rep_.key_eq(); }

  class const_iterator
  {
  public:
    typedef typename FlatSet::difference_type difference_type;
    typedef typename FlatSet::value_type value_type;
    typedef typename FlatSet::const_pointer pointer;
    typedef typename FlatSet::const_reference reference;
    typedef ::std::forward_iterator_tag iterator_category;

    const_iterator() : b_(nullptr), end_(nullptr), i_(0) {}

    // Make iterator pointing at first element at or after b.
    const_iterator(Bucket *b, Bucket *end) : b_(b), end_(end), i_(0)
    {
      SkipUnused();
    }

    // Make iterator pointing exactly at ith element in b, which must exist.
    const_iterator(Bucket *b, Bucket *end, uint32_t i)
        : b_(b), end_(end), i_(i) {}

    reference operator*() const { return key(); }
    pointer operator->() const { return &key(); }
    bool operator==(const const_iterator &x) const
    {
      return b_ == x.b_ && i_ == x.i_;
    }
    bool operator!=(const const_iterator &x) const { return !(*this == x); }
    const_iterator &operator++()
    {
      assert(b_ != end_);
      i_++;
      SkipUnused();
      return *this;
    }
    const_iterator operator++(int /*indicates postfix*/)
    {
      const_iterator tmp(*this);
      ++*this;
      return tmp;
    }

  private:
    friend class FlatSet;
    Bucket *b_;
    Bucket *end_;
    uint32_t i_;

    reference key() const { return b_->key(i_); }
    void SkipUnused()
    {
      while (b_ < end_)
      {
        if (i_ >= Rep::kWidth)
        {
          i_ = 0;
          b_++;
        }
        else if (b_->marker[i_] < 2)
        {
          i_++;
        }
        else
        {
          break;
        }
      }
    }
  };

  typedef const_iterator iterator;

  iterator begin() { return iterator(rep_.start(), rep_.limit()); }
  iterator end() { return iterator(rep_.limit(), rep_.limit()); }
  const_iterator begin() const
  {
    return const_iterator(rep_.start(), rep_.limit());
  }
  const_iterator end() const
  {
    return const_iterator(rep_.limit(), rep_.limit());
  }

  uint64_t count(const Key &k) const { return rep_.Find(k).found ? 1 : 0; }
  iterator find(const Key &k)
  {
    auto r = rep_.Find(k);
    return r.found ? iterator(r.b, rep_.limit(), r.index) : end();
  }
  const_iterator find(const Key &k) const
  {
    auto r = rep_.Find(k);
    return r.found ? const_iterator(r.b, rep_.limit(), r.index) : end();
  }

  std::pair<iterator, bool> insert(const Key &k) { return Insert(k); }
  template <typename InputIter>
  void insert(InputIter first, InputIter last)
  {
    for (; first != last; ++first)
    {
      insert(*first);
    }
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args &&... args)
  {
    rep_.MaybeResize();
    auto r = rep_.FindOrInsert(std::forward<Args>(args)...);
    const bool inserted = !r.found;
    return {iterator(r.b, rep_.limit(), r.index), inserted};
  }

  uint64_t erase(const Key &k)
  {
    auto r = rep_.Find(k);
    if (!r.found)
      return 0;
    rep_.Erase(r.b, r.index);
    return 1;
  }
  iterator erase(iterator pos)
  {
    rep_.Erase(pos.b_, pos.i_);
    ++pos;
    return pos;
  }
  iterator erase(iterator pos, iterator last)
  {
    for (; pos != last; ++pos)
    {
      rep_.Erase(pos.b_, pos.i_);
    }
    return pos;
  }

  std::pair<iterator, iterator> equal_range(const Key &k)
  {
    auto pos = find(k);
    if (pos == end())
    {
      return std::make_pair(pos, pos);
    }
    else
    {
      auto next = pos;
      ++next;
      return std::make_pair(pos, next);
    }
  }
  std::pair<const_iterator, const_iterator> equal_range(const Key &k) const
  {
    auto pos = find(k);
    if (pos == end())
    {
      return std::make_pair(pos, pos);
    }
    else
    {
      auto next = pos;
      ++next;
      return std::make_pair(pos, next);
    }
  }

  bool operator==(const FlatSet &x) const
  {
    if (size() != x.size())
      return false;
    for (const auto &elem : x)
    {
      auto i = find(elem);
      if (i == end())
        return false;
    }
    return true;
  }
  bool operator!=(const FlatSet &x) const { return !(*this == x); }

  // If key exists in the table, prefetch it.  This is a hint, and may
  // have no effect.
  void prefetch_value(const Key &key) const { rep_.Prefetch(key); }

private:
  using Rep = FlatRep<Key, Bucket, Hash, Eq>;

  // Bucket stores kWidth <marker, key, value> triples.
  // The data is organized as three parallel arrays to reduce padding.
  struct Bucket
  {
    uint8_t marker[Rep::kWidth];

    // Wrap keys in union to control construction and destruction.
    union Storage {
      Key key[Rep::kWidth];
      Storage() {}
      ~Storage() {}
    } storage;

    Key &key(uint32_t i)
    {
      //DCHECK_GE(marker[i], 2);
      return storage.key[i];
    }
    void Destroy(uint32_t i) { storage.key[i].Key::~Key(); }
    void MoveFrom(uint32_t i, Bucket *src, uint32_t src_index)
    {
      new (&storage.key[i]) Key(std::move(src->storage.key[src_index]));
    }
    void CopyFrom(uint32_t i, Bucket *src, uint32_t src_index)
    {
      new (&storage.key[i]) Key(src->storage.key[src_index]);
    }
  };

  std::pair<iterator, bool> Insert(const Key &k)
  {
    rep_.MaybeResize();
    auto r = rep_.FindOrInsert(k);
    const bool inserted = !r.found;
    return {iterator(r.b, rep_.limit(), r.index), inserted};
  }

  Rep rep_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MAP_UTIL_H_