#ifndef MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_
#define MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_

#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>
#include "atomic_util.h"
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

template <typename T>
class MPMCBlockingQueue
{
  std::queue<T> queue;
  std::mutex mutex;
  std::condition_variable condition;

  MPMCBlockingQueue(const MPMCBlockingQueue &) = delete;
  MPMCBlockingQueue &operator=(const MPMCBlockingQueue &) = delete;

public:
  // Produce a new value and possibily notify one of the threads calling `wait_and_consume`
  void produce(T const &value)
  {
    std::lock_guard<std::mutex> lock(mutex);
    queue.push(value);
    condition.notify_one();
  }

  // Blocking operation if queue is empty
  void wait_and_consume(T &popped_value)
  {
    std::unique_lock<std::mutex> lock(mutex);
    while (queue.empty())
      condition.wait(lock);
    popped_value = queue.front();
    queue.pop();
  }

  // Permits polling threads to do something else if the queue is empty
  bool try_consume(T &popped_value)
  {
    std::lock_guard<std::mutex> lock(mutex);
    if (queue.empty())
      return false;
    popped_value = queue.front();
    queue.pop();
    return true;
  }

  bool empty() const
  {
    std::unique_lock<std::mutex> lock(mutex);
    return queue.empty();
  }

  std::size_t size() const { return queue.size(); }
};

template <typename T, int Size = 5>
class TCLoopQueue
{
public:
  typedef std::vector<T> queue_type;

  TCLoopQueue(uint32_t iSize = Size)
  {
    assert(iSize < 1000000);
    _iBegin = 0;
    _iEnd = 0;
    _iCapacitySub = iSize;
    _iCapacity = iSize + 1;
    _p = (T *)malloc(_iCapacity * sizeof(T));
    //_p= new T[_iCapacity];
  }
  ~TCLoopQueue()
  {
    free(_p);
    //delete _p;
  }

  bool push_back(const T &t, bool &bEmpty, uint32_t &iBegin, uint32_t &iEnd)
  {
    bEmpty = false;
    //uint32_t iEnd = _iEnd;
    iEnd = _iEnd;
    iBegin = _iBegin;
    if ((iEnd > _iBegin && iEnd - _iBegin < 2) ||
        (_iBegin > iEnd && _iBegin - iEnd > (_iCapacity - 2)))
    {
      return false;
    }
    else
    {
      memcpy(_p + _iBegin, &t, sizeof(T));
      //*(_p+_iBegin) = t;

      if (_iEnd == _iBegin)
        bEmpty = true;

      if (_iBegin == _iCapacitySub)
        _iBegin = 0;
      else
        _iBegin++;

      if (!bEmpty && 1 == size())
        bEmpty = true;

      return true;
    }
  }

  bool push_back(const T &t, bool &bEmpty)
  {
    bEmpty = false;
    uint32_t iEnd = _iEnd;
    if ((iEnd > _iBegin && iEnd - _iBegin < 2) ||
        (_iBegin > iEnd && _iBegin - iEnd > (_iCapacity - 2)))
    {
      return false;
    }
    else
    {
      memcpy(_p + _iBegin, &t, sizeof(T));
      //*(_p+_iBegin) = t;
      if (_iEnd == _iBegin)
        bEmpty = true;

      if (_iBegin == _iCapacitySub)
        _iBegin = 0;
      else
        _iBegin++;

      if (!bEmpty && 1 == size())
        bEmpty = true;

      return true;
    }
  }

  bool push_back(const T &t)
  {
    bool bEmpty;
    return push_back(t, bEmpty);
  }

  bool push_back(const queue_type &vt)
  {
    uint32_t iEnd = _iEnd;
    if (vt.size() > (_iCapacity - 1) ||
        (iEnd > _iBegin && (iEnd - _iBegin) < (vt.size() + 1)) ||
        (_iBegin > iEnd && (_iBegin - iEnd) > (_iCapacity - vt.size() - 1)))
    {
      return false;
    }
    else
    {
      for (uint32_t i = 0; i < vt.size(); i++)
      {
        memcpy(_p + _iBegin, &vt[i], sizeof(T));
        //*(_p+_iBegin) = vt[i];
        if (_iBegin == _iCapacitySub)
          _iBegin = 0;
        else
          _iBegin++;
      }
      return true;
    }
  }

  bool pop_front(T &t)
  {
    if (_iEnd == _iBegin)
    {
      return false;
    }
    memcpy(&t, _p + _iEnd, sizeof(T));
    //t = *(_p+_iEnd);

    if (_iEnd == _iCapacitySub)
      _iEnd = 0;
    else
      _iEnd++;
    return true;
  }

  bool pop_front()
  {
    if (_iEnd == _iBegin)
    {
      return false;
    }
    if (_iEnd == _iCapacitySub)
      _iEnd = 0;
    else
      _iEnd++;
    return true;
  }

  bool get_front(T &t)
  {
    if (_iEnd == _iBegin)
    {
      return false;
    }
    memcpy(&t, _p + _iEnd, sizeof(T));
    //t = *(_p+_iEnd);
    return true;
  }

  bool empty()
  {
    if (_iEnd == _iBegin)
    {
      return true;
    }
    return false;
  }

  uint32_t size()
  {
    uint32_t iBegin = _iBegin;
    uint32_t iEnd = _iEnd;
    if (iBegin < iEnd)
      return iBegin + _iCapacity - iEnd;
    return iBegin - iEnd;
  }

  uint32_t getCapacity()
  {
    return _iCapacity;
  }

private:
  T *_p;
  uint32_t _iCapacity;
  uint32_t _iCapacitySub;
  uint32_t _iBegin;
  uint32_t _iEnd;
};

// A thread-unsafe bounded queue(ring buffer). It can push/pop from both
// sides and is more handy than thread-safe queues in single thread. Use
// boost::lockfree::spsc_queue or boost::lockfree::queue in multi-threaded
// scenarios.

// [Create a on-stack small queue]
//   char storage[64];
//   butil::BoundedQueue<int32_t> q(storage, sizeof(storage), butil::NOT_OWN_STORAGE);
//   q.push(1);
//   q.push(2);
//   ...

// [Initialize a class-member queue]
//   class Foo {
//     ...
//     BoundQueue<int32_t> _queue;
//   };
//   int32_t Foo::init() {
//     BoundedQueue<int32_t> tmp(capacity);
//     if (!tmp.initialized()) {
//       LOG(ERROR) << "Fail to create _queue";
//       return -1;
//     }
//     tmp.swap(_queue);
//   }

template <typename T>
class BoundedQueue
{
public:
  enum StorageOwnership
  {
    OWNS_STORAGE,
    NOT_OWN_STORAGE
  };

  // You have to pass the memory for storing items at creation.
  // The queue contains at most memsize/sizeof(T) items.
  BoundedQueue(void *mem, uint64_t memsize, StorageOwnership ownership)
      : _count(0), _cap(memsize / sizeof(T)), _start(0), _ownership(ownership), _items(mem)
  {
    assert(_items != nullptr);
  };

  // Construct a queue with the given capacity.
  // The malloc() may fail sliently, call initialized() to test validity
  // of the queue.
  explicit BoundedQueue(uint64_t capacity)
      : _count(0), _cap(capacity), _start(0), _ownership(OWNS_STORAGE), _items(malloc(capacity * sizeof(T)))
  {
    assert(_items != nullptr);
  };

  BoundedQueue()
      : _count(0), _cap(0), _start(0), _ownership(NOT_OWN_STORAGE), _items(NULL){};

  ~BoundedQueue()
  {
    clear();
    if (_ownership == OWNS_STORAGE)
    {
      free(_items);
      _items = NULL;
    }
  }

  // Push |item| into bottom side of this queue.
  // Returns true on success, false if queue is full.
  bool push(const T &item)
  {
    if (_count < _cap)
    {
      new ((T *)_items + _mod(_start + _count, _cap)) T(item);
      ++_count;
      return true;
    }
    return false;
  }

  // Push |item| into bottom side of this queue. If the queue is full,
  // pop topmost item first.
  void elim_push(const T &item)
  {
    if (_count < _cap)
    {
      new ((T *)_items + _mod(_start + _count, _cap)) T(item);
      ++_count;
    }
    else
    {
      ((T *)_items)[_start] = item;
      _start = _mod(_start + 1, _cap);
    }
  }

  // Push a default-constructed item into bottom side of this queue
  // Returns address of the item inside this queue
  T *push()
  {
    if (_count < _cap)
    {
      return new ((T *)_items + _mod(_start + _count++, _cap)) T();
    }
    return NULL;
  }

  // Push |item| into top side of this queue
  // Returns true on success, false if queue is full.
  bool push_top(const T &item)
  {
    if (_count < _cap)
    {
      _start = _start ? (_start - 1) : (_cap - 1);
      ++_count;
      new ((T *)_items + _start) T(item);
      return true;
    }
    return false;
  }

  // Push a default-constructed item into top side of this queue
  // Returns address of the item inside this queue
  T *push_top()
  {
    if (_count < _cap)
    {
      _start = _start ? (_start - 1) : (_cap - 1);
      ++_count;
      return new ((T *)_items + _start) T();
    }
    return NULL;
  }

  // Pop top-most item from this queue
  // Returns true on success, false if queue is empty
  bool pop()
  {
    if (_count)
    {
      --_count;
      ((T *)_items + _start)->~T();
      _start = _mod(_start + 1, _cap);
      return true;
    }
    return false;
  }

  // Pop top-most item from this queue and copy into |item|.
  // Returns true on success, false if queue is empty
  bool pop(T *item)
  {
    if (_count)
    {
      --_count;
      *item = ((T *)_items)[_start];
      ((T *)_items)[_start].~T();
      _start = _mod(_start + 1, _cap);
      return true;
    }
    return false;
  }

  // Pop bottom-most item from this queue
  // Returns true on success, false if queue is empty
  bool pop_bottom()
  {
    if (_count)
    {
      --_count;
      ((T *)_items + _start + _count)->~T();
      return true;
    }
    return false;
  }

  // Pop bottom-most item from this queue and copy into |item|.
  // Returns true on success, false if queue is empty
  bool pop_bottom(T *item)
  {
    if (_count)
    {
      --_count;
      *item = ((T *)_items)[_start + _count];
      ((T *)_items)[_start + _count].~T();
      return true;
    }
    return false;
  }

  // Pop all items
  void clear()
  {
    for (uint32_t i = 0; i < _count; ++i)
    {
      ((T *)_items + _mod(_start + i, _cap))->~T();
    }
    _count = 0;
    _start = 0;
  }

  // Get address of top-most item, NULL if queue is empty
  T *top()
  {
    return _count ? ((T *)_items + _start) : NULL;
  }
  const T *top() const
  {
    return _count ? ((const T *)_items + _start) : NULL;
  }

  // Randomly access item from top side.
  // top(0) == top(), top(size()-1) == bottom()
  // Returns NULL if |index| is out of range.
  T *top(uint64_t index)
  {
    if (index < _count)
    {
      return (T *)_items + _mod(_start + index, _cap);
    }
    return NULL; // including _count == 0
  }
  const T *top(uint64_t index) const
  {
    if (index < _count)
    {
      return (const T *)_items + _mod(_start + index, _cap);
    }
    return NULL; // including _count == 0
  }

  // Get address of bottom-most item, NULL if queue is empty
  T *bottom()
  {
    return _count ? ((T *)_items + _mod(_start + _count - 1, _cap)) : NULL;
  }
  const T *bottom() const
  {
    return _count ? ((const T *)_items + _mod(_start + _count - 1, _cap)) : NULL;
  }

  // Randomly access item from bottom side.
  // bottom(0) == bottom(), bottom(size()-1) == top()
  // Returns NULL if |index| is out of range.
  T *bottom(uint64_t index)
  {
    if (index < _count)
    {
      return (T *)_items + _mod(_start + _count - index - 1, _cap);
    }
    return NULL; // including _count == 0
  }
  const T *bottom(uint64_t index) const
  {
    if (index < _count)
    {
      return (const T *)_items + _mod(_start + _count - index - 1, _cap);
    }
    return NULL; // including _count == 0
  }

  bool empty() const { return !_count; }
  bool full() const { return _cap == _count; }

  // Number of items
  uint64_t size() const { return _count; }

  // Maximum number of items that can be in this queue
  uint64_t capacity() const { return _cap; }

  // Maximum value of capacity()
  uint64_t max_capacity() const { return (1UL << (sizeof(_cap) * 8)) - 1; }

  // True if the queue was constructed successfully.
  bool initialized() const { return _items != NULL; }

  // Swap internal fields with another queue.
  void swap(BoundedQueue &rhs)
  {
    std::swap(_count, rhs._count);
    std::swap(_cap, rhs._cap);
    std::swap(_start, rhs._start);
    std::swap(_ownership, rhs._ownership);
    std::swap(_items, rhs._items);
  }

private:
  // Since the space is possibly not owned, we disable copying.
  DISALLOW_COPY_AND_ASSIGN(BoundedQueue);

  // This is faster than % in this queue because most |off| are smaller
  // than |cap|. This is probably not true in other place, be careful
  // before you use this trick.
  static uint32_t _mod(uint32_t off, uint32_t cap)
  {
    while (off >= cap)
    {
      off -= cap;
    }
    return off;
  }

  uint32_t _count;
  uint32_t _cap;
  uint32_t _start;
  StorageOwnership _ownership;
  void *_items;
};

template <class T>
class QueueRing
{
  struct QueueBucket
  {
    Mutex mu;
    CondVar cond;
    typename std::list<T> entries;

    QueueBucket() : mu(), cond(&mu) {}
    QueueBucket(const QueueBucket &rhs) : mu(), cond(&mu)
    {
      entries = rhs.entries;
    }

    void enqueue(const T &entry)
    {
      MutexLock lock(&mu);
      if (entries.empty())
      {
        cond.signal();
      }
      entries.push_back(entry);
    }

    void dequeue(T *entry)
    {
      MutexLock lock(&mu);
      if (entries.empty())
      {
        cond.wait();
      };
      assert(!entries.empty());
      *entry = entries.front();
      entries.pop_front();
    };
  };

  std::vector<QueueBucket> buckets;
  int32_t num_buckets;

  std::atomic<int64_t> cur_read_bucket = {0};
  std::atomic<int64_t> cur_write_bucket = {0};

public:
  QueueRing(int32_t n) : buckets(n), num_buckets(n)
  {
  }

  void enqueue(const T &entry)
  {
    buckets[++cur_write_bucket % num_buckets].enqueue(entry);
  };

  void dequeue(T *entry)
  {
    buckets[++cur_read_bucket % num_buckets].dequeue(entry);
  }
};

template <class T>
class BlockChannel
{
public:
  explicit BlockChannel() : eof_(false) {}

  void SendEof()
  {
    std::lock_guard<std::mutex> lk(lock_);
    eof_ = true;
    cv_.notify_all();
  }

  bool Eof()
  {
    std::lock_guard<std::mutex> lk(lock_);
    return buffer_.empty() && eof_;
  }

  uint64_t Size() const
  {
    std::lock_guard<std::mutex> lk(lock_);
    return buffer_.size();
  }

  // writes elem to the queue
  void Write(T &&elem)
  {
    std::unique_lock<std::mutex> lk(lock_);
    buffer_.emplace(std::forward<T>(elem));
    cv_.notify_one();
  }

  /// Moves a dequeued element onto elem, blocking until an element
  /// is available.
  // returns false if EOF
  bool Read(T &elem)
  {
    std::unique_lock<std::mutex> lk(lock_);
    cv_.wait(lk, [&] { return eof_ || !buffer_.empty(); });
    if (eof_ && buffer_.empty())
    {
      return false;
    }
    elem = std::move(buffer_.front());
    buffer_.pop();
    cv_.notify_one();
    return true;
  }

private:
  std::condition_variable cv_;
  std::mutex lock_;
  std::queue<T> buffer_;
  bool eof_;

  DISALLOW_COPY_AND_ASSIGN(BlockChannel);
};

/**
 * A thread-safe queue that automatically grows but never shrinks.
 * Dequeue a empty queue will block current thread. Enqueue an element
 * will wake up another thread that blocked by dequeue method.
 *
 * For example.
 * @code{.cpp}
 *
 * paddle::SimpleBlockingQueue<int32_t> q;
 * END_OF_JOB=-1
 * void thread1() {
 *   while (true) {
 *     auto job = q.dequeue();
 *     if (job == END_OF_JOB) {
 *       break;
 *     }
 *     processJob(job);
 *   }
 * }
 *
 * void thread2() {
 *   while (true) {
 *      auto job = getJob();
 *      q.enqueue(job);
 *      if (job == END_OF_JOB) {
 *        break;
 *      }
 *   }
 * }
 *
 * @endcode
 */
template <class T>
class SimpleBlockingQueue
{
public:
  /**
   * @brief Construct Function. Default capacity of SimpleBlockingQueue is zero.
   */
  SimpleBlockingQueue() : numElements_(0) {}

  ~SimpleBlockingQueue() {}

  /**
   * @brief enqueue an element into SimpleBlockingQueue.
   * @param[in] el The enqueue element.
   * @note This method is thread-safe, and will wake up another blocked thread.
   */
  void enqueue(const T &el)
  {
    std::unique_lock<std::mutex> lock(queueLock_);
    elements_.emplace_back(el);
    numElements_++;

    queueCV_.notify_all();
  }

  /**
   * @brief enqueue an element into SimpleBlockingQueue.
   * @param[in] el The enqueue element. rvalue reference .
   * @note This method is thread-safe, and will wake up another blocked thread.
   */
  void enqueue(T &&el)
  {
    std::unique_lock<std::mutex> lock(queueLock_);
    elements_.emplace_back(std::move(el));
    numElements_++;

    queueCV_.notify_all();
  }

  /**
   * Dequeue from a queue and return a element.
   * @note this method will be blocked until not empty.
   */
  T dequeue()
  {
    std::unique_lock<std::mutex> lock(queueLock_);
    queueCV_.wait(lock, [this]() { return numElements_ != 0; });
    T el;

    using std::swap;
    // Becuase of the previous statement, the right swap() can be found
    // via argument-dependent lookup (ADL).
    swap(elements_.front(), el);

    elements_.pop_front();
    numElements_--;
    if (numElements_ == 0)
    {
      queueCV_.notify_all();
    }
    return el;
  }

  /**
   * Return size of queue.
   *
   * @note This method is not thread safe. Obviously this number
   * can change by the time you actually look at it.
   */
  inline int32_t size() const { return numElements_; }

  /**
   * @brief is empty or not.
   * @return true if empty.
   * @note This method is not thread safe.
   */
  inline bool empty() const { return numElements_ == 0; }

  /**
   * @brief wait util queue is empty
   */
  void waitEmpty()
  {
    std::unique_lock<std::mutex> lock(queueLock_);
    queueCV_.wait(lock, [this]() { return numElements_ == 0; });
  }

  /**
   * @brief wait queue is not empty at most for some seconds.
   * @param seconds wait time limit.
   * @return true if queue is not empty. false if timeout.
   */
  bool waitNotEmptyFor(int32_t seconds)
  {
    std::unique_lock<std::mutex> lock(queueLock_);
    return queueCV_.wait_for(lock, std::chrono::seconds(seconds), [this] {
      return numElements_ != 0;
    });
  }

private:
  std::deque<T> elements_;
  int32_t numElements_;
  std::mutex queueLock_;
  std::condition_variable queueCV_;
};

/*
 * A thread-safe circular queue that
 * automatically blocking calling thread if capacity reached.
 *
 * For example.
 * @code{.cpp}
 *
 * paddle::BlockingQueue<int32_t> q(capacity);
 * END_OF_JOB=-1
 * void thread1() {
 *   while (true) {
 *     auto job = q.dequeue();
 *     if (job == END_OF_JOB) {
 *       break;
 *     }
 *     processJob(job);
 *   }
 * }
 *
 * void thread2() {
 *   while (true) {
 *      auto job = getJob();
 *      q.enqueue(job); //Block until q.size() < capacity .
 *      if (job == END_OF_JOB) {
 *        break;
 *      }
 *   }
 * }
 */
template <typename T>
class SimpleCircularQueue
{
public:
  /**
   * @brief Construct Function.
   * @param[in] capacity the max numer of elements the queue can have.
   */
  explicit SimpleCircularQueue(uint64_t capacity) : capacity_(capacity) {}

  /**
   * @brief enqueue an element into SimpleBlockingQueue.
   * @param[in] x The enqueue element, pass by reference .
   * @note This method is thread-safe, and will wake up another thread
   * who was blocked because of the queue is empty.
   * @note If it's size() >= capacity before enqueue,
   * this method will block and wait until size() < capacity.
   */
  void enqueue(const T &x)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    notFull_.wait(lock, [&] { return queue_.size() < capacity_; });
    queue_.push_back(x);
    notEmpty_.notify_one();
  }

  /**
   * Dequeue from a queue and return a element.
   * @note this method will be blocked until not empty.
   * @note this method will wake up another thread who was blocked because
   * of the queue is full.
   */
  T dequeue()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    notEmpty_.wait(lock, [&] { return !queue_.empty(); });

    T front(queue_.front());
    queue_.pop_front();
    notFull_.notify_one();
    return front;
  }

  /**
   * Return size of queue.
   *
   * @note This method is thread safe.
   * The size of the queue won't change until the method return.
   */
  uint64_t size()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return queue_.size();
  }

  /**
   * @brief is empty or not.
   * @return true if empty.
   * @note This method is thread safe.
   */
  uint64_t empty()
  {
    std::lock_guard<std::mutex> guard(mutex_);
    return queue_.empty();
  }

private:
  std::mutex mutex_;
  std::condition_variable notEmpty_;
  std::condition_variable notFull_;
  std::deque<T> queue_;
  uint64_t capacity_;
};

/*! \brief type of concurrent queue */
enum class ConcurrentQueueType
{
  /*! \brief FIFO queue */
  kFIFO,
  /*! \brief queue with priority */
  kPriority
};

/*!
 * \brief Cocurrent blocking queue.
 */
template <typename T,
          ConcurrentQueueType type = ConcurrentQueueType::kFIFO>
class ConcurrentBlockingQueue
{
public:
  ConcurrentBlockingQueue();
  ~ConcurrentBlockingQueue() = default;
  /*!
   * \brief Push element to the end of the queue.
   * \param e Element to push into.
   * \param priority the priority of the element, only used for priority queue.
   *            The higher the priority is, the better.
   * \tparam E the element type
   *
   * It will copy or move the element into the queue, depending on the type of
   * the parameter.
   */
  template <typename E>
  void Push(E &&e, int32_t priority = 0);

  /*!
   * \brief Push element to the front of the queue. Only works for FIFO queue.
   *        For priority queue it is the same as Push.
   * \param e Element to push into.
   * \param priority the priority of the element, only used for priority queue.
   *            The higher the priority is, the better.
   * \tparam E the element type
   *
   * It will copy or move the element into the queue, depending on the type of
   * the parameter.
   */
  template <typename E>
  void PushFront(E &&e, int32_t priority = 0);
  /*!
   * \brief Pop element from the queue.
   * \param rv Element popped.
   * \return On false, the queue is exiting.
   *
   * The element will be copied or moved into the object passed in.
   */
  bool Pop(T *rv);
  /*!
   * \brief Signal the queue for destruction.
   *
   * After calling this method, all blocking pop call to the queue will return
   * false.
   */
  void SignalForKill();
  /*!
   * \brief Get the size of the queue.
   * \return The size of the queue.
   */
  uint64_t Size();

private:
  struct Entry
  {
    T data;
    int32_t priority;
    inline bool operator<(const Entry &b) const
    {
      return priority < b.priority;
    }
  };

  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> exit_now_;
  int32_t nwait_consumer_;
  // a priority queue
  std::vector<Entry> priority_queue_;
  // a FIFO queue
  std::deque<T> fifo_queue_;
  /*!
   * \brief Disable copy and move.
   */
  DISALLOW_COPY_AND_ASSIGN(ConcurrentBlockingQueue);
};

template <typename T, ConcurrentQueueType type>
ConcurrentBlockingQueue<T, type>::ConcurrentBlockingQueue()
    : exit_now_{false}, nwait_consumer_{0} {}

template <typename T, ConcurrentQueueType type>
template <typename E>
void ConcurrentBlockingQueue<T, type>::Push(E &&e, int32_t priority)
{
  static_assert(std::is_same<typename std::remove_cv<
                                 typename std::remove_reference<E>::type>::type,
                             T>::value,
                "Types must match.");
  bool notify;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (type == ConcurrentQueueType::kFIFO)
    {
      fifo_queue_.emplace_back(std::forward<E>(e));
      notify = nwait_consumer_ != 0;
    }
    else
    {
      Entry entry;
      entry.data = std::move(e);
      entry.priority = priority;
      priority_queue_.push_back(std::move(entry));
      std::push_heap(priority_queue_.begin(), priority_queue_.end());
      notify = nwait_consumer_ != 0;
    }
  }
  if (notify)
    cv_.notify_one();
}

template <typename T, ConcurrentQueueType type>
template <typename E>
void ConcurrentBlockingQueue<T, type>::PushFront(E &&e, int32_t priority)
{
  static_assert(std::is_same<typename std::remove_cv<
                                 typename std::remove_reference<E>::type>::type,
                             T>::value,
                "Types must match.");
  bool notify;
  {
    std::lock_guard<std::mutex> lock{mutex_};
    if (type == ConcurrentQueueType::kFIFO)
    {
      fifo_queue_.emplace_front(std::forward<E>(e));
      notify = nwait_consumer_ != 0;
    }
    else
    {
      Entry entry;
      entry.data = std::move(e);
      entry.priority = priority;
      priority_queue_.push_back(std::move(entry));
      std::push_heap(priority_queue_.begin(), priority_queue_.end());
      notify = nwait_consumer_ != 0;
    }
  }
  if (notify)
    cv_.notify_one();
}

template <typename T, ConcurrentQueueType type>
bool ConcurrentBlockingQueue<T, type>::Pop(T *rv)
{
  std::unique_lock<std::mutex> lock{mutex_};
  if (type == ConcurrentQueueType::kFIFO)
  {
    ++nwait_consumer_;
    cv_.wait(lock, [this] {
      return !fifo_queue_.empty() || exit_now_.load();
    });
    --nwait_consumer_;
    if (!exit_now_.load())
    {
      *rv = std::move(fifo_queue_.front());
      fifo_queue_.pop_front();
      return true;
    }
    else
    {
      return false;
    }
  }
  else
  {
    ++nwait_consumer_;
    cv_.wait(lock, [this] {
      return !priority_queue_.empty() || exit_now_.load();
    });
    --nwait_consumer_;
    if (!exit_now_.load())
    {
      std::pop_heap(priority_queue_.begin(), priority_queue_.end());
      *rv = std::move(priority_queue_.back().data);
      priority_queue_.pop_back();
      return true;
    }
    else
    {
      return false;
    }
  }
}

template <typename T, ConcurrentQueueType type>
void ConcurrentBlockingQueue<T, type>::SignalForKill()
{
  {
    std::lock_guard<std::mutex> lock{mutex_};
    exit_now_.store(true);
  }
  cv_.notify_all();
}

template <typename T, ConcurrentQueueType type>
uint64_t ConcurrentBlockingQueue<T, type>::Size()
{
  std::lock_guard<std::mutex> lock{mutex_};
  if (type == ConcurrentQueueType::kFIFO)
  {
    return fifo_queue_.size();
  }
  else
  {
    return priority_queue_.size();
  }
}

template <typename T>
class BlockingQueue
{
public:
  typedef T ValueType;
  typedef std::deque<T> UnderlyContainerType;

  BlockingQueue(uint64_t max_elements = static_cast<uint64_t>(-1))
      : m_max_elements(max_elements),
        m_cond_not_empty(&m_mutex), m_cond_not_full(&m_mutex)
  {
    assert(m_max_elements > 0);
  }

  /// @brief push element in to front of queue
  /// @param value to be pushed
  /// @note if queue is full, block and wait for non-full
  void PushFront(const T &value)
  {
    {
      MutexLock locker(m_mutex);
      while (UnlockedIsFull())
      {
        m_cond_not_full.wait();
      }
      m_queue.push_front(value);
    }
    m_cond_not_empty.signal();
  }

  /// @brief try push element in to front of queue
  /// @param value to be pushed
  /// @note if queue is full, return false
  bool TryPushFront(const T &value)
  {
    {
      MutexLock locker(&m_mutex);
      if (UnlockedIsFull())
        return false;
      m_queue.push_front(value);
    }
    m_cond_not_empty.signal();
    return true;
  }

  /// @brief push element in to back of queue
  /// @param value to be pushed
  /// @note if queue is full, block and wait for non-full
  void PushBack(const T &value)
  {
    {
      MutexLock locker(&m_mutex);
      while (UnlockedIsFull())
      {
        m_cond_not_full.wait();
      }
      m_queue.push_back(value);
    }
    m_cond_not_empty.signal();
  }

  /// @brief Try push element in to back of queue
  /// @param value to be pushed
  /// @note if queue is full, return false
  bool TryPushBack(const T &value)
  {
    {
      MutexLock locker(&m_mutex);
      if (UnlockedIsFull())
        return false;
      m_queue.push_back(value);
    }
    m_cond_not_empty.signal();
    return true;
  }

  /// @brief popup from front of queue.
  /// @param value to hold the result
  /// @note if queue is empty, block and wait for non-empty
  void PopFront(T *value)
  {
    {
      MutexLock locker(&m_mutex);
      while (m_queue.empty())
      {
        m_cond_not_empty.wait();
      }
      *value = m_queue.front();
      m_queue.pop_front();
    }
    m_cond_not_full.signal();
  }

  /// @brief Try popup from front of queue.
  /// @param value to hold the result
  /// @note if queue is empty, return false
  bool TryPopFront(T *value)
  {
    {
      MutexLock locker(&m_mutex);
      if (m_queue.empty())
        return false;
      *value = m_queue.front();
      m_queue.pop_front();
    }
    m_cond_not_full.signal();
    return true;
  }

  /// @brief popup from back of queue
  /// @param value to hold the result
  /// @note if queue is empty, block and wait for non-empty
  void PopBack(T *value)
  {
    {
      MutexLock locker(&m_mutex);
      while (m_queue.empty())
      {
        m_cond_not_empty.wait();
      }
      *value = m_queue.back();
      m_queue.pop_back();
    }
    m_cond_not_full.signal();
  }

  /// @brief Try popup from back of queue
  /// @param value to hold the result
  /// @note if queue is empty, return false
  bool TryPopBack(T *value)
  {
    {
      MutexLock locker(&m_mutex);
      if (m_queue.empty())
        return false;
      *value = m_queue.back();
      m_queue.pop_back();
    }
    m_cond_not_full.signal();
    return true;
  }

  /// @brief push front with time out
  /// @param value to be pushed
  /// @param timeout_in_ms timeout, in milliseconds
  /// @return whether pushed
  bool TimedPushFront(const T &value, int64_t timeout_in_ms)
  {
    bool success = false;
    {
      MutexLock locker(&m_mutex);

      if (UnlockedIsFull())
        m_cond_not_full.waitFor(timeout_in_ms);

      if (!UnlockedIsFull())
      {
        m_queue.push_front(value);
        success = true;
      }
    }

    if (success)
      m_cond_not_empty.signal();

    return success;
  }

  /// @brief push back with time out
  /// @param value to be pushed
  /// @param timeout_in_ms timeout, in milliseconds
  /// @return whether pushed
  bool TimedPushBack(const T &value, int64_t timeout_in_ms)
  {
    bool success = false;
    {
      MutexLock locker(&m_mutex);
      if (UnlockedIsFull())
        m_cond_not_full.waitFor(timeout_in_ms);

      if (!UnlockedIsFull())
      {
        m_queue.push_back(value);
        success = true;
      }
    }
    if (success)
      m_cond_not_empty.signal();
    return success;
  }

  /// @brief pop front with time out
  /// @param value to hold the result
  /// @param timeout_in_ms timeout, in milliseconds
  /// @return whether poped
  bool TimedPopFront(T *value, int64_t timeout_in_ms)
  {
    bool success = false;
    {
      MutexLock locker(&m_mutex);

      if (m_queue.empty())
        m_cond_not_empty.waitFor(timeout_in_ms);

      if (!m_queue.empty())
      {
        *value = m_queue.front();
        m_queue.pop_front();
        success = true;
      }
    }

    if (success)
      m_cond_not_full.signal();

    return success;
  }

  /// @brief pop back with time out
  /// @param value to hold the result
  /// @param timeout_in_ms timeout, in milliseconds
  /// @return whether poped
  bool TimedPopBack(T *value, int64_t timeout_in_ms)
  {
    bool success = false;
    {
      MutexLock locker(&m_mutex);

      if (m_queue.empty())
        m_cond_not_empty.waitFor(timeout_in_ms);

      if (!m_queue.empty())
      {
        *value = m_queue.back();
        m_queue.pop_back();
        success = true;
      }
    }

    if (success)
      m_cond_not_full.signal();

    return success;
  }

  /// @brief pop all from the queue and guarantee the output queue contains at
  /// least one element.
  /// @param values to hold the results, it would be cleared firstly by the
  /// function.
  /// @note if queue is empty, block and wait for non-empty.
  void PopAll(UnderlyContainerType *values)
  {
    values->clear();

    {
      MutexLock locker(&m_mutex);
      while (m_queue.empty())
        m_cond_not_empty.wait();
      values->swap(m_queue);
    }

    m_cond_not_full.broadcast();
  }

  /// @brief Try pop all from the queue and guarantee the output queue
  /// contains at least one element.
  /// @param values to hold the results, it would be cleared firstly by the
  /// function.
  /// @note if queue is empty, return false
  bool TryPopAll(UnderlyContainerType *values)
  {
    values->clear();

    {
      MutexLock locker(&m_mutex);
      if (m_queue.empty())
        return false;
      values->swap(m_queue);
    }
    m_cond_not_full.broadcast();
    return true;
  }

  /// @brief pop all from the queue with time out.
  /// @param values to hold the results, it would be cleared firstly by the
  /// function.
  /// @param timeout_in_ms timeout, in milliseconds.
  /// @return whether poped.
  bool TimedPopAll(UnderlyContainerType *values, int64_t timeout_in_ms)
  {
    bool success = false;
    values->clear();

    {
      MutexLock locker(&m_mutex);
      if (m_queue.empty())
        m_cond_not_empty.waitFor(timeout_in_ms);

      if (!m_queue.empty())
      {
        values->swap(m_queue);
        m_cond_not_full.broadcast();
        success = true;
      }
    }

    if (success)
      m_cond_not_full.signal();

    return success;
  }

  /// @brief number of elements in the queue.
  /// @return number of elements in the queue.
  uint64_t Size()
  {
    MutexLock locker(&m_mutex);
    return m_queue.size();
  }

  /// @brief whether the queue is empty
  /// @return whether empty
  bool IsEmpty()
  {
    MutexLock locker(&m_mutex);
    return m_queue.empty();
  }

  /// @brief whether the queue is full
  /// @return whether full
  bool IsFull()
  {
    MutexLock locker(&m_mutex);
    return UnlockedIsFull();
  }

  /// @brief clear the queue
  void Clear()
  {
    MutexLock locker(&m_mutex);
    m_queue.clear();
    m_cond_not_full.broadcast();
  }

private:
  bool UnlockedIsFull() const
  {
    return m_queue.size() >= m_max_elements;
  }

private:
  uint64_t m_max_elements;
  UnderlyContainerType m_queue;

  Mutex m_mutex;
  CondVar m_cond_not_empty;
  CondVar m_cond_not_full;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_