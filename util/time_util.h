
#ifndef MYCC_UTIL_TIME_UTIL_H_
#define MYCC_UTIL_TIME_UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <type_traits>
#include "error_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// The range of timestamp values we support.
static const int64_t kMinTime = -62135596800LL; // 0001-01-01T00:00:00
static const int64_t kMaxTime = 253402300799LL; // 9999-12-31T23:59:59

// The min/max Timestamp/Duration values we support.
//
// For "0001-01-01T00:00:00Z".
static const int64_t kTimestampMinSeconds = -62135596800LL;
// For "9999-12-31T23:59:59.999999999Z".
static const int64_t kTimestampMaxSeconds = 253402300799LL;
static const int64_t kDurationMinSeconds = -315576000000LL;
static const int64_t kDurationMaxSeconds = 315576000000LL;

static const int64_t kSecondsPerMinute = 60;
static const int64_t kSecondsPerHour = 3600;
static const int64_t kSecondsPerDay = kSecondsPerHour * 24;
static const int64_t kSecondsPer400Years =
    kSecondsPerDay * (400 * 365 + 400 / 4 - 3);
// Seconds from 0001-01-01T00:00:00 to 1970-01-01T:00:00:00
static const int64_t kSecondsFromEraToEpoch = 62135596800LL;

static const int64_t kMillisPerSecond = 1000;
static const int64_t kMicrosPerMillisecond = 1000;
static const int64_t kMicrosPerSecond = 1000000;
static const int64_t kMicrosPerMinute = kMicrosPerSecond * 60;
static const int64_t kMicrosPerHour = kMicrosPerMinute * 60;
static const int64_t kMicrosPerDay = kMicrosPerHour * 24;
static const int64_t kMicrosPerWeek = kMicrosPerDay * 7;
static const int64_t kNanosPerMicrosecond = 1000;
static const int64_t kNanosPerMillisecond = 1000000;
static const int64_t kNanosPerSecond = 1000000000;

// unix timestamp(1970.01.01) is different from gps timestamp(1980.01.06)
static const int UNIX_GPS_DIFF = 315964782;
// unix timestamp(2016.12.31 23:59:59(60) UTC/GMT)
static const int LEAP_SECOND_TIMESTAMP = 1483228799;

extern const char *kTimestampStdFormat;
extern const char *kTimeFormat;

struct DateTime
{
  int32_t year;
  int32_t month;
  int32_t day;
  int32_t hour;
  int32_t minute;
  int32_t second;
};

inline int64_t SecsToNanos(int64_t secs)
{
  return secs * 1000000000;
}

inline int64_t MillisToNanos(int64_t secs)
{
  return secs * 1000000;
}

inline int64_t MicrosToNanos(int64_t secs)
{
  return secs * 1000;
}

inline int64_t NanosToSecs(int64_t secs)
{
  return secs / 1000000000;
}

inline int64_t NanosToMicros(int64_t secs)
{
  return secs / 1000;
}

inline int64_t NanosToMillis(int64_t secs)
{
  return secs / 1000000;
}

// Converts a timestamp (seconds elapsed since 1970-01-01T00:00:00, could be
// negative to represent time before 1970-01-01) to DateTime. Returns false
// if the timestamp is not in the range between 0001-01-01T00:00:00 and
// 9999-12-31T23:59:59.
bool SecondsToDateTime(int64_t seconds, DateTime *time);
// Converts DateTime to a timestamp (seconds since 1970-01-01T00:00:00).
// Returns false if the DateTime is not valid or is not in the valid range.
bool DateTimeToSeconds(const DateTime &time, int64_t *seconds);

void SleepForMicros(uint32_t micros);
void SleepForSecs(uint32_t secs);

int64_t NowSystimeMicros();
int64_t NowMonotonicNanos();
int64_t NowMonotonicMicros();
int64_t NowMonotonicSecs();

string CurrentSystimeString();
string FormatSystime(int64_t micros);
string CurrentTimestampString();
string FormatTimestamp(int64_t seconds);
int64_t ParseTimestamp(const string &time_str);

void MakeTimeoutUs(struct timespec *pts, int64_t micros);
void MakeTimeoutMs(struct timespec *pts, int64_t millis);
bool IsInHourRange(int64_t min_hour, int64_t max_hour);

// @brief: UNIX timestamp to GPS timestamp, in seconds.
inline double Unix2gps(double unix_time)
{
  double gps_time = unix_time - UNIX_GPS_DIFF;
  if (unix_time < LEAP_SECOND_TIMESTAMP)
  {
    gps_time -= 1.0;
  }
  return gps_time;
}

// @brief: GPS timestamp to UNIX timestamp, in seconds.
inline double Gps2unix(double gps_time)
{
  double unix_time = gps_time + UNIX_GPS_DIFF;
  if (unix_time + 1 < LEAP_SECOND_TIMESTAMP)
  {
    unix_time += 1.0;
  }
  return unix_time;
}

class Timer
{
public:
  Timer() : start_(0) {}
  void start();
  uint64_t elapsedNanos(bool reset = false);

private:
  uint64_t start_;
};

////////////////////// c++11 chrno /////////////////////////////

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
    MOCK,
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
  explicit ChronoClock(ClockMode mode)
      : mode_(mode), mock_now_(ChronoTimestamp())
  {
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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TIME_UTIL_H_