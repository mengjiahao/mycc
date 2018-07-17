
#include "threadpool_util.h"
#include <stdlib.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>
#include "math_util.h"
#include "string_util.h"
#include "time_util.h"
#include "types_util.h"

#ifdef OS_LINUX
#include <sys/syscall.h>
#include <sys/resource.h>
#endif

namespace mycc
{
namespace util
{

namespace
{ // namespace anonymous

// check for result = pthread_xxx()
void PthreadCall(const char *label, int result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    abort();
  }
}

} // namespace

struct ThreadPoolImpl::Impl
{

  Impl();
  ~Impl();

  void JoinThreads(bool wait_for_jobs_to_complete);

  void SetBackgroundThreadsInternal(int32_t num, bool allow_reduce);
  int32_t GetBackgroundThreads();

  uint32_t GetQueueLen() const
  {
    return queue_len_.load(std::memory_order_relaxed);
  }

  void LowerIOPriority();

  void LowerCPUPriority();

  void WakeUpAllThreads()
  {
    bgsignal_.notify_all();
  }

  void BGThread(uint64_t thread_id);

  void StartBGThreads();

  void Submit(std::function<void()> &&schedule,
              std::function<void()> &&unschedule, void *tag);

  int32_t UnSchedule(void *arg);

  void SetHostEnv(Env *env) { env_ = env; }

  Env *GetHostEnv() const { return env_; }

  bool HasExcessiveThread() const
  {
    return static_cast<int32_t>(bgthreads_.size()) > total_threads_limit_;
  }

  // Return true iff the current thread is the excessive thread to terminate.
  // Always terminate the running thread that is added last, even if there are
  // more than one thread to terminate.
  bool IsLastExcessiveThread(uint64_t thread_id) const
  {
    return HasExcessiveThread() && thread_id == bgthreads_.size() - 1;
  }

  bool IsExcessiveThread(uint64_t thread_id) const
  {
    return static_cast<int>(thread_id) >= total_threads_limit_;
  }

  // Return the thread priority.
  // This would allow its member-thread to know its priority.
  Env::Priority GetThreadPriority() const { return priority_; }

  // Set the thread priority.
  void SetThreadPriority(Env::Priority priority) { priority_ = priority; }

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

void ThreadPoolImpl::Impl::JoinThreads(bool wait_for_jobs_to_complete)
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

inline void ThreadPoolImpl::Impl::LowerIOPriority()
{
  std::lock_guard<std::mutex> lock(mu_);
  low_io_priority_ = true;
}

inline void ThreadPoolImpl::Impl::LowerCPUPriority()
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
    while (!exit_all_threads_ && !IsLastExcessiveThread(thread_id) &&
           (queue_.empty() || IsExcessiveThread(thread_id)))
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

    if (IsLastExcessiveThread(thread_id))
    {
      // Current thread is the last generated one and is excessive.
      // We always terminate excessive thread in the reverse order of
      // generation time.
      auto &terminating_thread = bgthreads_.back();
      terminating_thread.detach();
      bgthreads_.pop_back();

      if (HasExcessiveThread())
      {
        // There is still at least more excessive thread to terminate.
        WakeUpAllThreads();
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

void ThreadPoolImpl::Impl::SetBackgroundThreadsInternal(int32_t num,
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
    WakeUpAllThreads();
    StartBGThreads();
  }
}

int32_t ThreadPoolImpl::Impl::GetBackgroundThreads()
{
  std::unique_lock<std::mutex> lock(mu_);
  return total_threads_limit_;
}

void ThreadPoolImpl::Impl::StartBGThreads()
{
  // Start background thread if necessary
  while ((int32_t)bgthreads_.size() < total_threads_limit_)
  {

    std::thread p_t(&BGThreadWrapper,
                    new BGThreadMetadata(this, bgthreads_.size()));

// Set the thread name to aid debugging
#if defined(_GNU_SOURCE) && defined(__GLIBC_PREREQ)
#if __GLIBC_PREREQ(2, 12)
    auto th_handle = p_t.native_handle();
    string thread_priority = env_->PriorityToString(GetThreadPriority());
    string thread_name_stream;
    StringFormatTo(&thread_name_stream, "threadpool:%s%" PRIu64, thread_priority.c_str(), bgthreads_.size());
    pthread_setname_np(th_handle, thread_name_stream.c_str());
#endif
#endif
    bgthreads_.push_back(std::move(p_t));
  }
}

void ThreadPoolImpl::Impl::Submit(std::function<void()> &&schedule,
                                  std::function<void()> &&unschedule, void *tag)
{

  std::lock_guard<std::mutex> lock(mu_);

  if (exit_all_threads_)
  {
    return;
  }

  StartBGThreads();

  // Add to priority queue
  queue_.push_back(BGItem());

  auto &item = queue_.back();
  item.tag = tag;
  item.function = std::move(schedule);
  item.unschedFunction = std::move(unschedule);

  queue_len_.store(static_cast<uint32_t>(queue_.size()),
                   std::memory_order_relaxed);

  if (!HasExcessiveThread())
  {
    // Wake up at least one waiting thread.
    bgsignal_.notify_one();
  }
  else
  {
    // Need to wake up all threads to make sure the one woken
    // up is not the one to terminate.
    WakeUpAllThreads();
  }
}

int32_t ThreadPoolImpl::Impl::UnSchedule(void *arg)
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

void ThreadPoolImpl::JoinAllThreads()
{
  impl_->JoinThreads(false);
}

void ThreadPoolImpl::SetBackgroundThreads(int32_t num)
{
  impl_->SetBackgroundThreadsInternal(num, true);
}

int32_t ThreadPoolImpl::GetBackgroundThreads()
{
  return impl_->GetBackgroundThreads();
}

uint32_t ThreadPoolImpl::GetQueueLen() const
{
  return impl_->GetQueueLen();
}

void ThreadPoolImpl::WaitForJobsAndJoinAllThreads()
{
  impl_->JoinThreads(true);
}

void ThreadPoolImpl::LowerIOPriority()
{
  impl_->LowerIOPriority();
}

void ThreadPoolImpl::LowerCPUPriority()
{
  impl_->LowerCPUPriority();
}

void ThreadPoolImpl::IncBackgroundThreadsIfNeeded(int32_t num)
{
  impl_->SetBackgroundThreadsInternal(num, false);
}

void ThreadPoolImpl::SubmitJob(const std::function<void()> &job)
{
  auto copy(job);
  impl_->Submit(std::move(copy), std::function<void()>(), nullptr);
}

void ThreadPoolImpl::SubmitJob(std::function<void()> &&job)
{
  impl_->Submit(std::move(job), std::function<void()>(), nullptr);
}

void ThreadPoolImpl::Schedule(void (*function)(void *arg1), void *arg,
                              void *tag, void (*unschedFunction)(void *arg))
{

  std::function<void()> fn = [arg, function] { function(arg); };

  std::function<void()> unfn;
  if (unschedFunction != nullptr)
  {
    auto uf = [arg, unschedFunction] { unschedFunction(arg); };
    unfn = std::move(uf);
  }

  impl_->Submit(std::move(fn), std::move(unfn), tag);
}

int32_t ThreadPoolImpl::UnSchedule(void *arg)
{
  return impl_->UnSchedule(arg);
}

void ThreadPoolImpl::SetHostEnv(Env *env) { impl_->SetHostEnv(env); }

Env *ThreadPoolImpl::GetHostEnv() const { return impl_->GetHostEnv(); }

// Return the thread priority.
// This would allow its member-thread to know its priority.
Env::Priority ThreadPoolImpl::GetThreadPriority() const
{
  return impl_->GetThreadPriority();
}

// Set the thread priority.
void ThreadPoolImpl::SetThreadPriority(Env::Priority priority)
{
  impl_->SetThreadPriority(priority);
}

////////////////// PosixFixedThreadPool //////////////////////

PosixFixedThreadPool *PosixFixedThreadPool::NewFixedThreadPool(int32_t num_threads, bool eager_init, void *attr)
{
  return new PosixFixedThreadPool(num_threads, eager_init, attr);
}

PosixFixedThreadPool::~PosixFixedThreadPool()
{
  MutexLock ml(&mu_);
  // Wait until all scheduled work has finished and then destroy the
  // set of threads.
  // Wakeup all waiters.
  shutting_down_ = true;
  bg_cv_.broadcast();
  // wait for num_pool_threads_
  while (num_pool_threads_ != 0)
  {
    bg_cv_.wait();
  }
  assert(bgthreads_.size() == 0);
  bgthreads_.clear();
}

string PosixFixedThreadPool::ToDebugString()
{
  char tmp[100];
  snprintf(tmp, sizeof(tmp), "POSIX fixed thread pool: num_threads=%d",
           max_threads_);
  return tmp;
}

void PosixFixedThreadPool::InitPool(void *attr)
{
  mu_.assertHeld();
  while (num_pool_threads_ < max_threads_)
  {
    ++num_pool_threads_;
    pthread_t t;
    PthreadCall(
        "pthread_create",
        ::pthread_create(&t, NULL, BGWrapper, this));
    bgthreads_.push_back(t);
  }
}

void PosixFixedThreadPool::Schedule(void (*function)(void *), void *arg, const string &name)
{
  MutexLock ml(&mu_);
  if (shutting_down_)
    return;
  InitPool(NULL); // Start background threads if necessary

  // If the queue is currently empty, the background threads
  // may be waiting.
  if (queue_.empty())
    bg_cv_.broadcast();

  // Add to priority queue
  BGItem work;
  work.function = function;
  work.arg = arg;
  work.name = name;
  queue_.push_back(work);
}

void PosixFixedThreadPool::BGThread()
{
  void (*function)(void *) = NULL;
  void *arg;

  pthread_t th = ::pthread_self();

  while (true)
  {
    {
      MutexLock ml(&mu_);
      // Wait until there is an item that is ready to run
      while (!shutting_down_ && (paused_ || queue_.empty()))
      {
        bg_cv_.wait();
      }
      if (shutting_down_)
      {
        assert(num_pool_threads_ > 0);
        std::vector<pthread_t>::iterator it = std::find(bgthreads_.begin(), bgthreads_.end(), th);
        if (it != bgthreads_.end())
        {
          bgthreads_.erase(it);
        }
        --num_pool_threads_;
        bg_cv_.broadcast();
        return;
      }

      assert(!queue_.empty());
      function = queue_.front().function;
      arg = queue_.front().arg;
      queue_.pop_front();
    }

    assert(function != NULL);
    function(arg);
  }
}

void PosixFixedThreadPool::Resume()
{
  MutexLock ml(&mu_);
  paused_ = false;
  bg_cv_.broadcast();
}

void PosixFixedThreadPool::Pause()
{
  MutexLock ml(&mu_);
  paused_ = true;
}

//////////// BGThreadPool ///////////////////

bool BGThreadPool::Start()
{
  MutexLock lock(&mutex_);
  if (tids_.size())
  {
    return false;
  }
  stop_ = false;
  for (int32_t i = 0; i < threads_num_; i++)
  {
    pthread_t tid;
    PthreadCall("pthread_create",
                pthread_create(&tid, NULL, ThreadWrapper, this));
    tids_.push_back(tid);
  }
  return true;
}

bool BGThreadPool::Stop(bool wait)
{
  if (wait)
  {
    while (pending_num_ > 0)
    {
      ::usleep(10000);
    }
  }

  {
    MutexLock lock(&mutex_);
    stop_ = true;
    work_cv_.broadcast();
  }
  for (uint32_t i = 0; i < tids_.size(); i++)
  {
    ::pthread_join(tids_[i], nullptr);
  }
  tids_.clear();
  return true;
}

void BGThreadPool::AddTask(const Task &task)
{
  MutexLock lock(&mutex_);
  if (stop_)
    return;
  queue_.push_back(BGItem(0, NowMonotonicMicros(), task));
  ++pending_num_;
  work_cv_.signal();
}

void BGThreadPool::AddPriorityTask(const Task &task)
{
  MutexLock lock(&mutex_);
  if (stop_)
    return;
  queue_.push_front(BGItem(0, NowMonotonicMicros(), task));
  ++pending_num_;
  work_cv_.signal();
}

int64_t BGThreadPool::DelayTask(int64_t delay, const Task &task)
{
  MutexLock lock(&mutex_);
  if (stop_)
    return 0;
  int64_t now_time = NowMonotonicMicros();
  int64_t exe_time = now_time + delay * 1000;
  BGItem bg_item(++last_task_id_, exe_time, task); // id > 1
  time_queue_.push(bg_item);
  latest_[bg_item.id] = bg_item;
  work_cv_.signal();
  return bg_item.id;
}

bool BGThreadPool::CancelTask(int64_t task_id, bool non_block, bool *is_running)
{
  if (task_id == 0)
  { // not delay task
    if (is_running != nullptr)
    {
      *is_running = false;
    }
    return false;
  }
  while (1)
  {
    {
      MutexLock lock(&mutex_);
      if (running_task_id_ != task_id)
      {
        BGMap::iterator it = latest_.find(task_id);
        if (it == latest_.end())
        {
          if (is_running != nullptr)
          {
            *is_running = false;
          }
          return false;
        }
        latest_.erase(it); // cancel task
        return true;
      }
      else if (non_block)
      { // already running
        if (is_running != nullptr)
        {
          *is_running = true;
        }
        return false;
      }
      // else block
    }
    SleepForNanos(100000);
  }
}

string BGThreadPool::ProfilingLog()
{
  int64_t schedule_cost_sum;
  int64_t schedule_count;
  int64_t task_cost_sum;
  int64_t task_count;
  {
    MutexLock lock(&mutex_);
    schedule_cost_sum = schedule_cost_sum_;
    schedule_cost_sum_ = 0;
    schedule_count = schedule_count_;
    schedule_count_ = 0;
    task_cost_sum = task_cost_sum_;
    task_cost_sum_ = 0;
    task_count = task_count_;
    task_count_ = 0;
  }
  std::stringstream ss;
  ss << (schedule_count == 0 ? 0 : schedule_cost_sum / schedule_count / 1000)
     << " " << (task_count == 0 ? 0 : task_cost_sum / task_count / 1000)
     << " " << task_count;
  return ss.str();
}

void BGThreadPool::ThreadProc()
{
  while (true)
  {
    Task task;
    MutexLock lock(&mutex_);

    while (time_queue_.empty() && queue_.empty() && !stop_)
    {
      work_cv_.wait();
    }
    if (stop_)
    {
      break;
    }

    // Timer task
    if (!time_queue_.empty())
    {
      int64_t now_time = NowMonotonicMicros();
      BGItem bg_item = time_queue_.top();
      int64_t wait_time = (bg_item.exe_time - now_time) / 1000; // in ms
      if (wait_time <= 0)
      {
        time_queue_.pop();
        BGMap::iterator it = latest_.find(bg_item.id);
        if (it != latest_.end() && it->second.exe_time == bg_item.exe_time)
        {
          schedule_cost_sum_ += now_time - bg_item.exe_time;
          schedule_count_++;
          task = bg_item.task;
          latest_.erase(it);
          running_task_id_ = bg_item.id;
          {
            mutex_.unlock();
            task(); // not use mutex in task, may call threadpool funcs
            task_cost_sum_ += NowMonotonicMicros() - now_time;
            task_count_++;
            mutex_.lock();
          }
          running_task_id_ = 0;
        }
        continue;
      }
      else if (queue_.empty() && !stop_)
      {
        work_cv_.waitFor(wait_time);
        continue;
      }
    }

    // Normal task
    if (!queue_.empty())
    {
      task = queue_.front().task;
      int64_t exe_time = queue_.front().exe_time;
      queue_.pop_front();
      --pending_num_;
      int64_t start_time = NowMonotonicMicros();
      schedule_cost_sum_ += start_time - exe_time;
      schedule_count_++;
      mutex_.unlock();
      task();
      int64_t finish_time = NowMonotonicMicros();
      task_cost_sum_ += finish_time - start_time;
      task_count_++;
      mutex_.lock();
    }
  }
}

} // namespace util
} // namespace mycc
