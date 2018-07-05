
#ifndef MYCC_UTIL_THREADPOOL_UTIL_H_
#define MYCC_UTIL_THREADPOOL_UTIL_H_

#include <pthread.h>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>
#include "env_util.h"
#include "locks_util.h"

namespace mycc
{
namespace util
{

class ThreadPoolImpl : public ThreadPool
{
public:
  ThreadPoolImpl();
  ~ThreadPoolImpl();

  // Implement ThreadPool interfaces

  // Wait for all threads to finish.
  // Discards all the jobs that did not
  // start executing and waits for those running
  // to complete
  void JoinAllThreads() override;

  // Set the number of background threads that will be executing the
  // scheduled jobs.
  void SetBackgroundThreads(int num) override;
  int32_t GetBackgroundThreads() override;

  // Get the number of jobs scheduled in the ThreadPool queue.
  uint32_t GetQueueLen() const override;

  // Waits for all jobs to complete those
  // that already started running and those that did not
  // start yet
  void WaitForJobsAndJoinAllThreads() override;

  // Make threads to run at a lower kernel IO priority
  // Currently only has effect on Linux
  void LowerIOPriority();

  // Make threads to run at a lower kernel CPU priority
  // Currently only has effect on Linux
  void LowerCPUPriority();

  // Ensure there is at aleast num threads in the pool
  // but do not kill threads if there are more
  void IncBackgroundThreadsIfNeeded(int num);

  // Submit a fire and forget job
  // These jobs can not be unscheduled

  // This allows to submit the same job multiple times
  void SubmitJob(const std::function<void()> &) override;
  // This moves the function in for efficiency
  void SubmitJob(std::function<void()> &&) override;

  // Schedule a job with an unschedule tag and unschedule function
  // Can be used to filter and unschedule jobs by a tag
  // that are still in the queue and did not start running
  void Schedule(void (*function)(void *arg1), void *arg, void *tag,
                void (*unschedFunction)(void *arg));

  // Filter jobs that are still in a queue and match
  // the given tag. Remove them from a queue if any
  // and for each such job execute an unschedule function
  // if such was given at scheduling time.
  int32_t UnSchedule(void *tag);

  void SetHostEnv(Env *env);

  Env *GetHostEnv() const;

  // Return the thread priority.
  // This would allow its member-thread to know its priority.
  Env::Priority GetThreadPriority() const;

  // Set the thread priority.
  void SetThreadPriority(Env::Priority priority);

  struct Impl;

private:
  // Current public virtual interface does not provide usable
  // functionality and thus can not be used internally to
  // facade different implementations.
  //
  // We propose a pimpl idiom in order to easily replace the thread pool impl
  // w/o touching the header file but providing a different .cc potentially
  // CMake option driven.
  //
  // Another option is to introduce a Env::MakeThreadPool() virtual interface
  // and override the environment. This would require refactoring ThreadPool usage.
  //
  // We can also combine these two approaches
  std::unique_ptr<Impl> impl_;

  DISALLOW_COPY_AND_ASSIGN(ThreadPoolImpl);
};

// Background execution service.
// The implementation of the ThreadPool type ensures that the Schedule method
// runs the functions it is provided in FIFO order when the scheduling is done
// by a single thread.
class PosixFixedThreadPool
{
public:
  explicit PosixFixedThreadPool(int32_t max_threads, bool eager_init = false,
                                void *attr = NULL)
      : bg_cv_(&mu_),
        num_pool_threads_(0),
        max_threads_(max_threads),
        shutting_down_(false),
        paused_(false)
  {
    if (eager_init)
    {
      // Create pool threads immediately
      MutexLock ml(&mu_);
      InitPool(attr);
    }
  }
  virtual ~PosixFixedThreadPool();

  // Instantiate a new thread pool with a fixed number of threads. The caller
  // should delete the pool to free associated resources.
  // If "eager_init" is true, children threads will be created immediately.
  // A caller may optionally set "attr" to alter default thread behaviour.
  static PosixFixedThreadPool *NewFixedThreadPool(int32_t num_threads, bool eager_init = false,
                                                  void *attr = NULL);

  // Arrange to run "(*function)(arg)" once in one of a pool of
  // background threads.
  //
  // "function" may run in an unspecified thread.  Multiple functions
  // added to the same pool may run concurrently in different threads.
  // I.e., the caller may not assume that background work items are
  // serialized.
  virtual void Schedule(void (*function)(void *), void *arg, const string &name = "BgWork");

  // Return a description of the pool implementation.
  virtual string ToDebugString();

  // Stop executing any tasks. Tasks already scheduled will keep running. Tasks
  // not yet scheduled won't be scheduled. Tasks submitted in future will be
  // queued but won't be scheduled.
  virtual void Pause();

  // Resume executing tasks.
  virtual void Resume();

  void InitPool(void *attr);

private:
  // BGThread() is the body of the background thread
  void BGThread();

  static void *BGWrapper(void *arg)
  {
    reinterpret_cast<PosixFixedThreadPool *>(arg)->BGThread();
    return NULL;
  }

  Mutex mu_;
  CondVar bg_cv_;
  int32_t num_pool_threads_;
  int32_t max_threads_;

  bool shutting_down_;
  bool paused_;

  // Entry per Schedule() call
  struct BGItem
  {
    void *arg;
    void (*function)(void *);
    string name;
  };
  typedef std::deque<BGItem> BGQueue;
  BGQueue queue_;
  std::vector<pthread_t> bgthreads_;

  DISALLOW_COPY_AND_ASSIGN(PosixFixedThreadPool);
};

// An simple unscalable thread pool.
class SimpleThreadPool
{
public:
  SimpleThreadPool(int32_t thread_num = 10)
      : threads_num_(thread_num),
        pending_num_(0),
        work_cv_(&mutex_),
        stop_(false),
        last_task_id_(0),
        running_task_id_(0),
        schedule_cost_sum_(0),
        schedule_count_(0),
        task_cost_sum_(0),
        task_count_(0)
  {
    //start();
  }

  ~SimpleThreadPool()
  {
    //stop(false);
  }

  bool Start();

  // Stop the thread pool.
  // Wait for all pending task to complete if wait is true.
  bool Stop(bool wait);

  // Task definition.
  typedef std::function<void()> Task;

  // Add a task to the thread pool.
  void AddTask(const Task &task);

  void AddPriorityTask(const Task &task);

  int64_t DelayTask(int64_t delay, const Task &task);

  /// Cancel a delayed task
  /// if running, wait if non_block==false; return immediately if non_block==true
  bool CancelTask(int64_t task_id, bool non_block = false,
                  bool *is_running = nullptr);

  int64_t pending_num() const
  {
    return pending_num_;
  }

  // log format: 3 numbers seperated by " ", e.g. "15 24 32"
  // 1st: thread pool schedule average cost (ms)
  // 2nd: user task average cost (ms)
  // 3rd: total task count since last ProfilingLog called
  string ProfilingLog();

private:
  static void *ThreadWrapper(void *arg)
  {
    reinterpret_cast<SimpleThreadPool *>(arg)->ThreadProc();
    return nullptr;
  }

  void ThreadProc();

private:
  struct BGItem
  {
    int64_t id;
    int64_t exe_time;
    Task task;
    bool operator<(const BGItem &item) const
    { // top is min-heap
      if (exe_time != item.exe_time)
      {
        return exe_time > item.exe_time;
      }
      else
      {
        return id > item.id;
      }
    }

    BGItem() {}
    BGItem(int64_t id_t, int64_t exe_time_t, const Task &task_t)
        : id(id_t), exe_time(exe_time_t), task(task_t) {}
  };
  typedef std::priority_queue<BGItem> BGQueue;
  typedef std::map<int64_t, BGItem> BGMap;

  int32_t threads_num_;
  std::deque<BGItem> queue_;
  volatile int pending_num_;
  Mutex mutex_;
  CondVar work_cv_;
  bool stop_;
  std::vector<pthread_t> tids_;

  BGQueue time_queue_;
  BGMap latest_;
  int64_t last_task_id_;
  int64_t running_task_id_;

  // for profiling
  int64_t schedule_cost_sum_;
  int64_t schedule_count_;
  int64_t task_cost_sum_;
  int64_t task_count_;
};

// c++11 std::thread threadpool
class TaskThreadPool
{
private:
  struct task_element_t
  {
    bool run_with_id;
    const std::function<void()> no_id;
    const std::function<void(uint64_t)> with_id;

    explicit task_element_t(const std::function<void()> &f) : run_with_id(false), no_id(f), with_id(nullptr) {}
    explicit task_element_t(const std::function<void(uint64_t)> &f) : run_with_id(true), no_id(nullptr), with_id(f) {}
  };
  std::queue<task_element_t> tasks_;
  std::vector<std::thread> threads_;
  std::mutex mutex_;
  std::condition_variable condition_;
  std::condition_variable completed_;
  bool running_;
  bool complete_;
  uint64_t available_;
  uint64_t total_;

public:
  /// @brief Constructor.
  explicit TaskThreadPool(uint64_t pool_size)
      : threads_(pool_size), running_(true), complete_(true),
        available_(pool_size), total_(pool_size)
  {
    for (uint64_t i = 0; i < pool_size; ++i)
    {
      threads_[i] = std::thread(
          std::bind(&TaskThreadPool::main_loop, this, i));
    }
  }

  /// @brief Destructor.
  ~TaskThreadPool()
  {
    // Set running flag to false then notify all threads.
    {
      std::unique_lock<std::mutex> lock(mutex_);
      running_ = false;
      condition_.notify_all();
    }

    try
    {
      for (auto &t : threads_)
      {
        t.join();
      }
    }
    // Suppress all exceptions.
    catch (const std::exception &)
    {
    }
  }

  /// @brief Add task to the thread pool if a thread is currently available.
  template <typename Task>
  void RunTask(Task task)
  {
    std::unique_lock<std::mutex> lock(mutex_);

    // Set task and signal condition variable so that a worker thread will
    // wake up and use the task.
    tasks_.push(task_element_t(static_cast<std::function<void()>>(task)));
    complete_ = false;
    condition_.notify_one();
  }

  template <typename Task>
  void RunTaskWithID(Task task)
  {
    std::unique_lock<std::mutex> lock(mutex_);

    // Set task and signal condition variable so that a worker thread will
    // wake up and use the task.
    tasks_.push(task_element_t(static_cast<std::function<void(uint64_t)>>(
        task)));
    complete_ = false;
    condition_.notify_one();
  }

  /// @brief Wait for queue to be empty
  void WaitWorkComplete()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!complete_)
      completed_.wait(lock);
  }

private:
  /// @brief Entry point for pool threads.
  void main_loop(uint64_t index)
  {
    while (running_)
    {
      // Wait on condition variable while the task is empty and
      // the pool is still running.
      std::unique_lock<std::mutex> lock(mutex_);
      while (tasks_.empty() && running_)
      {
        condition_.wait(lock);
      }
      // If pool is no longer running, break out of loop.
      if (!running_)
        break;

      // Copy task locally and remove from the queue.  This is
      // done within its own scope so that the task object is
      // destructed immediately after running the task.  This is
      // useful in the event that the function contains
      // shared_ptr arguments bound via bind.
      {
        auto tasks = tasks_.front();
        tasks_.pop();
        // Decrement count, indicating thread is no longer available.
        --available_;

        lock.unlock();

        // Run the task.
        try
        {
          if (tasks.run_with_id)
          {
            tasks.with_id(index);
          }
          else
          {
            tasks.no_id();
          }
        }
        // Suppress all exceptions.
        catch (const std::exception &)
        {
        }

        // Update status of empty, maybe
        // Need to recover the lock first
        lock.lock();

        // Increment count, indicating thread is available.
        ++available_;
        if (tasks_.empty() && available_ == total_)
        {
          complete_ = true;
          completed_.notify_one();
        }
      }
    } // while running_
  }
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADPOOL_UTIL_H_