
#ifndef MYCC_UTIL_MAP_UTIL_H_
#define MYCC_UTIL_MAP_UTIL_H_

#include <assert.h>
#include <math.h>
#include <algorithm>
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

namespace mycc
{
namespace util
{

template <class T>
struct DereferencingComparator
{
  bool operator()(const T a, const T b) const { return *a < *b; }
};

template <class K, class V>
struct KVNode
{
  K key;
  V val;
  KVNode *prev;
  KVNode *next;
  KVNode() : prev(nullptr), next(nullptr)
  {
    key = {};
    val = {};
  }

  template <typename VV>
  KVNode(const K &key, VV &&val)
      : key(key), val(std::forward<VV>(val)), prev(nullptr), next(nullptr) {}
};

template <class K, class V>
class LRUMap
{
public:
  LRUMap() : capacity_(kDefaultCapacity), map_(0), head_(), tail_()
  {
    Init();
  }

  explicit LRUMap(const uint64_t capacity)
      : capacity_(capacity), map_(0), head_(), tail_()
  {
    Init();
  }

  ~LRUMap() { Clear(); }

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

  KVNode<K, V> *First()
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
      auto *KVNode = &map_[key];
      Detach(KVNode);
      Attach(KVNode);
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
  std::unordered_map<K, KVNode<K, V>> map_;
  KVNode<K, V> head_;
  KVNode<K, V> tail_;

  void Init()
  {
    head_.prev = nullptr;
    head_.next = &tail_;
    tail_.prev = &head_;
    tail_.next = nullptr;
    size_ = 0;
  }

  void Detach(KVNode<K, V> *KVNode)
  {
    if (KVNode->prev != nullptr)
    {
      KVNode->prev->next = KVNode->next;
    }
    if (KVNode->next != nullptr)
    {
      KVNode->next->prev = KVNode->prev;
    }
    KVNode->prev = nullptr;
    KVNode->next = nullptr;
    --size_;
  }

  void Attach(KVNode<K, V> *KVNode)
  {
    KVNode->prev = &head_;
    KVNode->next = head_.next;
    head_.next = KVNode;
    if (KVNode->next != nullptr)
    {
      KVNode->next->prev = KVNode;
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
          auto *KVNode = &map_[key];
          Detach(KVNode);
          Attach(KVNode);
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

      map_.emplace(key, KVNode<K, V>(key, std::forward<VV>(val)));
      Attach(&map_[key]);
    }
    return true;
  }

  V *Get(const K &key, bool silent)
  {
    if (Contains(key))
    {
      auto *KVNode = &map_[key];
      if (!silent)
      {
        Detach(KVNode);
        Attach(KVNode);
      }
      return &KVNode->val;
    }
    return nullptr;
  }

  bool GetCopy(const K &key, const V *val, bool silent)
  {
    if (Contains(key))
    {
      auto *KVNode = &map_[key];
      if (!silent)
      {
        Detach(KVNode);
        Attach(KVNode);
      }
      *val = KVNode->val;
      return true;
    }
    return false;
  }

  bool GetObsolete(K *key)
  {
    if (Full())
    {
      auto *KVNode = tail_.prev;
      Detach(KVNode);
      *key = KVNode->key;
      map_.erase(KVNode->key);
      return true;
    }
    return false;
  }
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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MAP_UTIL_H_