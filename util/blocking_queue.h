#ifndef MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_
#define MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_

#include <atomic>
#include <condition_variable>
#include <deque>
#include <list>
#include <mutex>
#include <queue>
#include <type_traits>
#include <vector>
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

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
  void Push(E &&e, int priority = 0);

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
  void PushFront(E &&e, int priority = 0);
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
    int priority;
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
void ConcurrentBlockingQueue<T, type>::Push(E &&e, int priority)
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
void ConcurrentBlockingQueue<T, type>::PushFront(E &&e, int priority)
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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_