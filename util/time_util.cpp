
#include "time_util.h"

namespace mycc
{
namespace util
{

void ChronoTimer::start()
{
  start_time_ = std::chrono::system_clock::now();
}

uint64_t ChronoTimer::end(const string &msg)
{
  end_time_ = std::chrono::system_clock::now();
  uint64_t elapsed_time =
      std::chrono::duration_cast<ChronoMillis>(end_time_ - start_time_).count();

  // start new timer.
  start_time_ = end_time_;
  return elapsed_time;
}

} // namespace util
} // namespace mycc