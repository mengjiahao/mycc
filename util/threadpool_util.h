
#ifndef MYCC_UTIL_THREADPOOL_UTIL_H_
#define MYCC_UTIL_THREADPOOL_UTIL_H_

#include <functional>
#include <memory>
#include "env_util.h"

namespace mycc
{
namespace util
{

class ThreadPoolImpl : public ThreadPool
{
public:
  ThreadPoolImpl();
  ~ThreadPoolImpl();

  static void PthreadCall(const char *label, int32_t result);

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADPOOL_UTIL_H_