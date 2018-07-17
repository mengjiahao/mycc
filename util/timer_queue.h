
#ifndef MYCC_UTIL_TIMER_QUEUE_H_
#define MYCC_UTIL_TIMER_QUEUE_H_

#include <assert.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

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

#endif // MYCC_UTIL_TIMER_QUEUE_H_