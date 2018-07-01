
#ifndef MYCC_UTIL_SMALL_CONTAINER_H_
#define MYCC_UTIL_SMALL_CONTAINER_H_

#include <assert.h>
#include <algorithm>
#include <array>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

// unordered_map implemented as a simple array
/* 
 * use like:
 * static ArrayMap<sstring, 20> level_map = {
            { int(log_level::debug), "DEBUG" },
            { int(log_level::info),  "INFO "  },
            { int(log_level::trace), "TRACE" },
            { int(log_level::warn),  "WARN "  },
            { int(log_level::error), "ERROR" },
    };
 */

template <typename Value, uint64_t Max>
class ArrayMap
{
  std::array<Value, Max> _a{};

public:
  ArrayMap(std::initializer_list<std::pair<uint64_t, Value>> i)
  {
    for (auto kv : i)
    {
      _a[kv.first] = kv.second;
    }
  }
  Value &operator[](uint64_t key) { return _a[key]; }
  const Value &operator[](uint64_t key) const { return _a[key]; }

  Value &at(uint64_t key)
  {
    if (key >= Max)
    {
      throw std::out_of_range(std::to_string(key) + " >= " + std::to_string(Max));
    }
    return _a[key];
  }
};

// A vector that leverages pre-allocated stack-based array to achieve better
// performance for array with small amount of items.
//
// The interface resembles that of vector, but with less features since we aim
// to solve the problem that we have in hand, rather than implementing a
// full-fledged generic container.
//
// Currently we don't support:
//  * reserve()/shrink_to_fit()
//     If used correctly, in most cases, people should not touch the
//     underlying vector at all.
//  * random insert()/erase(), please only use push_back()/pop_back().
//  * No move/swap operations. Each autovector instance has a
//     stack-allocated array and if we want support move/swap operations, we
//     need to copy the arrays other than just swapping the pointers. In this
//     case we'll just explicitly forbid these operations since they may
//     lead users to make false assumption by thinking they are inexpensive
//     operations.
//
// Naming style of public methods almost follows that of the STL's.
template <class T, uint64_t kSize = 8>
class autovector
{
public:
  // General STL-style container member types.
  typedef T value_type;
  typedef typename std::vector<T>::difference_type difference_type;
  typedef typename std::vector<T>::size_type size_type;
  typedef value_type &reference;
  typedef const value_type &const_reference;
  typedef value_type *pointer;
  typedef const value_type *const_pointer;

  // This class is the base for regular/const iterator
  template <class TAutoVector, class TValueType>
  class iterator_impl
  {
  public:
    // -- iterator traits
    typedef iterator_impl<TAutoVector, TValueType> self_type;
    typedef TValueType value_type;
    typedef TValueType &reference;
    typedef TValueType *pointer;
    typedef typename TAutoVector::difference_type difference_type;
    typedef std::random_access_iterator_tag iterator_category;

    iterator_impl(TAutoVector *vect, uint64_t index)
        : vect_(vect), index_(index){};
    iterator_impl(const iterator_impl &) = default;
    ~iterator_impl() {}
    iterator_impl &operator=(const iterator_impl &) = default;

    // -- Advancement
    // ++iterator
    self_type &operator++()
    {
      ++index_;
      return *this;
    }

    // iterator++
    self_type operator++(int)
    {
      auto old = *this;
      ++index_;
      return old;
    }

    // --iterator
    self_type &operator--()
    {
      --index_;
      return *this;
    }

    // iterator--
    self_type operator--(int)
    {
      auto old = *this;
      --index_;
      return old;
    }

    self_type operator-(difference_type len) const
    {
      return self_type(vect_, index_ - len);
    }

    difference_type operator-(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ - other.index_;
    }

    self_type operator+(difference_type len) const
    {
      return self_type(vect_, index_ + len);
    }

    self_type &operator+=(difference_type len)
    {
      index_ += len;
      return *this;
    }

    self_type &operator-=(difference_type len)
    {
      index_ -= len;
      return *this;
    }

    // -- Reference
    reference operator*()
    {
      assert(vect_->size() >= index_);
      return (*vect_)[index_];
    }

    const_reference operator*() const
    {
      assert(vect_->size() >= index_);
      return (*vect_)[index_];
    }

    pointer operator->()
    {
      assert(vect_->size() >= index_);
      return &(*vect_)[index_];
    }

    const_pointer operator->() const
    {
      assert(vect_->size() >= index_);
      return &(*vect_)[index_];
    }

    // -- Logical Operators
    bool operator==(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ == other.index_;
    }

    bool operator!=(const self_type &other) const { return !(*this == other); }

    bool operator>(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ > other.index_;
    }

    bool operator<(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ < other.index_;
    }

    bool operator>=(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ >= other.index_;
    }

    bool operator<=(const self_type &other) const
    {
      assert(vect_ == other.vect_);
      return index_ <= other.index_;
    }

  private:
    TAutoVector *vect_ = nullptr;
    uint64_t index_ = 0;
  };

  typedef iterator_impl<autovector, value_type> iterator;
  typedef iterator_impl<const autovector, const value_type> const_iterator;
  typedef std::reverse_iterator<iterator> reverse_iterator;
  typedef std::reverse_iterator<const_iterator> const_reverse_iterator;

  autovector() = default;

  autovector(std::initializer_list<T> init_list)
  {
    for (const T &item : init_list)
    {
      push_back(item);
    }
  }

  ~autovector() = default;

  // -- Immutable operations
  // Indicate if all data resides in in-stack data structure.
  bool only_in_stack() const
  {
    // If no element was inserted at all, the vector's capacity will be `0`.
    return vect_.capacity() == 0;
  }

  size_type size() const { return num_stack_items_ + vect_.size(); }

  // resize does not guarantee anything about the contents of the newly
  // available elements
  void resize(size_type n)
  {
    if (n > kSize)
    {
      vect_.resize(n - kSize);
      num_stack_items_ = kSize;
    }
    else
    {
      vect_.clear();
      num_stack_items_ = n;
    }
  }

  bool empty() const { return size() == 0; }

  const_reference operator[](size_type n) const
  {
    assert(n < size());
    return n < kSize ? values_[n] : vect_[n - kSize];
  }

  reference operator[](size_type n)
  {
    assert(n < size());
    return n < kSize ? values_[n] : vect_[n - kSize];
  }

  const_reference at(size_type n) const
  {
    assert(n < size());
    return (*this)[n];
  }

  reference at(size_type n)
  {
    assert(n < size());
    return (*this)[n];
  }

  reference front()
  {
    assert(!empty());
    return *begin();
  }

  const_reference front() const
  {
    assert(!empty());
    return *begin();
  }

  reference back()
  {
    assert(!empty());
    return *(end() - 1);
  }

  const_reference back() const
  {
    assert(!empty());
    return *(end() - 1);
  }

  // -- Mutable Operations
  void push_back(T &&item)
  {
    if (num_stack_items_ < kSize)
    {
      values_[num_stack_items_++] = std::move(item);
    }
    else
    {
      vect_.push_back(item);
    }
  }

  void push_back(const T &item)
  {
    if (num_stack_items_ < kSize)
    {
      values_[num_stack_items_++] = item;
    }
    else
    {
      vect_.push_back(item);
    }
  }

  template <class... Args>
  void emplace_back(Args &&... args)
  {
    push_back(value_type(args...));
  }

  void pop_back()
  {
    assert(!empty());
    if (!vect_.empty())
    {
      vect_.pop_back();
    }
    else
    {
      --num_stack_items_;
    }
  }

  void clear()
  {
    num_stack_items_ = 0;
    vect_.clear();
  }

  // -- Copy and Assignment
  autovector &assign(const autovector &other);

  autovector(const autovector &other) { assign(other); }

  autovector &operator=(const autovector &other) { return assign(other); }

  // -- Iterator Operations
  iterator begin() { return iterator(this, 0); }

  const_iterator begin() const { return const_iterator(this, 0); }

  iterator end() { return iterator(this, this->size()); }

  const_iterator end() const { return const_iterator(this, this->size()); }

  reverse_iterator rbegin() { return reverse_iterator(end()); }

  const_reverse_iterator rbegin() const
  {
    return const_reverse_iterator(end());
  }

  reverse_iterator rend() { return reverse_iterator(begin()); }

  const_reverse_iterator rend() const
  {
    return const_reverse_iterator(begin());
  }

private:
  size_type num_stack_items_ = 0; // current number of items
  value_type values_[kSize];      // the first `kSize` items
  // used only if there are more than `kSize` items.
  std::vector<T> vect_;
};

template <class T, uint64_t kSize>
autovector<T, kSize> &autovector<T, kSize>::assign(const autovector &other)
{
  // copy the internal vector
  vect_.assign(other.vect_.begin(), other.vect_.end());

  // copy array
  num_stack_items_ = other.num_stack_items_;
  std::copy(other.values_, other.values_ + num_stack_items_, values_);

  return *this;
}

// This is similar to std::unordered_map, except that it tries to avoid
// allocating or deallocating memory as much as possible. With
// std::unordered_map, an allocation/deallocation is made for every insertion
// or deletion because of the requirement that iterators remain valid even
// with insertions or deletions. This means that the hash chains will be
// implemented as linked lists.
//
// This implementation uses autovector as hash chains insteads.
//
template <typename K, typename V, uint64_t size = 128>
class SmallHashMap
{
  std::array<autovector<std::pair<K, V>, 1>, size> table_;

public:
  bool Contains(K key)
  {
    auto &bucket = table_[key % size];
    auto it = std::find_if(
        bucket.begin(), bucket.end(),
        [key](const std::pair<K, V> &p) { return p.first == key; });
    return it != bucket.end();
  }

  void Insert(K key, V value)
  {
    auto &bucket = table_[key % size];
    bucket.push_back({key, value});
  }

  void Delete(K key)
  {
    auto &bucket = table_[key % size];
    auto it = std::find_if(
        bucket.begin(), bucket.end(),
        [key](const std::pair<K, V> &p) { return p.first == key; });
    if (it != bucket.end())
    {
      auto last = bucket.end() - 1;
      if (it != last)
      {
        *it = *last;
      }
      bucket.pop_back();
    }
  }

  V &Get(K key)
  {
    auto &bucket = table_[key % size];
    auto it = std::find_if(
        bucket.begin(), bucket.end(),
        [key](const std::pair<K, V> &p) { return p.first == key; });
    return it->second;
  }
};

// Binary heap implementation optimized for use in multi-way merge sort.
// Comparison to std::priority_queue:
// - In libstdc++, std::priority_queue::pop() usually performs just over logN
//   comparisons but never fewer.
// - std::priority_queue does not have a replace-top operation, requiring a
//   pop+push.  If the replacement element is the new top, this requires
//   around 2logN comparisons.
// - This heap's pop() uses a "schoolbook" downheap which requires up to ~2logN
//   comparisons.
// - This heap provides a replace_top() operation which requires [1, 2logN]
//   comparisons.  When the replacement element is also the new top, this
//   takes just 1 or 2 comparisons.
//
// The last property can yield an order-of-magnitude performance improvement
// when merge-sorting real-world non-random data.  If the merge operation is
// likely to take chunks of elements from the same input stream, only 1
// comparison per element is needed.  In RocksDB-land, this happens when
// compacting a database where keys are not randomly distributed across L0
// files but nearby keys are likely to be in the same L0 file.
//
// The container uses the same counterintuitive ordering as
// std::priority_queue: the comparison operator is expected to provide the
// less-than relation, but top() will return the maximum.

template <typename T, typename Compare = std::less<T>>
class BinaryHeap
{
public:
  BinaryHeap() {}
  explicit BinaryHeap(Compare cmp) : cmp_(std::move(cmp)) {}

  void push(const T &value)
  {
    data_.push_back(value);
    upheap(data_.size() - 1);
  }

  void push(T &&value)
  {
    data_.push_back(std::move(value));
    upheap(data_.size() - 1);
  }

  const T &top() const
  {
    assert(!empty());
    return data_.front();
  }

  void replace_top(const T &value)
  {
    assert(!empty());
    data_.front() = value;
    downheap(get_root());
  }

  void replace_top(T &&value)
  {
    assert(!empty());
    data_.front() = std::move(value);
    downheap(get_root());
  }

  void pop()
  {
    assert(!empty());
    data_.front() = std::move(data_.back());
    data_.pop_back();
    if (!empty())
    {
      downheap(get_root());
    }
    else
    {
      reset_root_cmp_cache();
    }
  }

  void swap(BinaryHeap &other)
  {
    std::swap(cmp_, other.cmp_);
    data_.swap(other.data_);
    std::swap(root_cmp_cache_, other.root_cmp_cache_);
  }

  void clear()
  {
    data_.clear();
    reset_root_cmp_cache();
  }

  bool empty() const
  {
    return data_.empty();
  }

  void reset_root_cmp_cache() { root_cmp_cache_ = port::kMaxSizet; }

private:
  static inline uint64_t get_root() { return 0; }
  static inline uint64_t get_parent(uint64_t index) { return (index - 1) / 2; }
  static inline uint64_t get_left(uint64_t index) { return 2 * index + 1; }
  static inline uint64_t get_right(uint64_t index) { return 2 * index + 2; }

  void upheap(uint64_t index)
  {
    T v = std::move(data_[index]);
    while (index > get_root())
    {
      const uint64_t parent = get_parent(index);
      if (!cmp_(data_[parent], v))
      {
        break;
      }
      data_[index] = std::move(data_[parent]);
      index = parent;
    }
    data_[index] = std::move(v);
    reset_root_cmp_cache();
  }

  void downheap(uint64_t index)
  {
    T v = std::move(data_[index]);

    uint64_t picked_child = port::kMaxSizet;
    while (1)
    {
      const uint64_t left_child = get_left(index);
      if (get_left(index) >= data_.size())
      {
        break;
      }
      const uint64_t right_child = left_child + 1;
      assert(right_child == get_right(index));
      picked_child = left_child;
      if (index == 0 && root_cmp_cache_ < data_.size())
      {
        picked_child = root_cmp_cache_;
      }
      else if (right_child < data_.size() &&
               cmp_(data_[left_child], data_[right_child]))
      {
        picked_child = right_child;
      }
      if (!cmp_(v, data_[picked_child]))
      {
        break;
      }
      data_[index] = std::move(data_[picked_child]);
      index = picked_child;
    }

    if (index == 0)
    {
      // We did not change anything in the tree except for the value
      // of the root node, left and right child did not change, we can
      // cache that `picked_child` is the smallest child
      // so next time we compare againist it directly
      root_cmp_cache_ = picked_child;
    }
    else
    {
      // the tree changed, reset cache
      reset_root_cmp_cache();
    }

    data_[index] = std::move(v);
  }

  Compare cmp_;
  autovector<T> data_;
  // Used to reduce number of cmp_ calls in downheap()
  uint64_t root_cmp_cache_ = (uint64_t)kMaxSizet;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SMALL_CONTAINER_H_