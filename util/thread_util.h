
#ifndef MYCC_UTIL_THREAD_UTIL_H_
#define MYCC_UTIL_THREAD_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <signal.h>
#include <syscall.h>
#include <unistd.h>
#include <functional>
#include "atomic_util.h"
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

typedef pthread_once_t OnceType;
void InitOnce(OnceType *once, void (*initializer)());
pid_t GetTID();
uint64_t PthreadId();

class PosixThread
{
public:
  static pthread_t CurrentThreadId()
  {
    return pthread_self();
  }

  static bool isThreadIdEqual(pthread_t &a, pthread_t &b)
  {
    return pthread_equal(a, b) == 0;
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
    struct timespec ts = {0, 0};
    ts.tv_sec = static_cast<time_t>(micros / 1000000);
    ts.tv_nsec = static_cast<long>(micros % 1000000 * 1000);
    ::nanosleep(&ts, NULL);
  }

  static void SleepCurrentThread(uint32_t secs)
  {
    ::sleep(secs);
  }

  static void *StartProcWrapper(void *arg);

  PosixThread(void (*func)(void *arg), void *arg, const string &name = "Thread");
  PosixThread(std::function<void()> func, const string &name = "Thread");
  ~PosixThread(){};

  pthread_t gettid() const { return tid_; }
  const char *getName() const { return name_.c_str(); }
  bool amSelf() const
  {
    return (pthread_self() == tid_);
  }

  bool isStarted();
  bool isRuning(pthread_t id);
  bool start();
  bool startForLaunch();
  bool Start();
  bool join();
  bool kill(int32_t signal_val);
  bool detach();

private:
  pthread_t tid_;
  pthread_attr_t attr_;
  string name_;
  CountDownLatch latch_;
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