
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
#include <queue>
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
  typedef void (*UserFunctionType)(void *);
  typedef std::function<void()> UserStdFunctionType;

  static uint64_t GetTID();
  static uint64_t PthreadIdInt();
  static pthread_t ThisThreadId();
  static bool IsThreadIdEqual(pthread_t &a, pthread_t &b);
  static void SetThisThreadName(string &name);
  static void ExitThisThread();
  static void YieldThisThread();
  static void UsleepThisThread(uint32_t micros);
  static void SleepThisThread(uint32_t secs);
  static cpu_set_t GetThisThreadCpuAffinity();
  static bool SetThisThreadCpuAffinity(cpu_set_t mask);
  static bool SetCpuMask(int32_t cpu_id, cpu_set_t mask);

  PosixThread(UserStdFunctionType func);
  PosixThread(UserFunctionType func, void *arg);
  ~PosixThread(){};

  string toString();
  void clearThreadId();
  pthread_t thread_id() const { return tid_; }
  bool amSelf() const
  {
    return (pthread_self() == tid_);
  }

  bool isRuning();
  bool start();
  bool join();
  bool kill(int32_t signal_val);
  bool detach();

  UserFunctionType user_func() { return user_func_; }
  void *user_arg() { return user_arg_; }
  bool is_std_func() { return is_std_func_; }
  UserStdFunctionType user_std_func() { return user_std_func_; }
  void set_is_running(bool running) { is_running_ = running; }

private:
  UserFunctionType user_func_;
  void *user_arg_;

  bool is_std_func_;
  UserStdFunctionType user_std_func_; // for c++11

  pthread_t tid_;
  bool is_running_;

  DISALLOW_COPY_AND_ASSIGN(PosixThread);
};

//////////////////////// BGThread ///////////////////////////

class BGThreadBase
{
public:
  BGThreadBase()
      : should_stop_(false),
        running_(false),
        thread_id_(0)
  {
  }

  virtual ~BGThreadBase(){};

  virtual bool start()
  {
    MutexLock l(&running_mu_);
    should_stop_ = false;
    if (!running_)
    {
      running_ = true;
      return (0 == pthread_create(&thread_id_, nullptr, RunThread, (void *)this));
    }
    return true;
  }

  virtual bool stop()
  {
    MutexLock l(&running_mu_);
    should_stop_ = true;
    if (running_)
    {
      running_ = false;
      return (0 == pthread_join(thread_id_, nullptr));
    }
    return true;
  }

  bool join()
  {
    return (0 == pthread_join(thread_id_, nullptr));
  }

  bool should_stop()
  {
    return should_stop_.load();
  }

  void set_should_stop()
  {
    should_stop_.store(true);
  }

  bool is_running()
  {
    return running_;
  }

  pthread_t thread_id() const
  {
    return thread_id_;
  }

  string thread_name() const
  {
    return thread_name_;
  }

  void set_thread_name(const string &name)
  {
    thread_name_ = name;
  }

protected:
  std::atomic<bool> should_stop_;

  virtual void *ThreadMain() = 0;

private:
  static void *RunThread(void *arg)
  {
    BGThreadBase *thread = reinterpret_cast<BGThreadBase *>(arg);
    if (!(thread->thread_name().empty()))
    {
      //SetThreadName(pthread_self(), thread->thread_name());
    }
    thread->ThreadMain();
    return nullptr;
  }

  Mutex running_mu_;
  bool running_;
  pthread_t thread_id_;
  string thread_name_;

  DISALLOW_COPY_AND_ASSIGN(BGThreadBase);
};

class BGThread : public BGThreadBase
{
public:
  struct TimerItem
  {
    uint64_t exec_time;
    void (*function)(void *);
    void *arg;

    TimerItem(uint64_t _exec_time, void (*_function)(void *), void *_arg)
        : exec_time(_exec_time),
          function(_function),
          arg(_arg) {}

    bool operator<(const TimerItem &item) const
    {
      return exec_time > item.exec_time;
    }
  };

  explicit BGThread(uint64_t full = 100000)
      : BGThreadBase::BGThreadBase(),
        full_(full),
        mu_(),
        rsignal_(&mu_),
        wsignal_(&mu_)
  {
  }

  virtual ~BGThread()
  {
    stop();
  }

  virtual bool stop() override
  {
    should_stop_ = true;
    rsignal_.signal();
    wsignal_.signal();
    return BGThreadBase::stop();
  }

  void Schedule(void (*function)(void *), void *arg);

  void DelaySchedule(uint64_t timeout_ms, void (*function)(void *), void *arg);

  void QueueSize(uint64_t *pri_size, uint64_t *qu_size);
  void QueueClear();

protected:
  virtual void *ThreadMain() override;

private:
  struct BGItem
  {
    void (*function)(void *);
    void *arg;
    BGItem(void (*_function)(void *), void *_arg)
        : function(_function), arg(_arg) {}
  };

  std::queue<BGItem> queue_;
  std::priority_queue<TimerItem> timer_queue_;

  uint64_t full_;
  Mutex mu_;
  CondVar rsignal_;
  CondVar wsignal_;
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

  SimpleSafeQueue<JobFunc> jobs_;
  bool stopping_;
  LockedCondition finishCV_;
  bool empty_;
};

////////////////////// StdThreadGroup //////////////////////////

class StdThreadGroup
{
public:
  StdThreadGroup(){};
  StdThreadGroup(const std::function<void()> &callback, uint64_t count);
  ~StdThreadGroup();
  void Add(const std::function<void()> &callback, uint64_t count = 1);
  void Start();
  void Join();
  uint64_t Size() const;

private:
  std::vector<std::thread> m_threads;
  DISALLOW_COPY_AND_ASSIGN(StdThreadGroup);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREAD_UTIL_H_