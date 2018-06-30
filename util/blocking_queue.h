#ifndef MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_
#define MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_

#include <atomic>
#include <condition_variable>
#include <list>
#include <mutex>
#include <queue>
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

  size_t Size() const
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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BLOCKING_QUEUE_UTIL_H_