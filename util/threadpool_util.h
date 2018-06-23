
#ifndef MYCC_UTIL_THREADPOOL_UTIL_H_
#define MYCC_UTIL_THREADPOOL_UTIL_H_

#include <pthread.h>
#include <deque>
#include <functional>
#include <memory>
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
  void joinAllThreads() override;

  // Set the number of background threads that will be executing the
  // scheduled jobs.
  void setBackgroundThreads(int num) override;
  int32_t getBackgroundThreads() override;

  // Get the number of jobs scheduled in the ThreadPool queue.
  uint32_t getQueueLen() const override;

  // Waits for all jobs to complete those
  // that already started running and those that did not
  // start yet
  void waitForJobsAndJoinAllThreads() override;

  // Make threads to run at a lower kernel IO priority
  // Currently only has effect on Linux
  void lowerIOPriority();

  // Make threads to run at a lower kernel CPU priority
  // Currently only has effect on Linux
  void lowerCPUPriority();

  // Ensure there is at aleast num threads in the pool
  // but do not kill threads if there are more
  void incBackgroundThreadsIfNeeded(int num);

  // Submit a fire and forget job
  // These jobs can not be unscheduled

  // This allows to submit the same job multiple times
  void submitJob(const std::function<void()> &) override;
  // This moves the function in for efficiency
  void submitJob(std::function<void()> &&) override;

  // Schedule a job with an unschedule tag and unschedule function
  // Can be used to filter and unschedule jobs by a tag
  // that are still in the queue and did not start running
  void schedule(void (*function)(void *arg1), void *arg, void *tag,
                void (*unschedFunction)(void *arg));

  // Filter jobs that are still in a queue and match
  // the given tag. Remove them from a queue if any
  // and for each such job execute an unschedule function
  // if such was given at scheduling time.
  int32_t unSchedule(void *tag);

  void setHostEnv(Env *env);

  Env *getHostEnv() const;

  // Return the thread priority.
  // This would allow its member-thread to know its priority.
  Env::Priority getThreadPriority() const;

  // Set the thread priority.
  void setThreadPriority(Env::Priority priority);

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
      initPool(attr);
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
  virtual void schedule(void (*function)(void *), void *arg, const string &name = "BgWork");

  // Return a description of the pool implementation.
  virtual string toDebugString();

  // Stop executing any tasks. Tasks already scheduled will keep running. Tasks
  // not yet scheduled won't be scheduled. Tasks submitted in future will be
  // queued but won't be scheduled.
  virtual void pause();

  // Resume executing tasks.
  virtual void resume();

  void initPool(void *attr);

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADPOOL_UTIL_H_