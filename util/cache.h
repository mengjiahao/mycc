
#ifndef MYCC_UTIL_CACHE_H_
#define MYCC_UTIL_CACHE_H_

#include <assert.h>
#include <stdint.h>
#include <algorithm>
#include <functional>
#include <iterator>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <type_traits>
#include <unordered_map>
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// A Cache is an interface that maps keys to values.  It has internal
// synchronization and may be safely accessed concurrently from
// multiple threads.  It may automatically evict entries to make room
// for new entries.  Values have a specified charge against the cache
// capacity.  For example, a cache where the values are variable
// length strings, may use the length of the string as the charge for
// the string.
//
// A builtin cache implementation with a least-recently-used eviction
// policy is provided.  Clients may use their own implementations if
// they want something more sophisticated (like scan-resistance, a
// custom eviction policy, variable cache sizing, etc.)

class Cache;

// Create a new cache with a fixed size capacity.  This implementation
// of Cache uses a least-recently-used eviction policy.
Cache *NewLRUCache(uint64_t capacity);

class Cache
{
public:
  Cache() = default;

  Cache(const Cache &) = delete;
  Cache &operator=(const Cache &) = delete;

  // Destroys all existing entries by calling the "deleter"
  // function that was passed to the constructor.
  virtual ~Cache(){};

  // Opaque handle to an entry stored in the cache.
  struct Handle
  {
  };

  // Insert a mapping from key->value into the cache and assign it
  // the specified charge against the total cache capacity.
  //
  // Returns a handle that corresponds to the mapping.  The caller
  // must call this->Release(handle) when the returned mapping is no
  // longer needed.
  //
  // When the inserted entry is no longer needed, the key and
  // value will be passed to "deleter".
  virtual Handle *Insert(const StringPiece &key, void *value, uint64_t charge,
                         void (*deleter)(const StringPiece &key, void *value)) = 0;

  // If the cache has no mapping for "key", returns nullptr.
  //
  // Else return a handle that corresponds to the mapping.  The caller
  // must call this->Release(handle) when the returned mapping is no
  // longer needed.
  virtual Handle *Lookup(const StringPiece &key) = 0;

  // Release a mapping returned by a previous Lookup().
  // REQUIRES: handle must not have been released yet.
  // REQUIRES: handle must have been returned by a method on *this.
  virtual void Release(Handle *handle) = 0;

  // Return the value encapsulated in a handle returned by a
  // successful Lookup().
  // REQUIRES: handle must not have been released yet.
  // REQUIRES: handle must have been returned by a method on *this.
  virtual void *Value(Handle *handle) = 0;

  // If the cache contains entry for key, erase it.  Note that the
  // underlying entry will be kept around until all existing handles
  // to it have been released.
  virtual void Erase(const StringPiece &key) = 0;

  // Return a new numeric id.  May be used by multiple clients who are
  // sharing the same cache to partition the key space.  Typically the
  // client will allocate a new id at startup and prepend the id to
  // its cache keys.
  virtual uint64_t NewId() = 0;

  // Remove all cache entries that are not actively in use.  Memory-constrained
  // applications may wish to call this method to reduce memory usage.
  // Default implementation of Prune() does nothing.  Subclasses are strongly
  // encouraged to override the default implementation.  A future release of
  // leveldb may change Prune() to a pure abstract method.
  virtual void Prune() {}

  // Return an estimate of the combined charges of all elements stored in the
  // cache.
  virtual uint64_t TotalCharge() const = 0;

private:
  void LRU_Remove(Handle *e);
  void LRU_Append(Handle *e);
  void Unref(Handle *e);

  struct Rep;
  Rep *rep_;
};

/////////////////////////// LruMap ///////////////////////////////////////

template <typename Key, typename Value>
class LruMap
{
public:
  typedef typename std::pair<Key, Value> KeyValuePair;
  typedef typename std::list<KeyValuePair>::iterator ListIterator;

  class Iterator : public std::iterator<std::input_iterator_tag, KeyValuePair>
  {
  public:
    Iterator(typename std::unordered_map<Key, ListIterator>::iterator it)
        : _it(it) {}

    Iterator &operator++()
    {
      ++_it;
      return *this;
    }

    bool operator==(const Iterator &rhs) const
    {
      return _it == rhs._it;
    }

    bool operator!=(const Iterator &rhs) const
    {
      return _it != rhs._it;
    }

    KeyValuePair *operator->()
    {
      return _it->second.operator->();
    }

    KeyValuePair &operator*()
    {
      return *_it->second;
    }

  private:
    typename std::unordered_map<Key, ListIterator>::iterator _it;
  };

  LruMap(uint64_t max_size) : _max_size(max_size) {}

  void put(const Key &key, const Value &value)
  {
    auto it = _cache_items_map.find(key);
    if (it != _cache_items_map.end())
    {
      _cache_items_list.erase(it->second);
      _cache_items_map.erase(it);
    }

    _cache_items_list.push_front(KeyValuePair(key, value));
    _cache_items_map[key] = _cache_items_list.begin();

    if (_cache_items_map.size() > _max_size)
    {
      auto last = _cache_items_list.end();
      last--;
      _cache_items_map.erase(last->first);
      _cache_items_list.pop_back();
    }
  }

  void erase(const Key &key)
  {
    auto it = _cache_items_map.find(key);
    if (it != _cache_items_map.end())
    {
      _cache_items_list.erase(it->second);
      _cache_items_map.erase(it);
    }
  }

  // Must copy value, because value maybe relased when caller used
  bool get(const Key &key, Value *value)
  {
    auto it = _cache_items_map.find(key);
    if (it == _cache_items_map.end())
    {
      return false;
    }
    _cache_items_list.splice(_cache_items_list.begin(), _cache_items_list, it->second);
    *value = it->second->second;
    return true;
  }

  bool exists(const Key &key) const
  {
    return _cache_items_map.find(key) != _cache_items_map.end();
  }

  uint64_t size() const
  {
    return _cache_items_map.size();
  }

  Iterator begin()
  {
    return Iterator(_cache_items_map.begin());
  }

  Iterator end()
  {
    return Iterator(_cache_items_map.end());
  }

private:
  std::list<KeyValuePair> _cache_items_list;
  std::unordered_map<Key, ListIterator> _cache_items_map;
  uint64_t _max_size;
};

/////////////////////////// LRUCacheEasy ///////////////////////////////////////

template <class K, class V>
struct LruNode
{
  K key;
  V val;
  LruNode *prev;
  LruNode *next;
  LruNode() : prev(nullptr), next(nullptr)
  {
    key = {};
    val = {};
  }

  template <typename VV>
  LruNode(const K &key, VV &&val)
      : key(key), val(std::forward<VV>(val)), prev(nullptr), next(nullptr) {}
};

template <class K, class V>
class LRUCacheEasy
{
public:
  LRUCacheEasy() : capacity_(kDefaultCapacity), map_(0), head_(), tail_()
  {
    Init();
  }

  explicit LRUCacheEasy(const uint64_t capacity)
      : capacity_(capacity), map_(0), head_(), tail_()
  {
    Init();
  }

  ~LRUCacheEasy() { Clear(); }

  void GetCache(std::unordered_map<K, V> *cache)
  {
    for (auto it = map_.begin(); it != map_.end(); ++it)
    {
      cache->operator[](it->first) = it->second.val;
    }
  }

  V &operator[](const K &key)
  {
    if (!Contains(key))
    {
      K obsolete;
      GetObsolete(&obsolete);
    }
    return map_[key].val;
  }

  /*
   * Silently get all as vector
   */
  void GetAllSilently(std::vector<V *> *ret)
  {
    for (auto it = map_.begin(); it != map_.end(); ++it)
    {
      ret->push_back(&it->second.val);
    }
  }

  /*
   * for both add & update purposes
   */
  template <typename VV>
  bool Put(const K &key, VV &&val)
  {
    K tmp;
    return Update(key, std::forward<VV>(val), &tmp, false, false);
  }

  /*
   * update existing elements only
   */
  template <typename VV>
  bool Update(const K &key, VV &&val)
  {
    if (!Contains(key))
    {
      return false;
    }
    K tmp;
    return Update(key, std::forward<VV>(val), &tmp, true, false);
  }

  /*
   * silently update existing elements only
   */
  template <typename VV>
  bool UpdateSilently(const K &key, VV *val)
  {
    if (!Contains(key))
    {
      return false;
    }
    K tmp;
    return Update(key, std::forward<VV>(*val), &tmp, true, true);
  }

  /*
   * add new elements only
   */
  template <typename VV>
  bool Add(const K &key, VV *val)
  {
    K tmp;
    return Update(key, std::forward<VV>(*val), &tmp, true, false);
  }

  template <typename VV>
  bool PutAndGetObsolete(const K &key, VV *val, K *obs)
  {
    return Update(key, std::forward<VV>(*val), obs, false, false);
  }

  template <typename VV>
  bool AddAndGetObsolete(const K &key, VV *val, K *obs)
  {
    return Update(key, std::forward<VV>(*val), obs, true, false);
  }

  V *GetSilently(const K &key) { return Get(key, true); }

  V *Get(const K &key) { return Get(key, false); }

  bool GetCopySilently(const K &key, const V *val)
  {
    return GetCopy(key, val, true);
  }

  bool GetCopy(const K &key, const V *val) { return GetCopy(key, val, false); }

  uint64_t size() { return size_; }

  bool Full() { return size() > 0 && size() >= capacity_; }

  bool Empty() { return size() == 0; }

  uint64_t capacity() { return capacity_; }

  LruNode<K, V> *First()
  {
    if (size())
    {
      return head_.next;
    }
    return nullptr;
  }

  bool Contains(const K &key) { return map_.find(key) != map_.end(); }

  bool Prioritize(const K &key)
  {
    if (Contains(key))
    {
      auto *LruNode = &map_[key];
      Detach(LruNode);
      Attach(LruNode);
      return true;
    }
    return false;
  }

  void Clear()
  {
    map_.clear();
    Init();
  }

private:
  static constexpr uint64_t kDefaultCapacity = 10;

  const uint64_t capacity_;
  uint64_t size_;
  std::unordered_map<K, LruNode<K, V>> map_;
  LruNode<K, V> head_;
  LruNode<K, V> tail_;

  void Init()
  {
    head_.prev = nullptr;
    head_.next = &tail_;
    tail_.prev = &head_;
    tail_.next = nullptr;
    size_ = 0;
  }

  void Detach(LruNode<K, V> *LruNode)
  {
    if (LruNode->prev != nullptr)
    {
      LruNode->prev->next = LruNode->next;
    }
    if (LruNode->next != nullptr)
    {
      LruNode->next->prev = LruNode->prev;
    }
    LruNode->prev = nullptr;
    LruNode->next = nullptr;
    --size_;
  }

  void Attach(LruNode<K, V> *LruNode)
  {
    LruNode->prev = &head_;
    LruNode->next = head_.next;
    head_.next = LruNode;
    if (LruNode->next != nullptr)
    {
      LruNode->next->prev = LruNode;
    }
    ++size_;
  }

  template <typename VV>
  bool Update(const K &key, VV &&val, K *obs, bool add_only,
              bool silent_update)
  {
    if (obs == nullptr)
    {
      return false;
    }
    if (Contains(key))
    {
      if (!add_only)
      {
        map_[key].val = std::forward<VV>(val);
        if (!silent_update)
        {
          auto *LruNode = &map_[key];
          Detach(LruNode);
          Attach(LruNode);
        }
        else
        {
          return false;
        }
      }
    }
    else
    {
      if (Full() && !GetObsolete(obs))
      {
        return false;
      }

      map_.emplace(key, LruNode<K, V>(key, std::forward<VV>(val)));
      Attach(&map_[key]);
    }
    return true;
  }

  V *Get(const K &key, bool silent)
  {
    if (Contains(key))
    {
      auto *LruNode = &map_[key];
      if (!silent)
      {
        Detach(LruNode);
        Attach(LruNode);
      }
      return &LruNode->val;
    }
    return nullptr;
  }

  bool GetCopy(const K &key, const V *val, bool silent)
  {
    if (Contains(key))
    {
      auto *LruNode = &map_[key];
      if (!silent)
      {
        Detach(LruNode);
        Attach(LruNode);
      }
      *val = LruNode->val;
      return true;
    }
    return false;
  }

  bool GetObsolete(K *key)
  {
    if (Full())
    {
      auto *LruNode = tail_.prev;
      Detach(LruNode);
      *key = LruNode->key;
      map_.erase(LruNode->key);
      return true;
    }
    return false;
  }
};

// This file contains a template for a Most Recently Used cache that allows
// constant-time access to items using a key, but easy identification of the
// least-recently-used items for removal.  Each key can only be associated with
// one payload item at a time.
//
// The key object will be stored twice, so it should support efficient copying.
//
// NOTE: While all operations are O(1), this code is written for
// legibility rather than optimality. If future profiling identifies this as
// a bottleneck, there is room for smaller values of 1 in the O(1). :]

// MRUCacheBase ----------------------------------------------------------------

// This template is used to standardize map type containers that can be used
// by MRUCacheBase. This level of indirection is necessary because of the way
// that template template params and default template params interact.
template <class KeyType, class ValueType, class CompareType>
struct MRUCacheStandardMap
{
  typedef std::map<KeyType, ValueType, CompareType> Type;
};

// Base class for the MRU cache specializations defined below.
template <class KeyType,
          class PayloadType,
          class HashOrCompareType,
          template <typename, typename, typename> class MapType =
              MRUCacheStandardMap>
class MRUCacheBase
{
public:
  // The payload of the list. This maintains a copy of the key so we can
  // efficiently delete things given an element of the list.
  typedef std::pair<KeyType, PayloadType> value_type;

private:
  typedef std::list<value_type> PayloadList;
  typedef typename MapType<KeyType,
                           typename PayloadList::iterator,
                           HashOrCompareType>::Type KeyIndex;

public:
  typedef typename PayloadList::size_type size_type;

  typedef typename PayloadList::iterator iterator;
  typedef typename PayloadList::const_iterator const_iterator;
  typedef typename PayloadList::reverse_iterator reverse_iterator;
  typedef typename PayloadList::const_reverse_iterator const_reverse_iterator;

  enum
  {
    NO_AUTO_EVICT = 0
  };

  // The max_size is the size at which the cache will prune its members to when
  // a new item is inserted. If the caller wants to manager this itself (for
  // example, maybe it has special work to do when something is evicted), it
  // can pass NO_AUTO_EVICT to not restrict the cache size.
  explicit MRUCacheBase(size_type max_size) : max_size_(max_size) {}

  virtual ~MRUCacheBase() {}

  size_type max_size() const { return max_size_; }

  // Inserts a payload item with the given key. If an existing item has
  // the same key, it is removed prior to insertion. An iterator indicating the
  // inserted item will be returned (this will always be the front of the list).
  //
  // The payload will be forwarded.
  template <typename Payload>
  iterator Put(const KeyType &key, Payload &&payload)
  {
    // Remove any existing payload with that key.
    typename KeyIndex::iterator index_iter = index_.find(key);
    if (index_iter != index_.end())
    {
      // Erase the reference to it. The index reference will be replaced in the
      // code below.
      Erase(index_iter->second);
    }
    else if (max_size_ != NO_AUTO_EVICT)
    {
      // New item is being inserted which might make it larger than the maximum
      // size: kick the oldest thing out if necessary.
      ShrinkToSize(max_size_ - 1);
    }

    ordering_.push_front(value_type(key, std::forward<Payload>(payload)));
    index_.insert(std::make_pair(key, ordering_.begin()));
    return ordering_.begin();
  }

  // Retrieves the contents of the given key, or end() if not found. This method
  // has the side effect of moving the requested item to the front of the
  // recency list.
  //
  // TODO(brettw) We may want a const version of this function in the future.
  iterator Get(const KeyType &key)
  {
    typename KeyIndex::iterator index_iter = index_.find(key);
    if (index_iter == index_.end())
      return end();
    typename PayloadList::iterator iter = index_iter->second;

    // Move the touched item to the front of the recency ordering.
    ordering_.splice(ordering_.begin(), ordering_, iter);
    return ordering_.begin();
  }

  // Retrieves the payload associated with a given key and returns it via
  // result without affecting the ordering (unlike Get).
  iterator Peek(const KeyType &key)
  {
    typename KeyIndex::const_iterator index_iter = index_.find(key);
    if (index_iter == index_.end())
      return end();
    return index_iter->second;
  }

  const_iterator Peek(const KeyType &key) const
  {
    typename KeyIndex::const_iterator index_iter = index_.find(key);
    if (index_iter == index_.end())
      return end();
    return index_iter->second;
  }

  // Exchanges the contents of |this| by the contents of the |other|.
  void Swap(MRUCacheBase &other)
  {
    ordering_.swap(other.ordering_);
    index_.swap(other.index_);
    std::swap(max_size_, other.max_size_);
  }

  // Erases the item referenced by the given iterator. An iterator to the item
  // following it will be returned. The iterator must be valid.
  iterator Erase(iterator pos)
  {
    index_.erase(pos->first);
    return ordering_.erase(pos);
  }

  // MRUCache entries are often processed in reverse order, so we add this
  // convenience function (not typically defined by STL containers).
  reverse_iterator Erase(reverse_iterator pos)
  {
    // We have to actually give it the incremented iterator to delete, since
    // the forward iterator that base() returns is actually one past the item
    // being iterated over.
    return reverse_iterator(Erase((++pos).base()));
  }

  // Shrinks the cache so it only holds |new_size| items. If |new_size| is
  // bigger or equal to the current number of items, this will do nothing.
  void ShrinkToSize(size_type new_size)
  {
    for (size_type i = size(); i > new_size; i--)
      Erase(rbegin());
  }

  // Deletes everything from the cache.
  void Clear()
  {
    index_.clear();
    ordering_.clear();
  }

  // Returns the number of elements in the cache.
  size_type size() const
  {
    // We don't use ordering_.size() for the return value because
    // (as a linked list) it can be O(n).
    DCHECK(index_.size() == ordering_.size());
    return index_.size();
  }

  // Allows iteration over the list. Forward iteration starts with the most
  // recent item and works backwards.
  //
  // Note that since these iterators are actually iterators over a list, you
  // can keep them as you insert or delete things (as long as you don't delete
  // the one you are pointing to) and they will still be valid.
  iterator begin() { return ordering_.begin(); }
  const_iterator begin() const { return ordering_.begin(); }
  iterator end() { return ordering_.end(); }
  const_iterator end() const { return ordering_.end(); }

  reverse_iterator rbegin() { return ordering_.rbegin(); }
  const_reverse_iterator rbegin() const { return ordering_.rbegin(); }
  reverse_iterator rend() { return ordering_.rend(); }
  const_reverse_iterator rend() const { return ordering_.rend(); }

  bool empty() const { return ordering_.empty(); }

private:
  PayloadList ordering_;
  KeyIndex index_;

  size_type max_size_;

  DISALLOW_COPY_AND_ASSIGN(MRUCacheBase);
};

// MRUCache --------------------------------------------------------------------

// A container that does not do anything to free its data. Use this when storing
// value types (as opposed to pointers) in the list.
template <class KeyType, class PayloadType>
class MRUCache : public MRUCacheBase<KeyType, PayloadType, std::less<KeyType>>
{
private:
  using ParentType = MRUCacheBase<KeyType, PayloadType, std::less<KeyType>>;

public:
  // See MRUCacheBase, noting the possibility of using NO_AUTO_EVICT.
  explicit MRUCache(typename ParentType::size_type max_size)
      : ParentType(max_size) {}
  virtual ~MRUCache() {}

private:
  DISALLOW_COPY_AND_ASSIGN(MRUCache);
};

// HashingMRUCache ------------------------------------------------------------

template <class KeyType, class ValueType, class HashType>
struct MRUCacheHashMap
{
  typedef std::unordered_map<KeyType, ValueType, HashType> Type;
};

// This class is similar to MRUCache, except that it uses std::unordered_map as
// the map type instead of std::map. Note that your KeyType must be hashable to
// use this cache or you need to provide a hashing class.
template <class KeyType, class PayloadType, class HashType = std::hash<KeyType>>
class HashingMRUCache
    : public MRUCacheBase<KeyType, PayloadType, HashType, MRUCacheHashMap>
{
private:
  using ParentType =
      MRUCacheBase<KeyType, PayloadType, HashType, MRUCacheHashMap>;

public:
  // See MRUCacheBase, noting the possibility of using NO_AUTO_EVICT.
  explicit HashingMRUCache(typename ParentType::size_type max_size)
      : ParentType(max_size) {}
  virtual ~HashingMRUCache() {}

private:
  DISALLOW_COPY_AND_ASSIGN(HashingMRUCache);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CACHE_H_