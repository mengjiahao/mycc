
#ifndef MYCC_UTIL_THREAD_UTIL_H_
#define MYCC_UTIL_THREAD_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <sched.h>
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

class PosixThread
{
public:
  static const uint64_t kDefaultThreadStackSize = (1 << 23); // 8192K
  static uint64_t GetTID();

  static uint64_t PthreadIntId();

  static pthread_t ThisThreadId()
  {
    return pthread_self();
  }

  static bool isThreadIdEqual(pthread_t &a, pthread_t &b)
  {
    return pthread_equal(a, b) == 0;
  }

  static void ExitThisThread()
  {
    pthread_exit(NULL);
  }

  static void YieldThisThread()
  {
    ::sched_yield();
  }

  static void UsleepThisThread(uint32_t micros)
  {
    struct timespec sleep_time = {0, 0};
    struct timespec remaining;
    sleep_time.tv_sec = static_cast<time_t>(micros / 1000000);
    sleep_time.tv_nsec = static_cast<long>(micros % 1000000 * 1000);

    while (::nanosleep(&sleep_time, &remaining) == -1 && errno == EINTR)
    {
      sleep_time = remaining;
    }
  }

  static void SleepThisThread(uint32_t secs)
  {
    ::sleep(secs);
  }

  static cpu_set_t GetThisThreadCpuAffinity()
  {
    cpu_set_t mask;
    CPU_ZERO(&mask);
    ::sched_getaffinity(0, sizeof(mask), &mask);
    return mask;
  }

  static bool SetThisThreadCpuAffinity(cpu_set_t mask)
  {
    if (!::sched_setaffinity(0, sizeof(mask), &mask))
    {
      return false;
    }
    /* guaranteed to take effect immediately */
    sched_yield();
    return true;
  }

  static bool SetCpuMask(int32_t cpu_id, cpu_set_t mask)
  {
    int32_t cpu_num = sysconf(_SC_NPROCESSORS_CONF);
    if (cpu_id < 0 || cpu_id >= cpu_num)
    {
      return false;
    }
    if (CPU_ISSET(cpu_id, &mask))
    {
      return true;
    }
    CPU_SET(cpu_id, &mask);
    return true;
  }

  static void *StartProcWrapper(void *arg);

  PosixThread(void (*func)(void *arg), void *arg, const string &name = "MyThread");
  PosixThread(std::function<void()> func, const string &name = "MyThread");
  ~PosixThread();

  pthread_t tid() const { return tid_; }
  string name() const { return name_; }
  CountDownLatch &latch() { return latch_; }
  bool amSelf() const
  {
    return (pthread_self() == tid_);
  }

  bool isStarted();
  void setAttr(uint64_t stack_size = 0, bool joinable = true);
  bool start();
  bool startForLaunch();
  bool join();
  bool kill(int32_t signal_val);
  bool detach();

private:
  pthread_t tid_;
  pthread_attr_t attr_;
  string name_;
  CountDownLatch latch_;

  void (*function_)(void *) = nullptr;
  void *arg_ = nullptr;

  bool isStdFunction_ = false;
  std::function<void()> user_proc_; // for c++11

  DISALLOW_COPY_AND_ASSIGN(PosixThread);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREAD_UTIL_H_