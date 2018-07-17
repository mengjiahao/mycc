
#ifndef MYCC_UTIL_SMALL_CONTAINER_H_
#define MYCC_UTIL_SMALL_CONTAINER_H_

#include <assert.h>
#include <algorithm>
#include <array>
#include <deque>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <type_traits>
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
            { int32_t(log_level::debug), "DEBUG" },
            { int32_t(log_level::info),  "INFO "  },
            { int32_t(log_level::trace), "TRACE" },
            { int32_t(log_level::warn),  "WARN "  },
            { int32_t(log_level::error), "ERROR" },
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

// Many functions read from or write to arrays. The obvious way to do this is
// to use two arguments, a pointer to the first element and an element count:
//
//   bool Contains17(const int32_t* arr, uint64_t size) {
//     for (uint64_t i = 0; i < size; ++i) {
//       if (arr[i] == 17)
//         return true;
//     }
//     return false;
//   }
//
// This is flexible, since it doesn't matter how the array is stored (C array,
// std::vector, std::array, ...), but it's error-prone because the caller has
// to correctly specify the array length:
//
//   Contains17(arr, arraysize(arr));  // C array
//   Contains17(&arr[0], arr.size());  // std::vector
//   Contains17(arr, size);            // pointer + size
//   ...
//
// It's also kind of messy to have two separate arguments for what is
// conceptually a single thing.
//
// Enter kudu::ArrayView<T>. It contains a T pointer (to an array it doesn't
// own) and a count, and supports the basic things you'd expect, such as
// indexing and iteration. It allows us to write our function like this:
//
//   bool Contains17(kudu::ArrayView<const int32_t> arr) {
//     for (auto e : arr) {
//       if (e == 17)
//         return true;
//     }
//     return false;
//   }
//
// And even better, because a bunch of things will implicitly convert to
// ArrayView, we can call it like this:
//
//   Contains17(arr);                             // C array
//   Contains17(arr);                             // std::vector
//   Contains17(kudu::ArrayView<int32_t>(arr, size)); // pointer + size
//   ...
//
// One important point is that ArrayView<T> and ArrayView<const T> are
// different types, which allow and don't allow mutation of the array elements,
// respectively. The implicit conversions work just like you'd hope, so that
// e.g. vector<int32_t> will convert to either ArrayView<int32_t> or ArrayView<const
// int32_t>, but const vector<int32_t> will convert only to ArrayView<const int32_t>.
// (ArrayView itself can be the source type in such conversions, so
// ArrayView<int32_t> will convert to ArrayView<const int32_t>.)
//
// Note: ArrayView is tiny (just a pointer and a count) and trivially copyable,
// so it's probably cheaper to pass it by value than by const reference.
template <typename T>
class ArrayView final
{
public:
  // Construct an empty ArrayView.
  ArrayView() : ArrayView(static_cast<T *>(nullptr), 0) {}

  // Construct an ArrayView for a (pointer,size) pair.
  template <typename U>
  ArrayView(U *data, uint64_t size)
      : data_(size == 0 ? nullptr : data), size_(size)
  {
    CheckInvariant();
  }

  // Construct an ArrayView for an array.
  template <typename U, uint64_t N>
  ArrayView(U (&array)[N]) : ArrayView(&array[0], N) {} // NOLINT(runtime/explicit)

  // Construct an ArrayView for any type U that has a size() method whose
  // return value converts implicitly to uint64_t, and a data() method whose
  // return value converts implicitly to T*. In particular, this means we allow
  // conversion from ArrayView<T> to ArrayView<const T>, but not the other way
  // around. Other allowed conversions include std::vector<T> to ArrayView<T>
  // or ArrayView<const T>, const std::vector<T> to ArrayView<const T>, and
  // kudu::faststring to ArrayView<uint8_t> (with the same const behavior as
  // std::vector).
  template <typename U>
  ArrayView(U &u) : ArrayView(u.data(), u.size()) {} // NOLINT(runtime/explicit)

  // Indexing, size, and iteration. These allow mutation even if the ArrayView
  // is const, because the ArrayView doesn't own the array. (To prevent
  // mutation, use ArrayView<const T>.)
  uint64_t size() const { return size_; }
  bool empty() const { return size_ == 0; }
  T *data() const { return data_; }
  T &operator[](uint64_t idx) const
  {
    //DCHECK_LT(idx, size_);
    //DCHECK(data_); // Follows from size_ > idx and the class invariant.
    return data_[idx];
  }
  T *begin() const { return data_; }
  T *end() const { return data_ + size_; }
  const T *cbegin() const { return data_; }
  const T *cend() const { return data_ + size_; }

  // Comparing two ArrayViews compares their (pointer,size) pairs; it does
  // *not* dereference the pointers.
  friend bool operator==(const ArrayView &a, const ArrayView &b)
  {
    return a.data_ == b.data_ && a.size_ == b.size_;
  }
  friend bool operator!=(const ArrayView &a, const ArrayView &b)
  {
    return !(a == b);
  }

private:
  // Invariant: !data_ iff size_ == 0.
  void CheckInvariant() const
  {
    //DCHECK_EQ(!data_, size_ == 0);
  }

  T *data_;
  uint64_t size_;
};

// This queue reduces the chance to allocate memory for deque
template <typename T, int32_t N>
class SmallQueue
{
public:
  SmallQueue() : _begin(0), _size(0), _full(NULL) {}

  void push(const T &val)
  {
    if (_full != NULL && !_full->empty())
    {
      _full->push_back(val);
    }
    else if (_size < N)
    {
      int64_t tail = _begin + _size;
      if (tail >= N)
      {
        tail -= N;
      }
      _c[tail] = val;
      ++_size;
    }
    else
    {
      if (_full == NULL)
      {
        _full = new std::deque<T>;
      }
      _full->push_back(val);
    }
  }
  bool pop(T *val)
  {
    if (_size > 0)
    {
      *val = _c[_begin];
      ++_begin;
      if (_begin >= N)
      {
        _begin -= N;
      }
      --_size;
      return true;
    }
    else if (_full && !_full->empty())
    {
      *val = _full->front();
      _full->pop_front();
      return true;
    }
    return false;
  }
  bool empty() const
  {
    return _size == 0 && (_full == NULL || _full->empty());
  }

  uint64_t size() const
  {
    return _size + (_full ? _full->size() : 0);
  }

  void clear()
  {
    _size = 0;
    _begin = 0;
    if (_full)
    {
      _full->clear();
    }
  }

  ~SmallQueue()
  {
    delete _full;
    _full = NULL;
  }

private:
  DISALLOW_COPY_AND_ASSIGN(SmallQueue);

  int64_t _begin;
  int64_t _size;
  T _c[N];
  std::deque<T> *_full;
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
  typedef typename std::vector<T>::uint64_type uint64_type;
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
    self_type operator++(int32_t)
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
    self_type operator--(int32_t)
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

  uint64_type size() const { return num_stack_items_ + vect_.size(); }

  // resize does not guarantee anything about the contents of the newly
  // available elements
  void resize(uint64_type n)
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

  const_reference operator[](uint64_type n) const
  {
    assert(n < size());
    return n < kSize ? values_[n] : vect_[n - kSize];
  }

  reference operator[](uint64_type n)
  {
    assert(n < size());
    return n < kSize ? values_[n] : vect_[n - kSize];
  }

  const_reference at(uint64_type n) const
  {
    assert(n < size());
    return (*this)[n];
  }

  reference at(uint64_type n)
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
  uint64_type num_stack_items_ = 0; // current number of items
  value_type values_[kSize];        // the first `kSize` items
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

  void reset_root_cmp_cache() { root_cmp_cache_ = kMaxSizet; }

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

    uint64_t picked_child = kMaxSizet;
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

// This simple class finds the top n elements of an incrementally provided set
// of elements which you push one at a time.  If the number of elements exceeds
// n, the lowest elements are incrementally dropped.  At the end you get
// a vector of the top elements sorted in descending order (through Extract() or
// ExtractNondestructive()), or a vector of the top elements but not sorted
// (through ExtractUnsorted() or ExtractUnsortedNondestructive()).
//
// The value n is specified in the constructor.  If there are p elements pushed
// altogether:
//   The total storage requirements are O(min(n, p)) elements
//   The running time is O(p * log(min(n, p))) comparisons
// If n is a constant, the total storage required is a constant and the running
// time is linear in p.
//
// NOTE: There is a way to do this in O(min(n, p)) storage and O(p)
// runtime. The basic idea is to repeatedly fill up a buffer of 2 * n elements,
// discarding the lowest n elements whenever the buffer is full using a linear-
// time median algorithm. This may have better performance when the input
// sequence is partially sorted.
// NOTE: This class should be redesigned to avoid reallocating a
// vector for each Extract.

// Cmp is an stl binary predicate.  Note that Cmp is the "greater" predicate,
// not the more commonly used "less" predicate.
//
// If you use a "less" predicate here, the TopNHeap will pick out the bottom N
// elements out of the ones passed to it, and it will return them sorted in
// ascending order.
//
// TopNHeap is rule-of-zero copyable and movable if its members are.
template <class T, class Cmp = std::greater<T>>
class TopNHeap
{
public:
  // The TopNHeap is in one of the three states:
  //
  //  o UNORDERED: this is the state an instance is originally in,
  //    where the elements are completely orderless.
  //
  //  o BOTTOM_KNOWN: in this state, we keep the invariant that there
  //    is at least one element in it, and the lowest element is at
  //    position 0. The elements in other positions remain
  //    unsorted. This state is reached if the state was originally
  //    UNORDERED and a peek_bottom() function call is invoked.
  //
  //  o HEAP_SORTED: in this state, the array is kept as a heap and
  //    there are exactly (limit_+1) elements in the array. This
  //    state is reached when at least (limit_+1) elements are
  //    pushed in.
  //
  //  The state transition graph is at follows:
  //
  //             peek_bottom()                (limit_+1) elements
  //  UNORDERED --------------> BOTTOM_KNOWN --------------------> HEAP_SORTED
  //      |                                                           ^
  //      |                      (limit_+1) elements                  |
  //      +-----------------------------------------------------------+

  enum State
  {
    UNORDERED,
    BOTTOM_KNOWN,
    HEAP_SORTED
  };
  using UnsortedIterator = typename std::vector<T>::const_iterator;

  // 'limit' is the maximum number of top results to return.
  explicit TopNHeap(uint64_t limit) : TopNHeap(limit, Cmp()) {}
  TopNHeap(uint64_t limit, const Cmp &cmp) : limit_(limit), cmp_(cmp) {}

  uint64_t limit() const { return limit_; }

  // Number of elements currently held by this TopNHeap object.  This
  // will be no greater than 'limit' passed to the constructor.
  uint64_t size() const { return std::min(elements_.size(), limit_); }

  bool empty() const { return size() == 0; }

  // If you know how many elements you will push at the time you create the
  // TopNHeap object, you can call reserve to preallocate the memory that TopNHeap
  // will need to process all 'n' pushes.  Calling this method is optional.
  void reserve(uint64_t n) { elements_.reserve(std::min(n, limit_ + 1)); }

  // Push 'v'.  If the maximum number of elements was exceeded, drop the
  // lowest element and return it in 'dropped' (if given). If the maximum is not
  // exceeded, 'dropped' will remain unchanged. 'dropped' may be omitted or
  // nullptr, in which case it is not filled in.
  // Requires: T is CopyAssignable, Swappable
  void push(const T &v) { push(v, nullptr); }
  void push(const T &v, T *dropped) { PushInternal(v, dropped); }

  // Move overloads of push.
  // Requires: T is MoveAssignable, Swappable
  void push(T &&v)
  { // NOLINT(build/c++11)
    push(std::move(v), nullptr);
  }
  void push(T &&v, T *dropped)
  { // NOLINT(build/c++11)
    PushInternal(std::move(v), dropped);
  }

  // Peeks the bottom result without calling Extract()
  const T &peek_bottom();

  // Extract the elements as a vector sorted in descending order.  The caller
  // assumes ownership of the vector and must delete it when done.  This is a
  // destructive operation.  The only method that can be called immediately
  // after Extract() is Reset().
  std::vector<T> *Extract();

  // Similar to Extract(), but makes no guarantees the elements are in sorted
  // order.  As with Extract(), the caller assumes ownership of the vector and
  // must delete it when done.  This is a destructive operation.  The only
  // method that can be called immediately after ExtractUnsorted() is Reset().
  std::vector<T> *ExtractUnsorted();

  // A non-destructive version of Extract(). Copy the elements in a new vector
  // sorted in descending order and return it.  The caller assumes ownership of
  // the new vector and must delete it when done.  After calling
  // ExtractNondestructive(), the caller can continue to push() new elements.
  std::vector<T> *ExtractNondestructive() const;

  // A non-destructive version of Extract(). Copy the elements to a given
  // vector sorted in descending order. After calling
  // ExtractNondestructive(), the caller can continue to push() new elements.
  // Note:
  //  1. The given argument must to be allocated.
  //  2. Any data contained in the vector prior to the call will be deleted
  //     from it. After the call the vector will contain only the elements
  //     from the data structure.
  void ExtractNondestructive(std::vector<T> *output) const;

  // A non-destructive version of ExtractUnsorted(). Copy the elements in a new
  // vector and return it, with no guarantees the elements are in sorted order.
  // The caller assumes ownership of the new vector and must delete it when
  // done.  After calling ExtractUnsortedNondestructive(), the caller can
  // continue to push() new elements.
  std::vector<T> *ExtractUnsortedNondestructive() const;

  // A non-destructive version of ExtractUnsorted(). Copy the elements into
  // a given vector, with no guarantees the elements are in sorted order.
  // After calling ExtractUnsortedNondestructive(), the caller can continue
  // to push() new elements.
  // Note:
  //  1. The given argument must to be allocated.
  //  2. Any data contained in the vector prior to the call will be deleted
  //     from it. After the call the vector will contain only the elements
  //     from the data structure.
  void ExtractUnsortedNondestructive(std::vector<T> *output) const;

  // Return an iterator to the beginning (end) of the container,
  // with no guarantees about the order of iteration. These iterators are
  // invalidated by mutation of the data structure.
  UnsortedIterator unsorted_begin() const { return elements_.begin(); }
  UnsortedIterator unsorted_end() const { return elements_.begin() + size(); }

  // Accessor for comparator template argument.
  Cmp *comparator() { return &cmp_; }

  // This removes all elements.  If Extract() or ExtractUnsorted() have been
  // called, this will put it back in an empty but useable state.
  void Reset();

private:
  template <typename U>
  void PushInternal(U &&v, T *dropped); // NOLINT(build/c++11)

  // elements_ can be in one of two states:
  //   elements_.size() <= limit_:  elements_ is an unsorted vector of elements
  //      pushed so far.
  //   elements_.size() > limit_:  The last element of elements_ is unused;
  //      the other elements of elements_ are an stl heap whose size is exactly
  //      limit_.  In this case elements_.size() is exactly one greater than
  //      limit_, but don't use "elements_.size() == limit_ + 1" to check for
  //      that because you'll get a false positive if limit_ == uint64_t(-1).
  std::vector<T> elements_;
  uint64_t limit_; // Maximum number of elements to find
  Cmp cmp_;        // Greater-than comparison function
  State state_ = UNORDERED;
};

// ----------------------------------------------------------------------
// Implementations of non-inline functions

template <class T, class Cmp>
template <typename U>
void TopNHeap<T, Cmp>::PushInternal(U &&v, T *dropped)
{ // NOLINT(build/c++11)
  if (limit_ == 0)
  {
    if (dropped)
      *dropped = std::forward<U>(v); // NOLINT(build/c++11)
    return;
  }
  if (state_ != HEAP_SORTED)
  {
    elements_.push_back(std::forward<U>(v)); // NOLINT(build/c++11)
    if (state_ == UNORDERED || cmp_(elements_.back(), elements_.front()))
    {
      // Easy case: we just pushed the new element back
    }
    else
    {
      // To maintain the BOTTOM_KNOWN state, we need to make sure that
      // the element at position 0 is always the smallest. So we put
      // the new element at position 0 and push the original bottom
      // element in the back.
      // Warning: this code is subtle.
      using std::swap;
      swap(elements_.front(), elements_.back());
    }
    if (elements_.size() == limit_ + 1)
    {
      // Transition from unsorted vector to a heap.
      std::make_heap(elements_.begin(), elements_.end(), cmp_);
      if (dropped)
        *dropped = std::move(elements_.front());
      std::pop_heap(elements_.begin(), elements_.end(), cmp_);
      state_ = HEAP_SORTED;
    }
  }
  else
  {
    // Only insert the new element if it is greater than the least element.
    if (cmp_(v, elements_.front()))
    {
      elements_.back() = std::forward<U>(v); // NOLINT(build/c++11)
      std::push_heap(elements_.begin(), elements_.end(), cmp_);
      if (dropped)
        *dropped = std::move(elements_.front());
      std::pop_heap(elements_.begin(), elements_.end(), cmp_);
    }
    else
    {
      if (dropped)
        *dropped = std::forward<U>(v); // NOLINT(build/c++11)
    }
  }
}

template <class T, class Cmp>
const T &TopNHeap<T, Cmp>::peek_bottom()
{
  //CHECK(!empty());
  if (state_ == UNORDERED)
  {
    // We need to do a linear scan to find out the bottom element
    int32_t min_candidate = 0;
    for (uint64_t i = 1; i < elements_.size(); ++i)
    {
      if (cmp_(elements_[min_candidate], elements_[i]))
      {
        min_candidate = i;
      }
    }
    // By swapping the element at position 0 and the minimal
    // element, we transition to the BOTTOM_KNOWN state
    if (min_candidate != 0)
    {
      using std::swap;
      swap(elements_[0], elements_[min_candidate]);
    }
    state_ = BOTTOM_KNOWN;
  }
  return elements_.front();
}

template <class T, class Cmp>
std::vector<T> *TopNHeap<T, Cmp>::Extract()
{
  auto out = new std::vector<T>;
  out->swap(elements_);
  if (state_ != HEAP_SORTED)
  {
    std::sort(out->begin(), out->end(), cmp_);
  }
  else
  {
    out->pop_back();
    std::sort_heap(out->begin(), out->end(), cmp_);
  }
  return out;
}

template <class T, class Cmp>
std::vector<T> *TopNHeap<T, Cmp>::ExtractUnsorted()
{
  auto out = new std::vector<T>;
  out->swap(elements_);
  if (state_ == HEAP_SORTED)
  {
    // Remove the limit_+1'th element.
    out->pop_back();
  }
  return out;
}

template <class T, class Cmp>
std::vector<T> *TopNHeap<T, Cmp>::ExtractNondestructive() const
{
  auto out = new std::vector<T>;
  ExtractNondestructive(out);
  return out;
}

template <class T, class Cmp>
void TopNHeap<T, Cmp>::ExtractNondestructive(std::vector<T> *output) const
{
  //CHECK(output);
  *output = elements_;
  if (state_ != HEAP_SORTED)
  {
    std::sort(output->begin(), output->end(), cmp_);
  }
  else
  {
    output->pop_back();
    std::sort_heap(output->begin(), output->end(), cmp_);
  }
}

template <class T, class Cmp>
std::vector<T> *TopNHeap<T, Cmp>::ExtractUnsortedNondestructive() const
{
  auto elements = new std::vector<T>;
  ExtractUnsortedNondestructive(elements);
  return elements;
}

template <class T, class Cmp>
void TopNHeap<T, Cmp>::ExtractUnsortedNondestructive(std::vector<T> *output) const
{
  //CHECK(output);
  *output = elements_;
  if (state_ == HEAP_SORTED)
  {
    // Remove the limit_+1'th element.
    output->pop_back();
  }
}

template <class T, class Cmp>
void TopNHeap<T, Cmp>::Reset()
{
  elements_.clear();
  state_ = UNORDERED;
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SMALL_CONTAINER_H_