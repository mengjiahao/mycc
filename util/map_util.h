
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

/**
 * interval_map
 *
 * Maps intervals to values.  Erasing or inserting over an existing
 * range will use S::operator() to split any overlapping existing
 * values.
 *
 * Surprisingly, boost/icl/interval_map doesn't seem to be appropriate
 * for this use case.  The aggregation concept seems to assume
 * commutativity, which doesn't work if we want more recent insertions
 * to overwrite previous ones.
 */

template <typename T>
class IntervalSet
{
public:
  class const_iterator;

  class iterator : public std::iterator<std::forward_iterator_tag, T>
  {
  public:
    explicit iterator(typename std::map<T, T>::iterator iter)
        : _iter(iter)
    {
    }

    // For the copy constructor and assignment operator, the compiler-generated functions, which
    // perform simple bitwise copying, should be fine.

    bool operator==(const iterator &rhs) const
    {
      return (_iter == rhs._iter);
    }

    bool operator!=(const iterator &rhs) const
    {
      return (_iter != rhs._iter);
    }

    // Dereference this iterator to get a pair.
    std::pair<T, T> &operator*()
    {
      return *_iter;
    }

    // Return the interval start.
    T get_start() const
    {
      return _iter->first;
    }

    // Return the interval length.
    T get_len() const
    {
      return _iter->second;
    }

    // Set the interval length.
    void set_len(T len)
    {
      _iter->second = len;
    }

    // Preincrement
    iterator &operator++()
    {
      ++_iter;
      return *this;
    }

    // Postincrement
    iterator operator++(int)
    {
      iterator prev(_iter);
      ++_iter;
      return prev;
    }

    friend class IntervalSet<T>::const_iterator;

  protected:
    typename std::map<T, T>::iterator _iter;
    friend class IntervalSet<T>;
  };

  class const_iterator : public std::iterator<std::forward_iterator_tag, T>
  {
  public:
    explicit const_iterator(typename std::map<T, T>::const_iterator iter)
        : _iter(iter)
    {
    }

    const_iterator(const iterator &i)
        : _iter(i._iter)
    {
    }

    // For the copy constructor and assignment operator, the compiler-generated functions, which
    // perform simple bitwise copying, should be fine.

    bool operator==(const const_iterator &rhs) const
    {
      return (_iter == rhs._iter);
    }

    bool operator!=(const const_iterator &rhs) const
    {
      return (_iter != rhs._iter);
    }

    // Dereference this iterator to get a pair.
    std::pair<T, T> operator*() const
    {
      return *_iter;
    }

    // Return the interval start.
    T get_start() const
    {
      return _iter->first;
    }

    // Return the interval length.
    T get_len() const
    {
      return _iter->second;
    }

    // Preincrement
    const_iterator &operator++()
    {
      ++_iter;
      return *this;
    }

    // Postincrement
    const_iterator operator++(int)
    {
      const_iterator prev(_iter);
      ++_iter;
      return prev;
    }

  protected:
    typename std::map<T, T>::const_iterator _iter;
  };

  IntervalSet() : _size(0) {}
  IntervalSet(std::map<T, T> &other)
  {
    m.swap(other);
    _size = 0;
    for (auto &i : m)
    {
      _size += i.second;
    }
  }

  int num_intervals() const
  {
    return m.size();
  }

  typename IntervalSet<T>::iterator begin()
  {
    return typename IntervalSet<T>::iterator(m.begin());
  }

  typename IntervalSet<T>::iterator lower_bound(T start)
  {
    return typename IntervalSet<T>::iterator(find_inc_m(start));
  }

  typename IntervalSet<T>::iterator end()
  {
    return typename IntervalSet<T>::iterator(m.end());
  }

  typename IntervalSet<T>::const_iterator begin() const
  {
    return typename IntervalSet<T>::const_iterator(m.begin());
  }

  typename IntervalSet<T>::const_iterator lower_bound(T start) const
  {
    return typename IntervalSet<T>::const_iterator(find_inc(start));
  }

  typename IntervalSet<T>::const_iterator end() const
  {
    return typename IntervalSet<T>::const_iterator(m.end());
  }

  // helpers
private:
  typename std::map<T, T>::const_iterator find_inc(T start) const
  {
    typename std::map<T, T>::const_iterator p = m.lower_bound(start); // p->first >= start
    if (p != m.begin() &&
        (p == m.end() || p->first > start))
    {
      p--; // might overlap?
      if (p->first + p->second <= start)
        p++; // it doesn't.
    }
    return p;
  }

  typename std::map<T, T>::iterator find_inc_m(T start)
  {
    typename std::map<T, T>::iterator p = m.lower_bound(start);
    if (p != m.begin() &&
        (p == m.end() || p->first > start))
    {
      p--; // might overlap?
      if (p->first + p->second <= start)
        p++; // it doesn't.
    }
    return p;
  }

  typename std::map<T, T>::const_iterator find_adj(T start) const
  {
    typename std::map<T, T>::const_iterator p = m.lower_bound(start);
    if (p != m.begin() &&
        (p == m.end() || p->first > start))
    {
      p--; // might touch?
      if (p->first + p->second < start)
        p++; // it doesn't.
    }
    return p;
  }

  typename std::map<T, T>::iterator find_adj_m(T start)
  {
    typename std::map<T, T>::iterator p = m.lower_bound(start);
    if (p != m.begin() &&
        (p == m.end() || p->first > start))
    {
      p--; // might touch?
      if (p->first + p->second < start)
        p++; // it doesn't.
    }
    return p;
  }

public:
  bool operator==(const IntervalSet &other) const
  {
    return _size == other._size && m == other.m;
  }

  int64_t size() const
  {
    return _size;
  }

  void clear()
  {
    m.clear();
    _size = 0;
  }

  bool contains(T i, T *pstart = 0, T *plen = 0) const
  {
    typename std::map<T, T>::const_iterator p = find_inc(i);
    if (p == m.end())
      return false;
    if (p->first > i)
      return false;
    if (p->first + p->second <= i)
      return false;
    assert(p->first <= i && p->first + p->second > i);
    if (pstart)
      *pstart = p->first;
    if (plen)
      *plen = p->second;
    return true;
  }
  bool contains(T start, T len) const
  {
    typename std::map<T, T>::const_iterator p = find_inc(start);
    if (p == m.end())
      return false;
    if (p->first > start)
      return false;
    if (p->first + p->second <= start)
      return false;
    assert(p->first <= start && p->first + p->second > start);
    if (p->first + p->second < start + len)
      return false;
    return true;
  }
  bool intersects(T start, T len) const
  {
    IntervalSet a;
    a.insert(start, len);
    IntervalSet i;
    i.intersection_of(*this, a);
    if (i.empty())
      return false;
    return true;
  }

  // outer range of set
  bool empty() const
  {
    return m.empty();
  }
  T range_start() const
  {
    assert(!empty());
    typename std::map<T, T>::const_iterator p = m.begin();
    return p->first;
  }
  T range_end() const
  {
    assert(!empty());
    typename std::map<T, T>::const_iterator p = m.end();
    p--;
    return p->first + p->second;
  }

  // interval start after p (where p not in set)
  bool starts_after(T i) const
  {
    assert(!contains(i));
    typename std::map<T, T>::const_iterator p = find_inc(i);
    if (p == m.end())
      return false;
    return true;
  }
  T start_after(T i) const
  {
    assert(!contains(i));
    typename std::map<T, T>::const_iterator p = find_inc(i);
    return p->first;
  }

  // interval end that contains start
  T end_after(T start) const
  {
    assert(contains(start));
    typename std::map<T, T>::const_iterator p = find_inc(start);
    return p->first + p->second;
  }

  void insert(T val)
  {
    insert(val, 1);
  }

  void insert(T start, T len, T *pstart = 0, T *plen = 0)
  {
    //cout << "insert " << start << "~" << len << endl;
    assert(len > 0);
    _size += len;
    typename std::map<T, T>::iterator p = find_adj_m(start);
    if (p == m.end())
    {
      m[start] = len; // new interval
      if (pstart)
        *pstart = start;
      if (plen)
        *plen = len;
    }
    else
    {
      if (p->first < start)
      {

        if (p->first + p->second != start)
        {
          //cout << "p is " << p->first << "~" << p->second << ", start is " << start << ", len is " << len << endl;
          abort();
        }

        p->second += len; // append to end

        typename std::map<T, T>::iterator n = p;
        n++;
        if (n != m.end() &&
            start + len == n->first)
        { // combine with next, too!
          p->second += n->second;
          m.erase(n);
        }
        if (pstart)
          *pstart = p->first;
        if (plen)
          *plen = p->second;
      }
      else
      {
        if (start + len == p->first)
        {
          m[start] = len + p->second; // append to front
          if (pstart)
            *pstart = start;
          if (plen)
            *plen = len + p->second;
          m.erase(p);
        }
        else
        {
          assert(p->first > start + len);
          m[start] = len; // new interval
          if (pstart)
            *pstart = start;
          if (plen)
            *plen = len;
        }
      }
    }
  }

  void swap(IntervalSet<T> &other)
  {
    m.swap(other.m);
    std::swap(_size, other._size);
  }

  void erase(iterator &i)
  {
    _size -= i.get_len();
    assert(_size >= 0);
    m.erase(i._iter);
  }

  void erase(T val)
  {
    erase(val, 1);
  }

  void erase(T start, T len)
  {
    typename std::map<T, T>::iterator p = find_inc_m(start);

    _size -= len;
    assert(_size >= 0);

    assert(p != m.end());
    assert(p->first <= start);

    T before = start - p->first;
    assert(p->second >= before + len);
    T after = p->second - before - len;

    if (before)
      p->second = before; // shorten bit before
    else
      m.erase(p);
    if (after)
      m[start + len] = after;
  }

  void subtract(const IntervalSet &a)
  {
    for (typename std::map<T, T>::const_iterator p = a.m.begin();
         p != a.m.end();
         p++)
      erase(p->first, p->second);
  }

  void insert(const IntervalSet &a)
  {
    for (typename std::map<T, T>::const_iterator p = a.m.begin();
         p != a.m.end();
         p++)
      insert(p->first, p->second);
  }

  void intersection_of(const IntervalSet &a, const IntervalSet &b)
  {
    assert(&a != this);
    assert(&b != this);
    clear();

    typename std::map<T, T>::const_iterator pa = a.m.begin();
    typename std::map<T, T>::const_iterator pb = b.m.begin();

    while (pa != a.m.end() && pb != b.m.end())
    {
      // passing?
      if (pa->first + pa->second <= pb->first)
      {
        pa++;
        continue;
      }
      if (pb->first + pb->second <= pa->first)
      {
        pb++;
        continue;
      }
      T start = MATH_MAX(pa->first, pb->first);
      T en = MATH_MIN(pa->first + pa->second, pb->first + pb->second);
      assert(en > start);
      insert(start, en - start);
      if (pa->first + pa->second > pb->first + pb->second)
        pb++;
      else
        pa++;
    }
  }
  void intersection_of(const IntervalSet &b)
  {
    IntervalSet a;
    swap(a);
    intersection_of(a, b);
  }

  void union_of(const IntervalSet &a, const IntervalSet &b)
  {
    assert(&a != this);
    assert(&b != this);
    clear();

    //cout << "union_of" << endl;

    // a
    m = a.m;
    _size = a._size;

    // - (a*b)
    IntervalSet ab;
    ab.intersection_of(a, b);
    subtract(ab);

    // + b
    insert(b);
    return;
  }
  void union_of(const IntervalSet &b)
  {
    IntervalSet a;
    swap(a);
    union_of(a, b);
  }
  void union_insert(T off, T len)
  {
    IntervalSet a;
    a.insert(off, len);
    union_of(a);
  }

  bool subset_of(const IntervalSet &big) const
  {
    for (typename std::map<T, T>::const_iterator i = m.begin();
         i != m.end();
         i++)
      if (!big.contains(i->first, i->second))
        return false;
    return true;
  }

  /*
   * build a subset of @other, starting at or after @start, and including
   * @len worth of values, skipping holes.  e.g.,
   *  span_of([5~10,20~5], 8, 5) -> [8~2,20~3]
   */
  void span_of(const IntervalSet &other, T start, T len)
  {
    clear();
    typename std::map<T, T>::const_iterator p = other.find_inc(start);
    if (p == other.m.end())
      return;
    if (p->first < start)
    {
      if (p->first + p->second < start)
        return;
      if (p->first + p->second < start + len)
      {
        T howmuch = p->second - (start - p->first);
        insert(start, howmuch);
        len -= howmuch;
        p++;
      }
      else
      {
        insert(start, len);
        return;
      }
    }
    while (p != other.m.end() && len > 0)
    {
      if (p->second < len)
      {
        insert(p->first, p->second);
        len -= p->second;
        p++;
      }
      else
      {
        insert(p->first, len);
        return;
      }
    }
  }

  /*
   * Move contents of m into another std::map<T,T>. Use that instead of
   * encoding IntervalSet into bufferlist then decoding it back into std::map.
   */
  void move_into(std::map<T, T> &other)
  {
    other = std::move(m);
  }

private:
  // data
  int64_t _size;
  std::map<T, T> m; // map start -> len
};

template <class T>
inline std::ostream &operator<<(std::ostream &out, const IntervalSet<T> &s)
{
  out << "[";
  const char *prequel = "";
  for (typename IntervalSet<T>::const_iterator i = s.begin();
       i != s.end();
       ++i)
  {
    out << prequel << i.get_start() << "~" << i.get_len();
    prequel = ",";
  }
  out << "]";
  return out;
}

template <typename K, typename V, typename S>
class interval_map
{
  S s;
  using map = std::map<K, std::pair<K, V>>;
  using mapiter = typename std::map<K, std::pair<K, V>>::iterator;
  using cmapiter = typename std::map<K, std::pair<K, V>>::const_iterator;
  map m;
  std::pair<mapiter, mapiter> get_range(K off, K len)
  {
    // fst is first iterator with end after off (may be end)
    auto fst = m.upper_bound(off);
    if (fst != m.begin())
      --fst;
    if (fst != m.end() && off >= (fst->first + fst->second.first))
      ++fst;

    // lst is first iterator with start after off + len (may be end)
    auto lst = m.lower_bound(off + len);
    return std::make_pair(fst, lst);
  }
  std::pair<cmapiter, cmapiter> get_range(K off, K len) const
  {
    // fst is first iterator with end after off (may be end)
    auto fst = m.upper_bound(off);
    if (fst != m.begin())
      --fst;
    if (fst != m.end() && off >= (fst->first + fst->second.first))
      ++fst;

    // lst is first iterator with start after off + len (may be end)
    auto lst = m.lower_bound(off + len);
    return std::make_pair(fst, lst);
  }
  void try_merge(mapiter niter)
  {
    if (niter != m.begin())
    {
      auto prev = niter;
      prev--;
      if (prev->first + prev->second.first == niter->first &&
          s.can_merge(prev->second.second, niter->second.second))
      {
        V n = s.merge(
            std::move(prev->second.second),
            std::move(niter->second.second));
        K off = prev->first;
        K len = niter->first + niter->second.first - off;
        niter++;
        m.erase(prev, niter);
        auto p = m.insert(
            std::make_pair(
                off,
                std::make_pair(len, std::move(n))));
        assert(p.second);
        niter = p.first;
      }
    }
    auto next = niter;
    next++;
    if (next != m.end() &&
        niter->first + niter->second.first == next->first &&
        s.can_merge(niter->second.second, next->second.second))
    {
      V n = s.merge(
          std::move(niter->second.second),
          std::move(next->second.second));
      K off = niter->first;
      K len = next->first + next->second.first - off;
      next++;
      m.erase(niter, next);
      auto p = m.insert(
          std::make_pair(
              off,
              std::make_pair(len, std::move(n))));
      assert(p.second);
    }
  }

public:
  interval_map intersect(K off, K len) const
  {
    interval_map ret;
    auto limits = get_range(off, len);
    for (auto i = limits.first; i != limits.second; ++i)
    {
      K o = i->first;
      K l = i->second.first;
      V v = i->second.second;
      if (o < off)
      {
        V p = v;
        l -= (off - o);
        v = s.split(off - o, l, p);
        o = off;
      }
      if ((o + l) > (off + len))
      {
        V p = v;
        l -= (o + l) - (off + len);
        v = s.split(0, l, p);
      }
      ret.insert(o, l, v);
    }
    return ret;
  }
  void clear()
  {
    m.clear();
  }
  void erase(K off, K len)
  {
    if (len == 0)
      return;
    auto range = get_range(off, len);
    std::vector<
        std::pair<
            K,
            std::pair<K, V>>>
        to_insert;
    for (auto i = range.first; i != range.second; ++i)
    {
      if (i->first < off)
      {
        to_insert.emplace_back(
            std::make_pair(
                i->first,
                std::make_pair(
                    off - i->first,
                    s.split(0, off - i->first, i->second.second))));
      }
      if ((off + len) < (i->first + i->second.first))
      {
        K nlen = (i->first + i->second.first) - (off + len);
        to_insert.emplace_back(
            std::make_pair(
                off + len,
                std::make_pair(
                    nlen,
                    s.split(i->second.first - nlen, nlen, i->second.second))));
      }
    }
    m.erase(range.first, range.second);
    m.insert(to_insert.begin(), to_insert.end());
  }
  void insert(K off, K len, V &&v)
  {
    assert(len > 0);
    assert(len == s.length(v));
    erase(off, len);
    auto p = m.insert(make_pair(off, std::make_pair(len, std::forward<V>(v))));
    assert(p.second);
    try_merge(p.first);
  }
  void insert(interval_map &&other)
  {
    for (auto i = other.m.begin();
         i != other.m.end();
         other.m.erase(i++))
    {
      insert(i->first, i->second.first, std::move(i->second.second));
    }
  }
  void insert(K off, K len, const V &v)
  {
    assert(len > 0);
    assert(len == s.length(v));
    erase(off, len);
    auto p = m.insert(make_pair(off, std::make_pair(len, v)));
    assert(p.second);
    try_merge(p.first);
  }
  void insert(const interval_map &other)
  {
    for (auto &&i : other)
    {
      insert(i.get_off(), i.get_len(), i.get_val());
    }
  }
  bool empty() const
  {
    return m.empty();
  }
  IntervalSet<K> get_interval_set() const
  {
    IntervalSet<K> ret;
    for (auto &&i : *this)
    {
      ret.insert(i.get_off(), i.get_len());
    }
    return ret;
  }
  class const_iterator
  {
    cmapiter it;
    const_iterator(cmapiter &&it) : it(std::move(it)) {}
    const_iterator(const cmapiter &it) : it(it) {}

    friend class interval_map;

  public:
    const_iterator(const const_iterator &) = default;
    const_iterator &operator=(const const_iterator &) = default;

    const_iterator &operator++()
    {
      ++it;
      return *this;
    }
    const_iterator operator++(int)
    {
      return const_iterator(it++);
    }
    const_iterator &operator--()
    {
      --it;
      return *this;
    }
    const_iterator operator--(int)
    {
      return const_iterator(it--);
    }
    bool operator==(const const_iterator &rhs) const
    {
      return it == rhs.it;
    }
    bool operator!=(const const_iterator &rhs) const
    {
      return it != rhs.it;
    }
    K get_off() const
    {
      return it->first;
    }
    K get_len() const
    {
      return it->second.first;
    }
    const V &get_val() const
    {
      return it->second.second;
    }
    const_iterator &operator*()
    {
      return *this;
    }
  };
  const_iterator begin() const
  {
    return const_iterator(m.begin());
  }
  const_iterator end() const
  {
    return const_iterator(m.end());
  }
  std::pair<const_iterator, const_iterator> get_containing_range(
      K off,
      K len) const
  {
    auto rng = get_range(off, len);
    return std::make_pair(const_iterator(rng.first), const_iterator(rng.second));
  }
  unsigned ext_count() const
  {
    return m.size();
  }
  bool operator==(const interval_map &rhs) const
  {
    return m == rhs.m;
  }

  std::ostream &print(std::ostream &out) const
  {
    bool first = true;
    out << "{";
    for (auto &&i : *this)
    {
      if (first)
      {
        first = false;
      }
      else
      {
        out << ",";
      }
      out << i.get_off() << "~" << i.get_len() << "("
          << s.length(i.get_val()) << ")";
    }
    return out << "}";
  }
};

template <typename K, typename V, typename S>
std::ostream &operator<<(std::ostream &out, const interval_map<K, V, S> &m)
{
  return m.print(out);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MAP_UTIL_H_