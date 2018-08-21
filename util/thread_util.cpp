
#include "thread_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include "atomic_util.h"
#include "math_util.h"
#include "string_util.h"
#include "time_util.h"

namespace mycc
{
namespace util
{

namespace
{ // anonymous namespace

bool PthreadCall(const char *label, int result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    return false;
  }
  return true;
}

} // namespace

namespace CurrentThread
{
__thread int t_cachedTid = 0;
__thread const char *t_threadName = "unknown";
} // namespace CurrentThread

void *StartPthreadWrapper(void *arg)
{
  assert(arg);
  // pthread_cleanup_push(&cleanup, arg);
  PosixThread *data = reinterpret_cast<PosixThread *>(arg);
  CountDownLatch *run_latch = data->run_latch();
  CountDownLatch *stop_latch = data->stop_latch();

  run_latch->countDown();
  stop_latch->reset(1);

  try
  {
    if (data->is_std_func())
    {
      PosixThread::UserStdFunctionType user_std_func = data->user_std_func();
      user_std_func();
    }
    else
    {
      PosixThread::UserFunctionType user_func = data->user_func();
      if (user_func)
      {
        user_func(data->user_arg());
      }
    }
  }
  catch (...)
  {
    //CurrentThread::t_threadName = "Crashed";
    fprintf(stderr, "unknown exception caught in Thread " PRIu64_FORMAT "\n",
            (uint64_t)data->thread_id());
    throw; // rethrow
  }

  data->setIsRunning(false);
  stop_latch->countDown();
  // pthread_cleanup_pop(1);
  return NULL;
}

void *StartPthreadPeriodicWrapper(void *arg)
{
  assert(arg);
  // pthread_cleanup_push(&cleanup, arg);
  PosixThread *data = reinterpret_cast<PosixThread *>(arg);
  CountDownLatch *run_latch = data->run_latch();
  CountDownLatch *stop_latch = data->stop_latch();

  run_latch->countDown();
  stop_latch->reset(1);

  while (data->isRunning())
  {
    try
    {
      if (data->is_std_func())
      {
        PosixThread::UserStdFunctionType user_std_func = data->user_std_func();
        user_std_func();
      }
      else
      {
        PosixThread::UserFunctionType user_func = data->user_func();
        if (user_func)
        {
          user_func(data->user_arg());
        }
      }
    }
    catch (...)
    {
      //CurrentThread::t_threadName = "Crashed";
      fprintf(stderr, "unknown exception caught in Thread " PRIu64_FORMAT "\n",
              (uint64_t)data->thread_id());
      throw; // rethrow
    }
  }

  data->setIsRunning(false);
  stop_latch->countDown();
  // pthread_cleanup_pop(1);
  return NULL;
}

void PosixThread::ClearPthreadId(pthread_t *tid)
{
  memset(tid, 0, sizeof(pthread_t));
}

uint64_t PosixThread::GetTID()
{
  uint64_t tid = syscall(__NR_gettid);
  return tid;
}

uint64_t PosixThread::PthreadIdInt()
{
  pthread_t tid = ::pthread_self();
  return (uint64_t)tid;
}

pthread_t PosixThread::ThisPthreadId()
{
  return ::pthread_self();
}

bool PosixThread::IsPthreadIdEqual(const pthread_t &a, const pthread_t &b)
{
  return ::pthread_equal(a, b) != 0;
}

bool PosixThread::IsValidPthreadId(const pthread_t &tid)
{
  pthread_t t;
  ClearPthreadId(&t);
  return !IsPthreadIdEqual(tid, t);
}

void PosixThread::SetThisThreadName(const string &name)
{
  ::prctl(PR_SET_NAME, name.c_str(), 0, 0, 0);
  //::pthread_setname_np(pthread_self(), name.c_str());
}

bool PosixThread::DetachThisThread()
{
  bool ret = false;
  ret = PthreadCall("pthread_detach", ::pthread_detach(::pthread_self()));
  return ret;
}

void PosixThread::ExitThisThread()
{
  ::pthread_exit(NULL);
}

void PosixThread::YieldThisThread()
{
  ::sched_yield();
}

void PosixThread::UsleepThisThread(uint32_t micros)
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

void PosixThread::SleepThisThread(uint32_t secs)
{
  ::sleep(secs);
}

cpu_set_t PosixThread::GetThisThreadCpuAffinity()
{
  cpu_set_t mask;
  CPU_ZERO(&mask);
  ::sched_getaffinity(0, sizeof(mask), &mask);
  return mask;
}

bool PosixThread::SetThisThreadCpuAffinity(cpu_set_t mask)
{
  if (!::sched_setaffinity(0, sizeof(mask), &mask))
  {
    return false;
  }
  /* guaranteed to take effect immediately */
  ::sched_yield();
  return true;
}

bool PosixThread::SetCpuMask(int32_t cpu_id, cpu_set_t mask)
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

bool PosixThread::IsPthreadRunning(const pthread_t &tid)
{
  if (!IsValidPthreadId(tid))
  {
    return false;
  }
  int ret = pthread_kill(tid, 0);
  if (0 == ret)
    return true;
  return false;
}

PosixThread::PosixThread(std::function<void()> func)
    : is_std_func_(true),
      user_func_(NULL),
      user_arg_(NULL),
      user_std_func_(func),
      joinable_(true),
      is_running_(false),
      run_latch_(0),
      stop_latch_(0)
{
  ClearPthreadId(&tid_);
}

PosixThread::PosixThread(void (*func)(void *arg), void *arg)
    : is_std_func_(false),
      user_func_(func),
      user_arg_(arg),
      joinable_(true),
      is_running_(false),
      run_latch_(0),
      stop_latch_(0)
{
  ClearPthreadId(&tid_);
}

string PosixThread::toString()
{
  string result;
  util::StringFormatTo(&result, "PosixThread@%p { tid:" PRIu64_FORMAT " }",
                       this, (uint64_t)tid_);
  return result;
}

bool PosixThread::isRunning()
{
  return AtomicLoadN<bool>(&is_running_);
}

void PosixThread::setIsRunning(bool is_running)
{
  AtomicStoreN<bool>(&is_running_, is_running);
}

bool PosixThread::isStarted()
{
  // Thread IDs are guaranteed to be unique only within a process.
  // A thread ID may be reused after a terminated thread has been joined,
  // or a detached thread has terminated.
  return IsValidPthreadId(tid_);
}

bool PosixThread::start()
{
  bool ret = false;
  if (isStarted())
  {
    return false;
  }
  // The child thread will inherit our signal mask.  Set our signal mask to
  // the set of signals we want to block.  (It's ok to block signals more
  // signals than usual for a little while-- they will just be delivered to
  // another thread or delieverd to this thread later.)
  // sigset_t old_sigset;
  // int to_block[] = {SIGPIPE, 0};
  // BlockSignals(to_block, &old_sigset);
  // pthread_create()...
  // RestoreSigset(&old_sigset);
  run_latch_.reset(1);

  ret = PthreadCall("pthread_create",
                    pthread_create(&tid_, NULL, &StartPthreadWrapper, this));
  if (ret)
  {
    // it is hard to know when pthread_create thread is started to run.
    setIsRunning(true);
    return true;
  }
  else
  {
    run_latch_.reset(0);
  }
  return false;
}

bool PosixThread::startPeriodic()
{
  bool ret = false;
  if (isStarted())
  {
    return false;
  }
  run_latch_.reset(1);

  ret = PthreadCall("pthread_create",
                    pthread_create(&tid_, NULL, &StartPthreadPeriodicWrapper, this));
  if (ret)
  {
    // it is hard to know when pthread_create thread is started to run.
    setIsRunning(true);
    return true;
  }
  else
  {
    run_latch_.reset(0);
  }
  return false;
}

bool PosixThread::cancel()
{
  setIsRunning(false);
  return true;
}

bool PosixThread::join()
{
  bool ret = false;
  if (amSelf())
  {
    return false;
  }
  if (!isStarted() || !joinable())
  {
    return false;
  }
  ret = PthreadCall("pthread_join", pthread_join(tid_, NULL));
  if (ret)
  {
    joinable_ = false;
  }
  return ret;
}

bool PosixThread::detach()
{
  bool ret = false;
  if (!isStarted() || !joinable())
  {
    return false;
  }
  ret = PthreadCall("pthread_detach", pthread_detach(tid_));
  if (ret)
  {
    joinable_ = false;
  }
  return ret;
}

bool PosixThread::kill(int32_t signal_val)
{
  // if thread is not running, send signal will cause crash.
  bool ret = false;
  if (!isRunning())
  {
    return false;
  }
  ret = PthreadCall("pthread_kill", pthread_kill(tid_, signal_val));
  return ret;
}

bool PosixThread::unsafeExit()
{
  return (0 == pthread_cancel(tid_));
}

void PosixThread::waitUntilRun()
{
  run_latch_.wait();
}

void PosixThread::waitUntilStop()
{
  stop_latch_.wait();
}

////////////////////////// BGThread //////////////////////////////

void BGThread::Schedule(void (*function)(void *), void *arg)
{
  MutexLock l(&mu_);
  while (queue_.size() >= full_ && !should_stop())
  {
    wsignal_.wait();
  }
  if (!should_stop())
  {
    queue_.push(BGItem(function, arg));
    rsignal_.signal();
  }
}

void BGThread::QueueSize(uint64_t *pri_size, uint64_t *qu_size)
{
  MutexLock l(&mu_);
  *pri_size = timer_queue_.size();
  *qu_size = queue_.size();
}

void BGThread::QueueClear()
{
  MutexLock l(&mu_);
  std::queue<BGItem>().swap(queue_);
  std::priority_queue<TimerItem>().swap(timer_queue_);
}

void *BGThread::ThreadMain()
{
  while (!should_stop())
  {
    mu_.lock();
    while (queue_.empty() && timer_queue_.empty() && !should_stop())
    {
      rsignal_.wait();
    }
    if (should_stop())
    {
      mu_.unlock();
      break;
    }

    if (!timer_queue_.empty())
    {
      uint64_t unow = NowSystimeMicros();
      TimerItem timer_item = timer_queue_.top();
      if (unow / 1000 >= timer_item.exec_time / 1000)
      {
        void (*function)(void *) = timer_item.function;
        void *arg = timer_item.arg;
        timer_queue_.pop();
        mu_.unlock();
        (*function)(arg);
        continue;
      }
      else if (queue_.empty() && !should_stop())
      {
        rsignal_.waitFor((timer_item.exec_time - unow) / 1000);
        mu_.unlock();
        continue;
      }
    }

    if (!queue_.empty())
    {
      void (*function)(void *) = queue_.front().function;
      void *arg = queue_.front().arg;
      queue_.pop();
      wsignal_.signal();
      mu_.unlock();
      (*function)(arg);
    }
  }
  return NULL;
}

void BGThread::DelaySchedule(
    uint64_t timeout_ms, void (*function)(void *), void *arg)
{
  uint64_t unow = NowSystimeMicros();
  uint64_t exec_time;
  exec_time = unow + timeout_ms * 1000;

  mu_.lock();
  timer_queue_.push(TimerItem(exec_time, function, arg));
  rsignal_.signal();
  mu_.unlock();
}

////////////////////// StdThreadGroup //////////////////////////

StdThreadGroup::StdThreadGroup(const std::function<void()> &callback,
                               uint64_t count)
{
  Add(callback, count);
}

StdThreadGroup::~StdThreadGroup()
{
  for (uint64_t i = 0; i < m_threads.size(); ++i)
  {
    if (m_threads[i].joinable())
    {
      m_threads[i].join();
    }
  }
  m_threads.clear();
}

void StdThreadGroup::Add(const std::function<void()> &callback,
                         uint64_t count)
{
  for (uint64_t i = 0; i < count; ++i)
  {
    m_threads.emplace_back(callback);
  }
}

void StdThreadGroup::Join()
{
  for (uint64_t i = 0; i < m_threads.size(); ++i)
  {
    if (m_threads[i].joinable())
    {
      m_threads[i].join();
    }
  }
}

uint64_t StdThreadGroup::Size() const
{
  return m_threads.size();
}

} // namespace util
} // namespace mycc