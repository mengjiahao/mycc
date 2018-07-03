
#ifndef MYCC_UTIL_TIMER_H_
#define MYCC_UTIL_TIMER_H_

#include <list>
#include <memory>
#include <unordered_map>
#include "time_util.h"

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TIMER_H_