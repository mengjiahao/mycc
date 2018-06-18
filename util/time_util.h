
#ifndef MYCC_UTIL_TIME_UTIL_H_
#define MYCC_UTIL_TIME_UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <atomic>
#include <chrono>
#include <type_traits>
#include "error_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

/**
 * @class ChronoDuration
 * @brief the default Duration is of precision nanoseconds (1e-9 seconds).
 */
using ChronoDuration = std::chrono::nanoseconds;
using ChronoNanos = std::chrono::nanoseconds;
using ChronoMicros = std::chrono::microseconds;
using ChronoMillis = std::chrono::milliseconds;
using ChronoSeconds = std::chrono::seconds;
using ChronoMinutes = std::chrono::minutes;
using ChronoHours = std::chrono::hours;

/**
 * @class ChronoTimestamp
 * @brief the default timestamp uses std::chrono::system_clock. The
 * system_clock is a system-wide realtime clock.
 */
using ChronoTimestamp = std::chrono::time_point<std::chrono::system_clock, ChronoDuration>;
using ChronoTimePoint = std::chrono::system_clock::time_point;

static_assert(std::is_same<int64_t, ChronoDuration::rep>::value,
              "The underlying type of the microseconds should be int64.");

/**
 * @brief converts the input duration (nanos) to a 64 bit integer, with
 * the unit specified by PrecisionDuration.
 * @param duration the input duration that needs to be converted to integer.
 * @return an integer representing the duration in the specified unit.
 */
template <typename PrecisionDuration>
int64_t ChronoAsInt64(const ChronoDuration &duration)
{
  return std::chrono::duration_cast<PrecisionDuration>(duration).count();
}

/**
 * @brief converts the input timestamp (nanos) to a 64 bit integer, with
 * the unit specified by PrecisionDuration.
 * @param timestamp the input timestamp that needs to be converted to integer.
 * @return an integer representing the timestamp in the specified unit.
 */
template <typename PrecisionDuration>
int64_t ChronoAsInt64(const ChronoTimestamp &timestamp)
{
  return ChronoAsInt64<PrecisionDuration>(timestamp.time_since_epoch());
}

/**
 * @brief converts the input duration (nanos) to a double in seconds.
 * The original precision will be preserved.
 * @param duration the input duration that needs to be converted.
 * @return a doule in seconds.
 */
inline double ChronoToSecond(const ChronoDuration &duration)
{
  return static_cast<double>(ChronoAsInt64<ChronoNanos>(duration)) * 1e-9;
}

/**
 * @brief converts the input timestamp (nanos) to a double in seconds.
 * The original precision will be preserved.
 * @param timestamp the input timestamp that needs to be converted.
 * @return a doule representing the same timestamp in seconds.
 */
inline double ChronoToSecond(const ChronoTimestamp &timestamp)
{
  return static_cast<double>(ChronoAsInt64<ChronoNanos>(timestamp.time_since_epoch())) *
         1e-9;
}

/**
 * @brief converts the integer-represented timestamp to \class Timestamp.
 * @return a Timestamp object.
 */
template <typename PrecisionDuration>
inline ChronoTimestamp ChronoFromInt64(int64_t timestamp_value)
{
  return ChronoTimestamp(
      std::chrono::duration_cast<ChronoNanos>(PrecisionDuration(timestamp_value)));
}

/**
 * @brief converts the double to \class Timestamp. The input double has
 * a unit of seconds.
 * @return a Timestamp object.
*/
inline ChronoTimestamp ChronoFrom(double timestamp_value)
{
  int64_t nanos_value = static_cast<int64_t>(timestamp_value * 1e9);
  return ChronoFromInt64<ChronoNanos>(nanos_value);
}

/**
 * @class ChronoClock
 * @brief a singleton clock that can be used to get the current current
 * timestamp. The source can be either system clock or a mock clock.
 * Mock clock is for testing purpose mainly. The mock clock related
 * methods are not thread-safe.
 */
class ChronoClock
{
public:
  static constexpr int64_t PRECISION =
      std::chrono::system_clock::duration::period::den /
      std::chrono::system_clock::duration::period::num;

  /// PRECISION >= 1000000 means the precision is at least 1us.
  static_assert(PRECISION >= 1000000,
                "The precision of the system clock should be at least 1 "
                "microsecond.");

  // The clock mode can either be a system clock time, a user mocked time (for
  // test only) or read from ROS.
  enum ClockMode
  {
    SYSTEM = 0,
    MOCK = 1,
    ROS = 2,
  };

  /**
   * @brief gets the current timestamp.
   * @return a Timestamp object representing the current time.
   */
  static ChronoTimestamp Now()
  {
    switch (Mode())
    {
    case ClockMode::SYSTEM:
      return SystemNow();
    case ClockMode::MOCK:
      return instance()->mock_now_;
    default:
      PANIC("Unsupported clock mode: %d", Mode());
    }
    return ChronoTimestamp::max();
  }

  /**
   * @brief gets the current time in second.
   * @return the current time in second.
   */
  static double NowInSeconds() { return ChronoToSecond(ChronoClock::Now()); }

  /**
   * @brief Set the behavior of the \class Clock.
   * @param The new clock mode to be set.
   */
  static void SetMode(ClockMode mode) { instance()->mode_ = mode; }

  /**
   * @brief Gets the current clock mode.
   * @return The current clock mode.
   */
  static ClockMode Mode() { return instance()->mode_; }

  /**
   * @brief This is for mock clock mode only. It will set the timestamp
   * for the mock clock.
   */
  static void SetNow(const ChronoDuration &duration)
  {
    ChronoClock *clock = instance();
    if (clock->mode_ != ClockMode::MOCK)
    {
      PANIC("Cannot set now when clock mode is not MOCK!");
    }
    clock->mock_now_ = ChronoTimestamp(duration);
  }

private:
  /**
   * @brief constructs the \class Clock instance
   * @param mode the desired clock mode
   */
  explicit ChronoClock(ClockMode mode) : mode_(mode), mock_now_(ChronoTimestamp())
  {
    //ros::Time::init();
  }

  /**
   * @brief Returns the current timestamp based on the system clock.
   * @return the current timestamp based on the system clock.
   */
  static ChronoTimestamp SystemNow()
  {
    return std::chrono::time_point_cast<ChronoDuration>(
        std::chrono::system_clock::now());
  }

  /// NOTE: Unless mode_ and mock_now_ are guarded by a
  /// lock or become atomic, having multiple threads calling mock
  /// clock related functions are STRICTLY PROHIBITED.

  /// Indicates whether it is in the system clock mode or the mock
  /// clock mode or the ROS time mode.
  ClockMode mode_;

  /// Stores the currently set timestamp, which serves mock clock
  /// queries.
  ChronoTimestamp mock_now_;

  /// Explicitly disable default and move/copy constructors.
  DECLARE_SINGLETON(ChronoClock);
};

class ChronoTimer
{
public:
  ChronoTimer(){};

  // no-thread safe.
  void start();

  // return the elapsed time,
  // also output msg and time in glog.
  // automatically start a new timer.
  // no-thread safe.
  uint64_t end(const string &msg);

private:
  // in ms.
  ChronoTimePoint start_time_;
  ChronoTimePoint end_time_;

  DISALLOW_COPY_AND_ASSIGN(ChronoTimer);
};

class ChronoTimerWrapper
{
public:
  explicit ChronoTimerWrapper(const string &msg) : msg_(msg)
  {
    timer_.start();
  }

  ~ChronoTimerWrapper()
  {
    timer_.end(msg_);
  }

private:
  ChronoTimer timer_;
  string msg_;

  DISALLOW_COPY_AND_ASSIGN(ChronoTimerWrapper);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TIME_UTIL_H_