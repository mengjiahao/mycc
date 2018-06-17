

#ifndef MYCC_UTIL_THREAD_UTIL_H_
#define MYCC_UTIL_THREAD_UTIL_H_

#include <pthread.h>
#include <thread>

namespace mycc
{
namespace util
{

class StdThread : public Thread
{
public:
  // name and thread_options are both ignored.
  StdThread(const ThreadOptions &thread_options, const string &name,
            std::function<void()> fn)
      : thread_(fn) {}
  ~StdThread() override { thread_.join(); }

private:
  std::thread thread_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREAD_UTIL_H_