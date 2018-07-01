
#ifndef MYCC_UTIL_THREAD_UTIL_H_
#define MYCC_UTIL_THREAD_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <syscall.h>
#include <unistd.h>
#include <functional>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <thread>
#include "atomic_util.h"
#include "blocking_queue.h"
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

/**
 * A simple wrapper for std::thread
 */

class StdThreadBase
{
public:
  /**
   * @brief Construct Function. Default thread pointer is null.
   */
  StdThreadBase() { thread_ = nullptr; }

  virtual ~StdThreadBase() {}

  /**
   * @brief Creat a new thread and call *run()* function.
   */
  void start()
  {
    thread_.reset(new std::thread([this]() { this->run(); }));
  }

  /**
   * @brief Detach the thread.
   * It don't need to be waited until it finish.
   */
  void detach() { thread_->detach(); }

  /**
   * @brief Join the thread.
   * It should be waited until it finish.
   */
  void join() { thread_->join(); }

  /**
   * @brief Define what to be done on this thread through override this
   * function.
   */
  virtual void run() = 0;

protected:
  std::unique_ptr<std::thread> thread_;
};

/**
 * ThreadWorker maintains a job queue. It executes the jobs in the job queue
 * sequentianlly in a separate thread.
 *
 * Use addJob() to add a new job to the job queue.
 */
class ThreadWorker : protected StdThreadBase
{
public:
  typedef std::function<void()> JobFunc;

  /**
   * @brief Construct Function. Default size of job queue is 0 and not stopping.
   */
  ThreadWorker() : stopping_(false), empty_(true) { start(); }

  /**
   * @brief Destruct Function.
   * If it's running, wait until all job finish and then stop it.
   */
  ~ThreadWorker()
  {
    if (!stopping_)
    {
      wait();
      stop();
    }
  }

  /**
   * @brief Finish current running job and quit the thread.
   */
  void stop()
  {
    stopping_ = true;
    jobs_.enqueue([]() {});
    join();
  }

  /**
   * @brief Add a new job to the job queue.
   */
  void addJob(JobFunc func)
  {
    empty_ = false;
    jobs_.enqueue(func);
  }

  /**
   * @brief Wait until all jobs was done (the job queue was empty).
   */
  void wait()
  {
    finishCV_.wait([this] { return empty_; });
  }

protected:
  /**
   * @brief Execute jobs in the job queue sequentianlly,
   * @note If finish all the jobs in the job queue,
   * notifies all the waiting threads the job queue was empty.
   */
  virtual void run()
  {
    while (true)
    {
      JobFunc func = jobs_.dequeue();
      if (stopping_)
        break;
      func();
      if (jobs_.empty())
      {
        finishCV_.notify_all([this] { empty_ = true; });
      }
    }
  }

  SimpleQueue<JobFunc> jobs_;
  bool stopping_;
  LockedCondition finishCV_;
  bool empty_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREAD_UTIL_H_