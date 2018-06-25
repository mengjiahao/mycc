
#include "cache_util.h"
#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hash_util.h"
#include "locks_util.h"

namespace mycc
{
namespace util
{

namespace
{

// LRU cache implementation
//
// Cache entries have an "in_cache" boolean indicating whether the cache has a
// reference on the entry.  The only ways that this can become false without the
// entry being passed to its "deleter" are via Erase(), via Insert() when
// an element with a duplicate key is inserted, or on destruction of the cache.
//
// The cache keeps two linked lists of items in the cache.  All items in the
// cache are in one list or the other, and never both.  Items still referenced
// by clients but erased from the cache are in neither list.  The lists are:
// - in-use:  contains the items currently referenced by clients, in no
//   particular order.  (This list is used for invariant checking.  If we
//   removed the check, elements that would otherwise be on this list could be
//   left as disconnected singleton lists.)
// - LRU:  contains the items not currently referenced by clients, in LRU order
// Elements are moved between these lists by the Ref() and Unref() methods,
// when they detect an element in the cache acquiring or losing its only
// external reference.

// An entry is a variable length heap-allocated structure.  Entries
// are kept in a circular doubly linked list ordered by access time.

struct SimpleLRUHandle
{
  void *value;
  void (*deleter)(const StringPiece &, void *value);
  SimpleLRUHandle *next_hash;
  SimpleLRUHandle *next;
  SimpleLRUHandle *prev;
  size_t charge; // TODO(opt): Only allow uint32_t?
  size_t key_length;
  bool in_cache;    // Whether entry is in the cache.
  uint32_t refs;    // References, including cache reference, if present.
  uint32_t hash;    // Hash of key(); used for fast sharding and comparisons
  char key_data[1]; // Beginning of key

  StringPiece key() const
  {
    // For cheaper lookups, we allow a temporary Handle object
    // to store a pointer to a key in "value".
    if (next == this)
    {
      return *(reinterpret_cast<StringPiece *>(value));
    }
    else
    {
      return StringPiece(key_data, key_length);
    }
  }
};

// We provide our own simple hash table since it removes a whole bunch
// of porting hacks and is also faster than some of the built-in hash
// table implementations in some of the compiler/runtime combinations
// we have tested.  E.g., readrandom speeds up by ~5% over the g++
// 4.4.3's builtin hashtable.
class SimpleHandleTable
{
public:
  SimpleHandleTable() : length_(0), elems_(0), list_(NULL) { Resize(); }
  ~SimpleHandleTable() { delete[] list_; }

  SimpleLRUHandle *Lookup(const StringPiece &key, uint32_t hash)
  {
    return *FindPointer(key, hash);
  }

  SimpleLRUHandle *Insert(SimpleLRUHandle *h)
  {
    SimpleLRUHandle **ptr = FindPointer(h->key(), h->hash);
    SimpleLRUHandle *old = *ptr;
    h->next_hash = (old == NULL ? NULL : old->next_hash);
    *ptr = h;
    if (old == NULL)
    {
      ++elems_;
      if (elems_ > length_)
      {
        // Since each cache entry is fairly large, we aim for a small
        // average linked list length (<= 1).
        Resize();
      }
    }
    return old;
  }

  SimpleLRUHandle *Remove(const StringPiece &key, uint32_t hash)
  {
    SimpleLRUHandle **ptr = FindPointer(key, hash);
    SimpleLRUHandle *result = *ptr;
    if (result != NULL)
    {
      *ptr = result->next_hash;
      --elems_;
    }
    return result;
  }

private:
  // The table consists of an array of buckets where each bucket is
  // a linked list of cache entries that hash into the bucket.
  uint32_t length_;
  uint32_t elems_;
  SimpleLRUHandle **list_;

  // Return a pointer to slot that points to a cache entry that
  // matches key/hash.  If there is no such cache entry, return a
  // pointer to the trailing slot in the corresponding linked list.
  SimpleLRUHandle **FindPointer(const StringPiece &key, uint32_t hash)
  {
    SimpleLRUHandle **ptr = &list_[hash & (length_ - 1)];
    while (*ptr != NULL &&
           ((*ptr)->hash != hash || key != (*ptr)->key()))
    {
      ptr = &(*ptr)->next_hash;
    }
    return ptr;
  }

  void Resize()
  {
    uint32_t new_length = 4;
    while (new_length < elems_)
    {
      new_length *= 2;
    }
    SimpleLRUHandle **new_list = new SimpleLRUHandle *[new_length];
    memset(new_list, 0, sizeof(new_list[0]) * new_length);
    uint32_t count = 0;
    for (uint32_t i = 0; i < length_; i++)
    {
      SimpleLRUHandle *h = list_[i];
      while (h != NULL)
      {
        SimpleLRUHandle *next = h->next_hash;
        uint32_t hash = h->hash;
        SimpleLRUHandle **ptr = &new_list[hash & (new_length - 1)];
        h->next_hash = *ptr;
        *ptr = h;
        h = next;
        count++;
      }
    }
    assert(elems_ == count);
    delete[] list_;
    list_ = new_list;
    length_ = new_length;
  }
};

// A single shard of sharded cache.
class SimpleLRUCache
{
public:
  SimpleLRUCache();
  ~SimpleLRUCache();

  // Separate from constructor so caller can easily make an array of SimpleLRUCache
  void SetCapacity(size_t capacity) { capacity_ = capacity; }

  // Like Cache methods, but with an extra "hash" parameter.
  SimpleCache::Handle *Insert(const StringPiece &key, uint32_t hash,
                              void *value, size_t charge,
                              void (*deleter)(const StringPiece &key, void *value));
  SimpleCache::Handle *Lookup(const StringPiece &key, uint32_t hash);
  void Release(SimpleCache::Handle *handle);
  void Erase(const StringPiece &key, uint32_t hash);
  void Prune();
  size_t TotalCharge() const
  {
    MutexLock l(&mutex_);
    return usage_;
  }

private:
  void LRU_Remove(SimpleLRUHandle *e);
  void LRU_Append(SimpleLRUHandle *list, SimpleLRUHandle *e);
  void Ref(SimpleLRUHandle *e);
  void Unref(SimpleLRUHandle *e);
  bool FinishErase(SimpleLRUHandle *e);

  // Initialized before use.
  size_t capacity_;

  // mutex_ protects the following state.
  mutable Mutex mutex_;
  size_t usage_;

  // Dummy head of LRU list.
  // lru.prev is newest entry, lru.next is oldest entry.
  // Entries have refs==1 and in_cache==true.
  SimpleLRUHandle lru_;

  // Dummy head of in-use list.
  // Entries are in use by clients, and have refs >= 2 and in_cache==true.
  SimpleLRUHandle in_use_;

  SimpleHandleTable table_;
};

SimpleLRUCache::SimpleLRUCache()
    : usage_(0)
{
  // Make empty circular linked lists.
  lru_.next = &lru_;
  lru_.prev = &lru_;
  in_use_.next = &in_use_;
  in_use_.prev = &in_use_;
}

SimpleLRUCache::~SimpleLRUCache()
{
  assert(in_use_.next == &in_use_); // Error if caller has an unreleased handle
  for (SimpleLRUHandle *e = lru_.next; e != &lru_;)
  {
    SimpleLRUHandle *next = e->next;
    assert(e->in_cache);
    e->in_cache = false;
    assert(e->refs == 1); // Invariant of lru_ list.
    Unref(e);
    e = next;
  }
}

void SimpleLRUCache::Ref(SimpleLRUHandle *e)
{
  if (e->refs == 1 && e->in_cache)
  { // If on lru_ list, move to in_use_ list.
    LRU_Remove(e);
    LRU_Append(&in_use_, e);
  }
  e->refs++;
}

void SimpleLRUCache::Unref(SimpleLRUHandle *e)
{
  assert(e->refs > 0);
  e->refs--;
  if (e->refs == 0)
  { // Deallocate.
    assert(!e->in_cache);
    (*e->deleter)(e->key(), e->value);
    free(e);
  }
  else if (e->in_cache && e->refs == 1)
  { // No longer in use; move to lru_ list.
    LRU_Remove(e);
    LRU_Append(&lru_, e);
  }
}

void SimpleLRUCache::LRU_Remove(SimpleLRUHandle *e)
{
  e->next->prev = e->prev;
  e->prev->next = e->next;
}

void SimpleLRUCache::LRU_Append(SimpleLRUHandle *list, SimpleLRUHandle *e)
{
  // Make "e" newest entry by inserting just before *list
  e->next = list;
  e->prev = list->prev;
  e->prev->next = e;
  e->next->prev = e;
}

SimpleCache::Handle *SimpleLRUCache::Lookup(const StringPiece &key, uint32_t hash)
{
  MutexLock l(&mutex_);
  SimpleLRUHandle *e = table_.Lookup(key, hash);
  if (e != NULL)
  {
    Ref(e);
  }
  return reinterpret_cast<SimpleCache::Handle *>(e);
}

void SimpleLRUCache::Release(SimpleCache::Handle *handle)
{
  MutexLock l(&mutex_);
  Unref(reinterpret_cast<SimpleLRUHandle *>(handle));
}

SimpleCache::Handle *SimpleLRUCache::Insert(
    const StringPiece &key, uint32_t hash, void *value, size_t charge,
    void (*deleter)(const StringPiece &key, void *value))
{
  MutexLock l(&mutex_);

  SimpleLRUHandle *e = reinterpret_cast<SimpleLRUHandle *>(
      malloc(sizeof(SimpleLRUHandle) - 1 + key.size()));
  e->value = value;
  e->deleter = deleter;
  e->charge = charge;
  e->key_length = key.size();
  e->hash = hash;
  e->in_cache = false;
  e->refs = 1; // for the returned handle.
  memcpy(e->key_data, key.data(), key.size());

  if (capacity_ > 0)
  {
    e->refs++; // for the cache's reference.
    e->in_cache = true;
    LRU_Append(&in_use_, e);
    usage_ += charge;
    FinishErase(table_.Insert(e));
  } // else don't cache.  (Tests use capacity_==0 to turn off caching.)

  while (usage_ > capacity_ && lru_.next != &lru_)
  {
    SimpleLRUHandle *old = lru_.next;
    assert(old->refs == 1);
    bool erased = FinishErase(table_.Remove(old->key(), old->hash));
    if (!erased)
    { // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }

  return reinterpret_cast<SimpleCache::Handle *>(e);
}

// If e != NULL, finish removing *e from the cache; it has already been removed
// from the hash table.  Return whether e != NULL.  Requires mutex_ held.
bool SimpleLRUCache::FinishErase(SimpleLRUHandle *e)
{
  if (e != NULL)
  {
    assert(e->in_cache);
    LRU_Remove(e);
    e->in_cache = false;
    usage_ -= e->charge;
    Unref(e);
  }
  return e != NULL;
}

void SimpleLRUCache::Erase(const StringPiece &key, uint32_t hash)
{
  MutexLock l(&mutex_);
  FinishErase(table_.Remove(key, hash));
}

void SimpleLRUCache::Prune()
{
  MutexLock l(&mutex_);
  while (lru_.next != &lru_)
  {
    SimpleLRUHandle *e = lru_.next;
    assert(e->refs == 1);
    bool erased = FinishErase(table_.Remove(e->key(), e->hash));
    if (!erased)
    { // to avoid unused variable when compiled NDEBUG
      assert(erased);
    }
  }
}

class SimpleShardedLRUCache : public SimpleCache
{
public:
  static const int kNumShardBits = 4;
  static const int kNumShards = 1 << kNumShardBits;

private:
  SimpleLRUCache shard_[kNumShards];
  Mutex id_mutex_;
  uint64_t last_id_;

  static inline uint32_t HashStringPiece(const StringPiece &s)
  {
    return Hash(s.data(), s.size(), 0);
  }

  static uint32_t Shard(uint32_t hash)
  {
    return hash >> (32 - kNumShardBits);
  }

public:
  explicit SimpleShardedLRUCache(size_t capacity)
      : last_id_(0)
  {
    const size_t per_shard = (capacity + (kNumShards - 1)) / kNumShards;
    for (int s = 0; s < kNumShards; s++)
    {
      shard_[s].SetCapacity(per_shard);
    }
  }
  virtual ~SimpleShardedLRUCache() {}
  virtual Handle *Insert(const StringPiece &key, void *value, size_t charge,
                         void (*deleter)(const StringPiece &key, void *value))
  {
    const uint32_t hash = HashStringPiece(key);
    return shard_[Shard(hash)].Insert(key, hash, value, charge, deleter);
  }
  virtual Handle *Lookup(const StringPiece &key)
  {
    const uint32_t hash = HashStringPiece(key);
    return shard_[Shard(hash)].Lookup(key, hash);
  }
  virtual void Release(Handle *handle)
  {
    SimpleLRUHandle *h = reinterpret_cast<SimpleLRUHandle *>(handle);
    shard_[Shard(h->hash)].Release(handle);
  }
  virtual void Erase(const StringPiece &key)
  {
    const uint32_t hash = HashStringPiece(key);
    shard_[Shard(hash)].Erase(key, hash);
  }
  virtual void *Value(Handle *handle)
  {
    return reinterpret_cast<SimpleLRUHandle *>(handle)->value;
  }
  virtual uint64_t NewId()
  {
    MutexLock l(&id_mutex_);
    return ++(last_id_);
  }
  virtual void Prune()
  {
    for (int s = 0; s < kNumShards; s++)
    {
      shard_[s].Prune();
    }
  }
  virtual size_t TotalCharge() const
  {
    size_t total = 0;
    for (int s = 0; s < kNumShards; s++)
    {
      total += shard_[s].TotalCharge();
    }
    return total;
  }
};

} // end anonymous namespace

SimpleCache *SimpleCache::NewLRUCache(size_t capacity)
{
  return new SimpleShardedLRUCache(capacity);
}

} // namespace util
} // namespace mycc