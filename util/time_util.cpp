
#include "time_util.h"
#include <assert.h>
#include "math_util.h"

namespace mycc
{
namespace util
{

const char *kTimestampStdFormat = "%Y-%m-%d:%H:%M:%S";
const char *kTimeFormat = "%04d-%02d-%02d:%02d:%02d:%02d.%06d";

// Count the seconds from the given year (start at Jan 1, 00:00) to 100 years
// after.
int64_t SecondsPer100Years(int32_t year)
{
  if (year % 400 == 0 || year % 400 > 300)
  {
    return kSecondsPerDay * (100 * 365 + 100 / 4);
  }
  else
  {
    return kSecondsPerDay * (100 * 365 + 100 / 4 - 1);
  }
}

// Count the seconds from the given year (start at Jan 1, 00:00) to 4 years
// after.
int64_t SecondsPer4Years(int32_t year)
{
  if ((year % 100 == 0 || year % 100 > 96) &&
      !(year % 400 == 0 || year % 400 > 396))
  {
    // No leap years.
    return kSecondsPerDay * (4 * 365);
  }
  else
  {
    // One leap years.
    return kSecondsPerDay * (4 * 365 + 1);
  }
}

bool IsLeapYear(int32_t year)
{
  return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

int64_t SecondsPerYear(int32_t year)
{
  return kSecondsPerDay * (IsLeapYear(year) ? 366 : 365);
}

static const int32_t kDaysInMonth[13] = {
    0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

int64_t SecondsPerMonth(int32_t month, bool leap)
{
  if (month == 2 && leap)
  {
    return kSecondsPerDay * (kDaysInMonth[month] + 1);
  }
  return kSecondsPerDay * kDaysInMonth[month];
}

static const int32_t kDaysSinceJan[13] = {
    0,
    0,
    31,
    59,
    90,
    120,
    151,
    181,
    212,
    243,
    273,
    304,
    334,
};

bool ValidateDateTime(const DateTime &time)
{
  if (time.year < 1 || time.year > 9999 ||
      time.month < 1 || time.month > 12 ||
      time.day < 1 || time.day > 31 ||
      time.hour < 0 || time.hour > 23 ||
      time.minute < 0 || time.minute > 59 ||
      time.second < 0 || time.second > 59)
  {
    return false;
  }
  if (time.month == 2 && IsLeapYear(time.year))
  {
    return time.month <= kDaysInMonth[time.month] + 1;
  }
  else
  {
    return time.month <= kDaysInMonth[time.month];
  }
}

// Count the number of seconds elapsed from 0001-01-01T00:00:00 to the given
// time.
int64_t SecondsSinceCommonEra(const DateTime &time)
{
  int64_t result = 0;
  // Years should be between 1 and 9999.
  assert(time.year >= 1 && time.year <= 9999);
  int32_t year = 1;
  if ((time.year - year) >= 400)
  {
    int32_t count_400years = (time.year - year) / 400;
    result += kSecondsPer400Years * count_400years;
    year += count_400years * 400;
  }
  while ((time.year - year) >= 100)
  {
    result += SecondsPer100Years(year);
    year += 100;
  }
  while ((time.year - year) >= 4)
  {
    result += SecondsPer4Years(year);
    year += 4;
  }
  while (time.year > year)
  {
    result += SecondsPerYear(year);
    ++year;
  }
  // Months should be between 1 and 12.
  assert(time.month >= 1 && time.month <= 12);
  int32_t month = time.month;
  result += kSecondsPerDay * kDaysSinceJan[month];
  if (month > 2 && IsLeapYear(year))
  {
    result += kSecondsPerDay;
  }
  assert(time.day >= 1 &&
         time.day <= (month == 2 && IsLeapYear(year)
                          ? kDaysInMonth[month] + 1
                          : kDaysInMonth[month]));
  result += kSecondsPerDay * (time.day - 1);
  result += kSecondsPerHour * time.hour +
            kSecondsPerMinute * time.minute +
            time.second;
  return result;
}

bool SecondsToDateTime(int64_t seconds, DateTime *time)
{
  if (seconds < kMinTime || seconds > kMaxTime)
  {
    return false;
  }
  // It's easier to calcuate the DateTime starting from 0001-01-01T00:00:00
  seconds = seconds + kSecondsFromEraToEpoch;
  int32_t year = 1;
  if (seconds >= kSecondsPer400Years)
  {
    int32_t count_400years = seconds / kSecondsPer400Years;
    year += 400 * count_400years;
    seconds %= kSecondsPer400Years;
  }
  while (seconds >= SecondsPer100Years(year))
  {
    seconds -= SecondsPer100Years(year);
    year += 100;
  }
  while (seconds >= SecondsPer4Years(year))
  {
    seconds -= SecondsPer4Years(year);
    year += 4;
  }
  while (seconds >= SecondsPerYear(year))
  {
    seconds -= SecondsPerYear(year);
    year += 1;
  }
  bool leap = IsLeapYear(year);
  int32_t month = 1;
  while (seconds >= SecondsPerMonth(month, leap))
  {
    seconds -= SecondsPerMonth(month, leap);
    ++month;
  }
  int32_t day = 1 + seconds / kSecondsPerDay;
  seconds %= kSecondsPerDay;
  int32_t hour = seconds / kSecondsPerHour;
  seconds %= kSecondsPerHour;
  int32_t minute = seconds / kSecondsPerMinute;
  seconds %= kSecondsPerMinute;
  time->year = year;
  time->month = month;
  time->day = day;
  time->hour = hour;
  time->minute = minute;
  time->second = static_cast<int32_t>(seconds);
  return true;
}

bool DateTimeToSeconds(const DateTime &time, int64_t *seconds)
{
  if (!ValidateDateTime(time))
  {
    return false;
  }
  *seconds = SecondsSinceCommonEra(time) - kSecondsFromEraToEpoch;
  return true;
}

void SleepForNanos(uint32_t nanos)
{
  struct timespec sleep_time;
  sleep_time.tv_sec = static_cast<time_t>(nanos / 1000000000);
  sleep_time.tv_nsec = static_cast<long>(nanos % 1000000000);
  nanosleep(&sleep_time, NULL);
}

void SleepForMicros(uint32_t micros)
{
  usleep(micros);
}

void SleepForSecs(uint32_t secs)
{
  sleep(secs);
}

int64_t NowRealtimeNanos()
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + static_cast<int64_t>(ts.tv_nsec);
}

int64_t NowRealtimeMicros()
{
  return NowRealtimeNanos() / 1000;
}

int64_t NowRealtimeSecs()
{
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec);
}

int64_t NowMonotonicNanos()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

int64_t NowMonotonicMicros()
{
  return NowMonotonicNanos() / 1000;
}

int64_t NowMonotonicSecs()
{
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec);
}

int64_t NowSystimeMicros()
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  int64_t micros = tv.tv_sec * 1000000 + tv.tv_usec;
  return micros;
}

int64_t NowSystimeMillis()
{
  int64_t us = NowSystimeMicros();
  return us / 1000;
}

int64_t NowSystimeSecs()
{
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  return tv.tv_sec;
}

string CurrentSystimeString()
{
  char buf[64] = {0};
  struct timeval tv;
  gettimeofday(&tv, NULL);
  const time_t time_now = tv.tv_sec;
  struct tm now;
  struct tm *pnow = localtime_r(&time_now, &now);

  snprintf(buf, sizeof(buf), kTimeFormat,
           pnow->tm_year + 1900, pnow->tm_mon + 1, pnow->tm_mday, pnow->tm_hour, pnow->tm_min,
           pnow->tm_sec, static_cast<int32_t>(tv.tv_usec));

  return string(buf);
}

string FormatSystime(int64_t micros)
{
  const time_t seconds = static_cast<time_t>(micros / 1000000);
  int64_t remain_micros = static_cast<int64_t>(micros % 1000000);

  char buf[64] = {0};
  struct tm t;
  localtime_r(&seconds, &t);
  snprintf(buf, sizeof(buf), kTimeFormat,
           t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, t.tm_hour, t.tm_min,
           t.tm_sec, static_cast<int>(remain_micros));

  return string(buf);
}

string CurrentTimestampString()
{
  char buf[64] = {0};
  struct tm tt;
  time_t t = time(NULL);
  strftime(buf, 20, kTimestampStdFormat, localtime_r(&t, &tt));
  return string(buf);
}

string FormatTimestamp(int64_t seconds)
{
  char buf[64] = {0};
  struct tm tt;
  time_t t = (time_t)seconds;
  strftime(buf, 20, kTimestampStdFormat, localtime_r(&t, &tt));
  return string(buf);
}

int64_t ParseTimestamp(const string &time_str)
{
  char buf[64] = {0};
  tm tm_;
  strncpy(buf, time_str.c_str(), sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  strptime(buf, kTimestampStdFormat, &tm_);
  tm_.tm_isdst = -1;
  time_t t = mktime(&tm_);
  return (int64_t)t;
}

void MakeTimeoutUs(struct timespec *pts, int64_t micros)
{
  struct timeval now;
  gettimeofday(&now, NULL);
  int64_t usec = now.tv_usec + micros;
  pts->tv_sec = now.tv_sec + usec / 1000000;
  pts->tv_nsec = (usec % 1000000) * 1000;
}

void MakeTimeoutMs(struct timespec *pts, int64_t millis)
{
  MakeTimeoutUs(pts, millis * 1000LL);
}

bool IsInHourRange(int64_t min_hour, int64_t max_hour)
{
  assert(min_hour <= max_hour);

  time_t now = time(NULL);
  struct tm now_tm;
  localtime_r(&now, &now_tm);

  bool in_range = MATH_BEWTEEN(now_tm.tm_hour, min_hour, max_hour);
  return in_range;
}

void Timer::start()
{
  start_ = NowMonotonicNanos();
}

uint64_t Timer::elapsedNanos(bool reset)
{
  uint64_t now = NowMonotonicNanos();
  uint64_t elapsed = now - start_;
  if (reset)
  {
    start_ = now;
  }
  return elapsed;
}

void ChronoTimer::start()
{
  start_time_ = std::chrono::system_clock::now();
}

uint64_t ChronoTimer::end()
{
  end_time_ = std::chrono::system_clock::now();
  uint64_t elapsed_time =
      std::chrono::duration_cast<ChronoMillis>(end_time_ - start_time_).count();

  // start new timer.
  start_time_ = end_time_;
  return elapsed_time;
}

SequenceTimer::SequenceTimer()
{
  m_timer_seqid = 0;
  m_last_error[0] = 0;
}

int64_t SequenceTimer::StartTimer(uint32_t timeout_ms, const TimeoutCallback &cb)
{
  if (!cb || 0 == timeout_ms)
  {
    //_LOG_LAST_ERROR("param is invalid: timeout_ms = %u, cb = %d", timeout_ms, (cb ? true : false));
    return -1;
  }

  std::shared_ptr<TimerItem> item(new TimerItem);
  item->stoped = false;
  item->id = m_timer_seqid;
  item->timeout = NowSystimeMillis() + timeout_ms;
  item->cb = cb;

  m_timers[timeout_ms].push_back(item);
  m_id_2_timer[m_timer_seqid] = item;

  return m_timer_seqid++;
}

int32_t SequenceTimer::StopTimer(int64_t timer_id)
{
  std::unordered_map<int64_t, std::shared_ptr<TimerItem>>::iterator it =
      m_id_2_timer.find(timer_id);
  if (m_id_2_timer.end() == it)
  {
    //_LOG_LAST_ERROR("timer id %ld not exist", timer_id);
    return -1;
  }

  it->second->stoped = true;
  m_id_2_timer.erase(it);
  return 0;
}

int32_t SequenceTimer::Update()
{
  int32_t num = 0;
  int64_t now = NowSystimeMillis();
  int32_t ret = 0;
  uint32_t old_timeout = 0;
  uint32_t timer_map_size = m_timers.size();

  std::unordered_map<uint32_t,
                     std::list<std::shared_ptr<TimerItem>>>::iterator mit =
      m_timers.begin();
  std::list<std::shared_ptr<TimerItem>>::iterator lit;
  while (mit != m_timers.end())
  {
    // not gc actively
    std::list<std::shared_ptr<TimerItem>> &timer_list = mit->second;
    while (!timer_list.empty())
    {
      lit = timer_list.begin();
      if ((*lit)->stoped)
      {
        timer_list.erase(lit);
        continue;
      }

      if ((*lit)->timeout > now)
      {
        // entry after this is not timeout
        break;
      }

      old_timeout = mit->first;
      ret = (*lit)->cb();

      // <0 remove timer, =0 continue, >0 restart timer with new timeout
      if (ret < 0)
      {
        m_id_2_timer.erase((*lit)->id);
        timer_list.erase(lit);
      }
      else
      {
        std::shared_ptr<TimerItem> back_item = *lit;
        timer_list.erase(lit);
        if (ret > 0)
        {
          back_item->timeout = now + ret;
          m_timers[ret].push_back(back_item);
        }
        else
        {
          back_item->timeout = now + old_timeout;
          timer_list.push_back(back_item);
        }
      }
      ++num;
    }

    if (timer_map_size != m_timers.size())
    {
      // m_timers now only inc not dec
      break;
    }
    ++mit;
  }

  return num;
}

} // namespace util
} // namespace mycc