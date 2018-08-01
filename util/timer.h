
#ifndef MYCC_UTIL_TIMER_H_
#define MYCC_UTIL_TIMER_H_

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <list>
#include <memory>
#include <queue>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#include "time_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class Timer
{
public:
  Timer() : start_(0) {}
  void start();
  uint64_t elapsedNanos(bool reset = false);

private:
  uint64_t start_;
};

class ChronoTimer
{
public:
  ChronoTimer(){};

  // no-thread safe.
  void start();

  // return the elapsed time,
  // automatically start a new timer.
  // no-thread safe.
  uint64_t end();

private:
  // in ms.
  ChronoTimePoint start_time_;
  ChronoTimePoint end_time_;

  DISALLOW_COPY_AND_ASSIGN(ChronoTimer);
};

class ChronoTimerWrapper
{
public:
  explicit ChronoTimerWrapper()
  {
    timer_.start();
  }

  ~ChronoTimerWrapper()
  {
    timer_.end();
  }

private:
  ChronoTimer timer_;

  DISALLOW_COPY_AND_ASSIGN(ChronoTimerWrapper);
};

/**
 * @brief A simple timer object for measuring time.
 *
 * This is a minimal class around a std::chrono::high_resolution_clock that
 * serves as a utility class for testing code.
 */
class ChronoSimpleTimer
{
public:
  typedef std::chrono::high_resolution_clock clock;
  typedef std::chrono::nanoseconds ns;
  ChronoSimpleTimer() { start(); }
  /**
   * @brief Starts a timer.
   */
  inline void start() { start_time_ = clock::now(); }
  inline float nanos()
  {
    return std::chrono::duration_cast<ns>(clock::now() - start_time_).count();
  }
  /**
   * @brief Returns the elapsed time in milliseconds.
   */
  inline float millis() { return nanos() / 1000000.f; }
  /**
   * @brief Returns the elapsed time in microseconds.
   */
  inline float micros() { return nanos() / 1000.f; }
  /**
   * @brief Returns the elapsed time in seconds.
   */
  inline float secs() { return nanos() / 1000000000.f; }

protected:
  std::chrono::time_point<clock> start_time_;
  DISALLOW_COPY_AND_ASSIGN(ChronoSimpleTimer);
};

// start O(1gn)，timeout O(1)，stop O(1gn)
class SequenceTimer : public Timer
{
public:
  typedef std::function<int32_t()> TimeoutCallback;

  SequenceTimer();
  virtual ~SequenceTimer(){};

  virtual int64_t StartTimer(uint32_t timeout_ms, const TimeoutCallback &cb);
  virtual int32_t StopTimer(int64_t timer_id);
  virtual int32_t Update();
  virtual const char *GetLastError() const
  {
    return m_last_error;
  }
  virtual int64_t GetTimerNum()
  {
    return m_id_2_timer.size();
  }

private:
  struct TimerItem
  {
    TimerItem()
    {
      stoped = false;
      id = -1;
      timeout = 0;
    }

    bool stoped;
    int64_t id;
    int64_t timeout;
    TimeoutCallback cb;
  };

private:
  int64_t m_timer_seqid;
  // map<timeout_ms, list<TimerItem> >
  std::unordered_map<uint32_t, std::list<std::shared_ptr<TimerItem>>> m_timers;
  // map<timer_seqid, TimerItem>
  std::unordered_map<int64_t, std::shared_ptr<TimerItem>> m_id_2_timer;
  char m_last_error[256];
};

// Borrowed from
// http://www.crazygaze.com/blog/2016/03/24/portable-c-timer-queue/
// Timer Queue:
// Allows execution of handlers at a specified time in the future
// Guarantees:
//  - All handlers are executed ONCE, even if cancelled (aborted parameter will
// be set to true)
//      - If TimerQueue is destroyed, it will cancel all handlers.
//  - Handlers are ALWAYS executed in the Timer Queue worker thread.
//  - Handlers execution order is NOT guaranteed

class TimerQueue
{
public:
  using Clock = std::chrono::steady_clock;

  TimerQueue() : m_th(&TimerQueue::run, this) {}

  ~TimerQueue()
  {
    cancelAll();
    // Abusing the timer queue to trigger the shutdown.
    add(0, [this](bool) {
      m_finish = true;
      return std::make_pair(false, 0);
    });
    m_th.join();
  }

  // Adds a new timer
  // \return
  //  Returns the ID of the new timer. You can use this ID to cancel the
  // timer
  uint64_t add(int64_t milliseconds,
               std::function<std::pair<bool, int64_t>(bool)> handler)
  {
    WorkItem item;
    Clock::time_point tp = Clock::now();
    item.end = tp + std::chrono::milliseconds(milliseconds);
    item.period = milliseconds;
    item.handler = std::move(handler);

    std::unique_lock<std::mutex> lk(m_mtx);
    uint64_t id = ++m_idcounter;
    item.id = id;
    m_items.push(std::move(item));

    // Something changed, so wake up timer thread
    m_checkWork.notify_one();
    return id;
  }

  // Cancels the specified timer
  // \return
  //  1 if the timer was cancelled.
  //  0 if you were too late to cancel (or the timer ID was never valid to
  // start with)
  uint64_t cancel(uint64_t id)
  {
    // Instead of removing the item from the container (thus breaking the
    // heap integrity), we set the item as having no handler, and put
    // that handler on a new item at the top for immediate execution
    // The timer thread will then ignore the original item, since it has no
    // handler.
    std::unique_lock<std::mutex> lk(m_mtx);
    for (auto &&item : m_items.getContainer())
    {
      if (item.id == id && item.handler)
      {
        WorkItem newItem;
        // Zero time, so it stays at the top for immediate execution
        newItem.end = Clock::time_point();
        newItem.id = 0; // Means it is a canceled item
        // Move the handler from item to newitem (thus clearing item)
        newItem.handler = std::move(item.handler);
        m_items.push(std::move(newItem));

        // Something changed, so wake up timer thread
        m_checkWork.notify_one();
        return 1;
      }
    }
    return 0;
  }

  // Cancels all timers
  // \return
  //  The number of timers cancelled
  uint64_t cancelAll()
  {
    // Setting all "end" to 0 (for immediate execution) is ok,
    // since it maintains the heap integrity
    std::unique_lock<std::mutex> lk(m_mtx);
    m_cancel = true;
    for (auto &&item : m_items.getContainer())
    {
      if (item.id && item.handler)
      {
        item.end = Clock::time_point();
        item.id = 0;
      }
    }
    auto ret = m_items.size();

    m_checkWork.notify_one();
    return ret;
  }

private:
  TimerQueue(const TimerQueue &) = delete;
  TimerQueue &operator=(const TimerQueue &) = delete;

  void run()
  {
    std::unique_lock<std::mutex> lk(m_mtx);
    while (!m_finish)
    {
      auto end = calcWaitTime_lock();
      if (end.first)
      {
        // Timers found, so wait until it expires (or something else
        // changes)
        m_checkWork.wait_until(lk, end.second);
      }
      else
      {
        // No timers exist, so wait forever until something changes
        m_checkWork.wait(lk);
      }

      // Check and execute as much work as possible, such as, all expired
      // timers
      checkWork(&lk);
    }

    // If we are shutting down, we should not have any items left,
    // since the shutdown cancels all items
    assert(m_items.size() == 0);
  }

  std::pair<bool, Clock::time_point> calcWaitTime_lock()
  {
    while (m_items.size())
    {
      if (m_items.top().handler)
      {
        // Item present, so return the new wait time
        return std::make_pair(true, m_items.top().end);
      }
      else
      {
        // Discard empty handlers (they were cancelled)
        m_items.pop();
      }
    }

    // No items found, so return no wait time (causes the thread to wait
    // indefinitely)
    return std::make_pair(false, Clock::time_point());
  }

  void checkWork(std::unique_lock<std::mutex> *lk)
  {
    while (m_items.size() && m_items.top().end <= Clock::now())
    {
      WorkItem item(m_items.top());
      m_items.pop();

      if (item.handler)
      {
        (*lk).unlock();
        auto reschedule_pair = item.handler(item.id == 0);
        (*lk).lock();
        if (!m_cancel && reschedule_pair.first)
        {
          int64_t new_period = (reschedule_pair.second == -1)
                                   ? item.period
                                   : reschedule_pair.second;

          item.period = new_period;
          item.end = Clock::now() + std::chrono::milliseconds(new_period);
          m_items.push(std::move(item));
        }
      }
    }
  }

  bool m_finish = false;
  bool m_cancel = false;
  uint64_t m_idcounter = 0;
  std::condition_variable m_checkWork;

  struct WorkItem
  {
    Clock::time_point end;
    int64_t period;
    uint64_t id; // id==0 means it was cancelled
    std::function<std::pair<bool, int64_t>(bool)> handler;
    bool operator>(const WorkItem &other) const { return end > other.end; }
  };

  std::mutex m_mtx;
  // Inheriting from priority_queue, so we can access the internal container
  class Queue : public std::priority_queue<WorkItem, std::vector<WorkItem>,
                                           std::greater<WorkItem>>
  {
  public:
    std::vector<WorkItem> &getContainer() { return this->c; }
  } m_items;
  std::thread m_th;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TIMER_H_