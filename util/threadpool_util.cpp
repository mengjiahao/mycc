
#include "threadpool_util.h"
#include <stdlib.h>
#include <unistd.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include "macros_util.h"
#include "math_util.h"
#include "string_util.h"
#include "types_util.h"

#ifdef OS_LINUX
#include <sys/syscall.h>
#include <sys/resource.h>
#endif

namespace mycc
{
namespace util
{

// check for result = pthread_xxx()
void ThreadPoolImpl::PthreadCall(const char *label, int32_t result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    abort();
  }
}

struct ThreadPoolImpl::Impl
{

  Impl();
  ~Impl();

  void joinThreads(bool wait_for_jobs_to_complete);

  void setBackgroundThreadsInternal(int32_t num, bool allow_reduce);
  int32_t getBackgroundThreads();

  uint32_t getQueueLen() const
  {
    return queue_len_.load(std::memory_order_relaxed);
  }

  void lowerIOPriority();

  void lowerCPUPriority();

  void wakeUpAllThreads()
  {
    bgsignal_.notify_all();
  }

  void BGThread(uint64_t thread_id);

  void startBGThreads();

  void submit(std::function<void()> &&schedule,
              std::function<void()> &&unschedule, void *tag);

  int32_t unSchedule(void *arg);

  void setHostEnv(Env *env) { env_ = env; }

  Env *getHostEnv() const { return env_; }

  bool hasExcessiveThread() const
  {
    return static_cast<int32_t>(bgthreads_.size()) > total_threads_limit_;
  }

  // Return true iff the current thread is the excessive thread to terminate.
  // Always terminate the running thread that is added last, even if there are
  // more than one thread to terminate.
  bool isLastExcessiveThread(uint64_t thread_id) const
  {
    return hasExcessiveThread() && thread_id == bgthreads_.size() - 1;
  }

  bool isExcessiveThread(uint64_t thread_id) const
  {
    return static_cast<int>(thread_id) >= total_threads_limit_;
  }

  // Return the thread priority.
  // This would allow its member-thread to know its priority.
  Env::Priority getThreadPriority() const { return priority_; }

  // Set the thread priority.
  void setThreadPriority(Env::Priority priority) { priority_ = priority; }

private:
  static void *BGThreadWrapper(void *arg);

  bool low_io_priority_;
  bool low_cpu_priority_;
  Env::Priority priority_;
  Env *env_;

  int32_t total_threads_limit_;
  std::atomic<uint32_t> queue_len_; // Queue length. Used for stats reporting
  bool exit_all_threads_;
  bool wait_for_jobs_to_complete_;

  // Entry per Schedule()/Submit() call
  struct BGItem
  {
    void *tag = nullptr;
    std::function<void()> function;
    std::function<void()> unschedFunction;
  };

  using BGQueue = std::deque<BGItem>;
  BGQueue queue_;

  std::mutex mu_;
  std::condition_variable bgsignal_;
  std::vector<std::thread> bgthreads_;
};

inline ThreadPoolImpl::Impl::Impl()
    : low_io_priority_(false),
      low_cpu_priority_(false),
      priority_(Env::LOW),
      env_(nullptr),
      total_threads_limit_(0),
      queue_len_(),
      exit_all_threads_(false),
      wait_for_jobs_to_complete_(false),
      queue_(),
      mu_(),
      bgsignal_(),
      bgthreads_()
{
}

inline ThreadPoolImpl::Impl::~Impl() { assert(bgthreads_.size() == 0U); }

void ThreadPoolImpl::Impl::joinThreads(bool wait_for_jobs_to_complete)
{

  std::unique_lock<std::mutex> lock(mu_);
  assert(!exit_all_threads_);

  wait_for_jobs_to_complete_ = wait_for_jobs_to_complete;
  exit_all_threads_ = true;
  // prevent threads from being recreated right after they're joined, in case
  // the user is concurrently submitting jobs.
  total_threads_limit_ = 0;

  lock.unlock();

  bgsignal_.notify_all();

  for (auto &th : bgthreads_)
  {
    th.join();
  }

  bgthreads_.clear();

  exit_all_threads_ = false;
  wait_for_jobs_to_complete_ = false;
}

inline void ThreadPoolImpl::Impl::lowerIOPriority()
{
  std::lock_guard<std::mutex> lock(mu_);
  low_io_priority_ = true;
}

inline void ThreadPoolImpl::Impl::lowerCPUPriority()
{
  std::lock_guard<std::mutex> lock(mu_);
  low_cpu_priority_ = true;
}

void ThreadPoolImpl::Impl::BGThread(uint64_t thread_id)
{
  bool low_io_priority = false;
  bool low_cpu_priority = false;

  while (true)
  {
    // Wait until there is an item that is ready to run
    std::unique_lock<std::mutex> lock(mu_);
    // Stop waiting if the thread needs to do work or needs to terminate.
    while (!exit_all_threads_ && !isLastExcessiveThread(thread_id) &&
           (queue_.empty() || isExcessiveThread(thread_id)))
    {
      bgsignal_.wait(lock);
    }

    if (exit_all_threads_)
    { // mechanism to let BG threads exit safely

      if (!wait_for_jobs_to_complete_ ||
          queue_.empty())
      {
        break;
      }
    }

    if (isLastExcessiveThread(thread_id))
    {
      // Current thread is the last generated one and is excessive.
      // We always terminate excessive thread in the reverse order of
      // generation time.
      auto &terminating_thread = bgthreads_.back();
      terminating_thread.detach();
      bgthreads_.pop_back();

      if (hasExcessiveThread())
      {
        // There is still at least more excessive thread to terminate.
        wakeUpAllThreads();
      }
      break;
    }

    auto func = std::move(queue_.front().function);
    queue_.pop_front();

    queue_len_.store(static_cast<unsigned int>(queue_.size()),
                     std::memory_order_relaxed);

    bool decrease_io_priority = (low_io_priority != low_io_priority_);
    bool decrease_cpu_priority = (low_cpu_priority != low_cpu_priority_);
    lock.unlock();

#ifdef OS_LINUX
    if (decrease_cpu_priority)
    {
      ::setpriority(
          PRIO_PROCESS,
          // Current thread.
          0,
          // Lowest priority possible.
          19);
      low_cpu_priority = true;
    }

    if (decrease_io_priority)
    {
#define IOPRIO_CLASS_SHIFT (13)
#define IOPRIO_PRIO_VALUE(class, data) (((class) << IOPRIO_CLASS_SHIFT) | data)
      // Put schedule into IOPRIO_CLASS_IDLE class (lowest)
      // These system calls only have an effect when used in conjunction
      // with an I/O scheduler that supports I/O priorities. As at
      // kernel 2.6.17 the only such scheduler is the Completely
      // Fair Queuing (CFQ) I/O scheduler.
      // To change scheduler:
      //  echo cfq > /sys/block/<device_name>/queue/schedule
      // Tunables to consider:
      //  /sys/block/<device_name>/queue/slice_idle
      //  /sys/block/<device_name>/queue/slice_sync
      ::syscall(SYS_ioprio_set, 1, // IOPRIO_WHO_PROCESS
                0,                 // current thread
                IOPRIO_PRIO_VALUE(3, 0));
      low_io_priority = true;
    }
#else
    (void)decrease_io_priority; // avoid 'unused variable' error
    (void)decrease_cpu_priority;
#endif
    func();
  }
}

// Helper struct for passing arguments when creating threads.
struct BGThreadMetadata
{
  ThreadPoolImpl::Impl *thread_pool_;
  uint64_t thread_id_; // Thread count in the thread.
  BGThreadMetadata(ThreadPoolImpl::Impl *thread_pool, uint64_t thread_id)
      : thread_pool_(thread_pool), thread_id_(thread_id) {}
};

void *ThreadPoolImpl::Impl::BGThreadWrapper(void *arg)
{
  BGThreadMetadata *meta = reinterpret_cast<BGThreadMetadata *>(arg);
  uint64_t thread_id = meta->thread_id_;
  ThreadPoolImpl::Impl *tp = meta->thread_pool_;
  delete meta;
  tp->BGThread(thread_id);
  return nullptr;
}

void ThreadPoolImpl::Impl::setBackgroundThreadsInternal(int32_t num,
                                                        bool allow_reduce)
{
  std::unique_lock<std::mutex> lock(mu_);
  if (exit_all_threads_)
  {
    lock.unlock();
    return;
  }
  if (num > total_threads_limit_ ||
      (num < total_threads_limit_ && allow_reduce))
  {
    total_threads_limit_ = MATH_MAX(0, num);
    wakeUpAllThreads();
    startBGThreads();
  }
}

int32_t ThreadPoolImpl::Impl::getBackgroundThreads()
{
  std::unique_lock<std::mutex> lock(mu_);
  return total_threads_limit_;
}

void ThreadPoolImpl::Impl::startBGThreads()
{
  // Start background thread if necessary
  while ((int)bgthreads_.size() < total_threads_limit_)
  {

    std::thread p_t(&BGThreadWrapper,
                    new BGThreadMetadata(this, bgthreads_.size()));

// Set the thread name to aid debugging
#if defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 12)
    auto th_handle = p_t.native_handle();
    string thread_priority = Env::PriorityToString(getThreadPriority());
    string thread_name_stream;
    StringFormatTo(&thread_name_stream, "threadpool:%s%" PRIu64, thread_priority.c_str(), bgthreads_.size());
    pthread_setname_np(th_handle, thread_name_stream.c_str());
#endif
#endif
    bgthreads_.push_back(std::move(p_t));
  }
}

void ThreadPoolImpl::Impl::submit(std::function<void()> &&schedule,
                                  std::function<void()> &&unschedule, void *tag)
{

  std::lock_guard<std::mutex> lock(mu_);

  if (exit_all_threads_)
  {
    return;
  }

  startBGThreads();

  // Add to priority queue
  queue_.push_back(BGItem());

  auto &item = queue_.back();
  item.tag = tag;
  item.function = std::move(schedule);
  item.unschedFunction = std::move(unschedule);

  queue_len_.store(static_cast<uint32_t>(queue_.size()),
                   std::memory_order_relaxed);

  if (!hasExcessiveThread())
  {
    // Wake up at least one waiting thread.
    bgsignal_.notify_one();
  }
  else
  {
    // Need to wake up all threads to make sure the one woken
    // up is not the one to terminate.
    wakeUpAllThreads();
  }
}

int32_t ThreadPoolImpl::Impl::unSchedule(void *arg)
{
  int32_t count = 0;

  std::vector<std::function<void()>> candidates;
  {
    std::lock_guard<std::mutex> lock(mu_);

    // Remove from priority queue
    BGQueue::iterator it = queue_.begin();
    while (it != queue_.end())
    {
      if (arg == (*it).tag)
      {
        if (it->unschedFunction)
        {
          candidates.push_back(std::move(it->unschedFunction));
        }
        it = queue_.erase(it);
        count++;
      }
      else
      {
        ++it;
      }
    }
    queue_len_.store(static_cast<uint32_t>(queue_.size()),
                     std::memory_order_relaxed);
  }

  // Run unschedule functions outside the mutex
  for (auto &f : candidates)
  {
    f();
  }

  return count;
}

ThreadPoolImpl::ThreadPoolImpl() : impl_(new Impl())
{
}

ThreadPoolImpl::~ThreadPoolImpl()
{
}

void ThreadPoolImpl::joinAllThreads()
{
  impl_->joinThreads(false);
}

void ThreadPoolImpl::setBackgroundThreads(int32_t num)
{
  impl_->setBackgroundThreadsInternal(num, true);
}

int32_t ThreadPoolImpl::getBackgroundThreads()
{
  return impl_->getBackgroundThreads();
}

uint32_t ThreadPoolImpl::getQueueLen() const
{
  return impl_->getQueueLen();
}

void ThreadPoolImpl::waitForJobsAndJoinAllThreads()
{
  impl_->joinThreads(true);
}

void ThreadPoolImpl::lowerIOPriority()
{
  impl_->lowerIOPriority();
}

void ThreadPoolImpl::lowerCPUPriority()
{
  impl_->lowerCPUPriority();
}

void ThreadPoolImpl::incBackgroundThreadsIfNeeded(int32_t num)
{
  impl_->setBackgroundThreadsInternal(num, false);
}

void ThreadPoolImpl::submitJob(const std::function<void()> &job)
{
  auto copy(job);
  impl_->submit(std::move(copy), std::function<void()>(), nullptr);
}

void ThreadPoolImpl::submitJob(std::function<void()> &&job)
{
  impl_->submit(std::move(job), std::function<void()>(), nullptr);
}

void ThreadPoolImpl::schedule(void (*function)(void *arg1), void *arg,
                              void *tag, void (*unschedFunction)(void *arg))
{

  std::function<void()> fn = [arg, function] { function(arg); };

  std::function<void()> unfn;
  if (unschedFunction != nullptr)
  {
    auto uf = [arg, unschedFunction] { unschedFunction(arg); };
    unfn = std::move(uf);
  }

  impl_->submit(std::move(fn), std::move(unfn), tag);
}

int32_t ThreadPoolImpl::unSchedule(void *arg)
{
  return impl_->unSchedule(arg);
}

void ThreadPoolImpl::setHostEnv(Env *env) { impl_->setHostEnv(env); }

Env *ThreadPoolImpl::getHostEnv() const { return impl_->getHostEnv(); }

// Return the thread priority.
// This would allow its member-thread to know its priority.
Env::Priority ThreadPoolImpl::getThreadPriority() const
{
  return impl_->getThreadPriority();
}

// Set the thread priority.
void ThreadPoolImpl::setThreadPriority(Env::Priority priority)
{
  impl_->setThreadPriority(priority);
}

} // namespace util
} // namespace mycc