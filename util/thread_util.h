
#ifndef MYCC_UTIL_THREAD_UTIL_H_
#define MYCC_UTIL_THREAD_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <syscall.h>
#include <unistd.h>
#include <functional>
#include "atomic_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class PosixThread
{
public:
  static int64_t CurrentThreadId()
  {
    return static_cast<int64_t>(pthread_self());
  }

  static void ExitCurrentThread()
  {
    pthread_exit(NULL);
  }

  static void YieldCurrentThread()
  {
    ::sched_yield();
  }

  static void UsleepCurrentThread(uint32_t micros)
  {
    ::usleep(micros);
  }

  static void SleepCurrentThread(uint32_t secs)
  {
    ::sleep(secs);
  }

  static void *StartProcWrapper(void *arg);

  PosixThread(void (*func)(void *arg), void *arg, const string &name = "Thread");
  PosixThread(std::function<void()> func, const string &name = "Thread");
  ~PosixThread(){};

  int64_t gettid() const { return static_cast<int64_t>(tid_); }
  const char *getName() const { return name_.c_str(); }
  bool amSelf() const
  {
    return (pthread_self() == tid_);
  }

  bool isStarted();
  bool start();
  bool Start();
  bool join();
  bool kill(int32_t signal_val);
  bool detach();

private:
  pthread_t tid_;
  string name_;
  AtomicPointer started_;

  void (*function_)(void *) = nullptr;
  void *arg_ = nullptr;

  bool isProc_ = false;
  std::function<void()> user_proc_; // for c++11

  DISALLOW_COPY_AND_ASSIGN(PosixThread);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREAD_UTIL_H_