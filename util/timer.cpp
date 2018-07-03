
#include "timer.h"

namespace mycc
{
namespace util
{

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