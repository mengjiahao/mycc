
#ifndef MYCC_UTIL_TIME_UTIL_H_
#define MYCC_UTIL_TIME_UTIL_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <functional>
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

// ----------------------
// timespec manipulations
// ----------------------

inline timespec make_timespec(int64_t sec, int64_t nsec)
{
  timespec tm;
  tm.tv_sec = sec;
  tm.tv_nsec = nsec;
  return tm;
}

inline timespec max_timespec()
{
  return make_timespec(-1, 0);
}

inline bool is_max_timespec(timespec &tm)
{
  return (-1 == tm.tv_sec);
}

// Let tm->tv_nsec be in [0, 1,000,000,000) if it's not.
inline void timespec_normalize(timespec *tm)
{
  if (tm->tv_nsec >= 1000000000L)
  {
    const int64_t added_sec = tm->tv_nsec / 1000000000L;
    tm->tv_sec += added_sec;
    tm->tv_nsec -= added_sec * 1000000000L;
  }
  else if (tm->tv_nsec < 0)
  {
    const int64_t sub_sec = (tm->tv_nsec - 999999999L) / 1000000000L;
    tm->tv_sec += sub_sec;
    tm->tv_nsec -= sub_sec * 1000000000L;
  }
}

// Add timespec |span| into timespec |*tm|.
inline void timespec_add(timespec *tm, const timespec &span)
{
  tm->tv_sec += span.tv_sec;
  tm->tv_nsec += span.tv_nsec;
  timespec_normalize(tm);
}

// Minus timespec |span| from timespec |*tm|.
// tm->tv_nsec will be inside [0, 1,000,000,000)
inline void timespec_minus(timespec *tm, const timespec &span)
{
  tm->tv_sec -= span.tv_sec;
  tm->tv_nsec -= span.tv_nsec;
  timespec_normalize(tm);
}

// ------------------------------------------------------------------
// Get the timespec after specified duration from |start_time|
// ------------------------------------------------------------------
inline timespec nanoseconds_from(timespec start_time, int64_t nanoseconds)
{
  start_time.tv_nsec += nanoseconds;
  timespec_normalize(&start_time);
  return start_time;
}

inline timespec microseconds_from(timespec start_time, int64_t microseconds)
{
  return nanoseconds_from(start_time, microseconds * 1000L);
}

inline timespec milliseconds_from(timespec start_time, int64_t milliseconds)
{
  return nanoseconds_from(start_time, milliseconds * 1000000L);
}

inline timespec seconds_from(timespec start_time, int64_t seconds)
{
  return nanoseconds_from(start_time, seconds * 1000000000L);
}

// --------------------------------------------------------------------
// Get the timespec after specified duration from now (CLOCK_REALTIME)
// --------------------------------------------------------------------
inline timespec nanoseconds_from_now(int64_t nanoseconds)
{
  timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  return nanoseconds_from(time, nanoseconds);
}

inline timespec microseconds_from_now(int64_t microseconds)
{
  return nanoseconds_from_now(microseconds * 1000L);
}

inline timespec milliseconds_from_now(int64_t milliseconds)
{
  return nanoseconds_from_now(milliseconds * 1000000L);
}

inline timespec seconds_from_now(int64_t seconds)
{
  return nanoseconds_from_now(seconds * 1000000000L);
}

inline timespec timespec_from_now(const timespec &span)
{
  timespec time;
  clock_gettime(CLOCK_REALTIME, &time);
  timespec_add(&time, span);
  return time;
}

// ---------------------------------------------------------------------
// Convert timespec to and from a single integer.
// For conversions between timespec and timeval, use TIMEVAL_TO_TIMESPEC
// and TIMESPEC_TO_TIMEVAL defined in <sys/time.h>
// ---------------------------------------------------------------------1
inline int64_t timespec_to_nanoseconds(const timespec &ts)
{
  return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

inline int64_t timespec_to_microseconds(const timespec &ts)
{
  return timespec_to_nanoseconds(ts) / 1000L;
}

inline int64_t timespec_to_milliseconds(const timespec &ts)
{
  return timespec_to_nanoseconds(ts) / 1000000L;
}

inline int64_t timespec_to_seconds(const timespec &ts)
{
  return timespec_to_nanoseconds(ts) / 1000000000L;
}

inline timespec nanoseconds_to_timespec(int64_t ns)
{
  timespec ts;
  ts.tv_sec = ns / 1000000000L;
  ts.tv_nsec = ns - ts.tv_sec * 1000000000L;
  return ts;
}

inline timespec microseconds_to_timespec(int64_t us)
{
  return nanoseconds_to_timespec(us * 1000L);
}

inline timespec milliseconds_to_timespec(int64_t ms)
{
  return nanoseconds_to_timespec(ms * 1000000L);
}

inline timespec seconds_to_timespec(int64_t s)
{
  return nanoseconds_to_timespec(s * 1000000000L);
}

// ---------------------------------------------------------------------
// Convert timeval to and from a single integer.
// For conversions between timespec and timeval, use TIMEVAL_TO_TIMESPEC
// and TIMESPEC_TO_TIMEVAL defined in <sys/time.h>
// ---------------------------------------------------------------------

inline timeval make_timeval(int64_t sec, int64_t usec)
{
  timeval tv;
  tv.tv_sec = sec;
  tv.tv_usec = usec;
  return tv;
}

inline int64_t timeval_to_microseconds(const timeval &tv)
{
  return tv.tv_sec * 1000000L + tv.tv_usec;
}

inline int64_t timeval_to_milliseconds(const timeval &tv)
{
  return timeval_to_microseconds(tv) / 1000L;
}

inline int64_t timeval_to_seconds(const timeval &tv)
{
  return timeval_to_microseconds(tv) / 1000000L;
}

inline timeval microseconds_to_timeval(int64_t us)
{
  timeval tv;
  tv.tv_sec = us / 1000000L;
  tv.tv_usec = us - tv.tv_sec * 1000000L;
  return tv;
}

inline timeval milliseconds_to_timeval(int64_t ms)
{
  return microseconds_to_timeval(ms * 1000L);
}

inline timeval seconds_to_timeval(int64_t s)
{
  return microseconds_to_timeval(s * 1000000L);
}

// --------------------------------------------------------------------
// Get elapse since the Epoch.
// No gettimeofday_ns() because resolution of timeval is microseconds.
// Cost ~40ns on 2.6.32_1-12-0-0, Intel(R) Xeon(R) CPU E5620 @ 2.40GHz
// --------------------------------------------------------------------
inline int64_t gettimeofday_us()
{
  timeval now;
  gettimeofday(&now, nullptr);
  return now.tv_sec * 1000000L + now.tv_usec;
}

inline int64_t gettimeofday_ms()
{
  return gettimeofday_us() / 1000L;
}

inline int64_t gettimeofday_s()
{
  return gettimeofday_us() / 1000000L;
}

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

void SleepForNanos(uint32_t nanos);
void SleepForMicros(uint32_t micros);
void SleepForSecs(uint32_t secs);

int64_t NowRealtimeNanos();
int64_t NowRealtimeMicros();
int64_t NowRealtimeSecs();
int64_t NowMonotonicNanos();
int64_t NowMonotonicMicros();
int64_t NowMonotonicSecs();
int64_t NowSystimeMicros();
int64_t NowSystimeMillis();
int64_t NowSystimeSecs();

int32_t CurrentSystimeBuf(char *buf, int64_t size);
string CurrentSystimeString();
string FormatSystime(int64_t micros);
string CurrentTimestampString();
string FormatTimestamp(int64_t seconds);
int64_t ParseTimestamp(const string &time_str);

void MakeTimeoutUs(struct timespec *pts, int64_t micros);
void MakeTimeoutMs(struct timespec *pts, int64_t millis);
bool IsInHourRange(int64_t min_hour, int64_t max_hour);

uint32_t GetTimedUt(int32_t h, int32_t m, int32_t s, time_t cur_time);
uint32_t GetDateFromTime(time_t time);
uint32_t GetMonthFromTime(time_t nowtime);

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

////////////////////// c++11 chrno /////////////////////////////

/*!
 * \brief return time in seconds
 */
inline double NowChronoSecs(void)
{
  return std::chrono::duration<double>(
             std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_TIME_UTIL_H_