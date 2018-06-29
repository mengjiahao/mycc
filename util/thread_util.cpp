
#include "thread_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

namespace mycc
{
namespace util
{

namespace CurrentThread
{
__thread int t_cachedTid = 0;
__thread const char *t_threadName = "unknown";
} // namespace CurrentThread

struct ThreadData
{
  void (*user_function)(void *);
  void *user_arg;
  string name;
  pthread_t tid;
  CountDownLatch *latch;
};

namespace
{ // anonymous namespace

bool PthreadCall(const char *label, int32_t result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    return false;
  }
  return true;
}

void *StartPthreadWrapper(void *arg)
{
  ThreadData *data = reinterpret_cast<ThreadData *>(arg);
  (data->latch)->countDown();
  //::prctl(PR_SET_NAME, (data->name).c_str());
  pthread_setname_np(data->tid, (data->name).c_str());
  data->user_function(data->user_arg);
  delete data;
  return NULL;
}

} // namespace

void InitOnce(OnceType *once, void (*initializer)())
{
  PthreadCall("pthread_once", pthread_once(once, initializer));
}

pid_t GetTID()
{
  pid_t tid = syscall(__NR_gettid);
  return tid;
}

uint64_t PthreadId()
{
  pthread_t tid = pthread_self();
  uint64_t thread_id = 0;
  memcpy(&thread_id, &tid, std::min(sizeof(thread_id), sizeof(tid)));
  return thread_id;
}

void *PosixThread::StartProcWrapper(void *arg)
{
  PosixThread *pth = reinterpret_cast<PosixThread *>(arg);
  CountDownLatch &latch = pth->latch();
  latch.countDown();
  //::prctl(PR_SET_NAME, (pth->name()).c_str());
  pthread_setname_np(pth->tid(), (pth->name()).c_str());
  try
  {
    pth->user_proc_();
  }
  catch (...)
  {
    CurrentThread::t_threadName = "Crashed";
    fprintf(stderr, "unknown exception caught in Thread %s\n", (pth->name()).c_str());
    throw; // rethrow
  }
  return nullptr;
}

PosixThread::PosixThread(void (*func)(void *arg), void *arg, const string &name)
    : name_(name),
      latch_(1),
      started_(NULL),
      function_(func),
      arg_(arg),
      isStdFunction_(false)
{
  memset(&tid_, 0, sizeof(tid_));
}

PosixThread::PosixThread(std::function<void()> func, const string &name)
    : name_(name),
      latch_(1),
      started_(NULL),
      isStdFunction_(true),
      user_proc_(func)
{
  memset(&tid_, 0, sizeof(tid_));
}

bool PosixThread::isStarted()
{
  return (NULL != started_.Acquire_Load());
}

bool PosixThread::start()
{
  if (isStarted())
  {
    return false;
  }

  if (isStdFunction_)
  {
    bool ret = PthreadCall("pthread_create",
                           pthread_create(&tid_, NULL, &StartProcWrapper, this));
    if (!ret)
    {
      return false;
    }
  }
  else
  {
    ThreadData *data = new ThreadData();
    data->user_function = function_;
    data->user_arg = arg_;
    data->name = name_;
    data->tid = tid_;
    data->latch = &latch_;
    bool ret = PthreadCall("pthread_create",
                           pthread_create(&tid_, NULL, &StartPthreadWrapper, data));
    if (!ret)
    {
      delete data; // or no delete?
      return false;
    }
  }

  started_.Release_Store(this);
  return true;
}

bool PosixThread::startForLaunch()
{
  if (!start())
    return false;
  latch_.wait();
  return true;
}

bool PosixThread::join()
{
  if (amSelf())
    return false;
  if (isStarted())
  {
    return PthreadCall("pthread_join", pthread_join(tid_, NULL));
  }
  return true;
}

bool PosixThread::kill(int32_t signal_val)
{
  if (isStarted())
  {
    return PthreadCall("pthread_kill", pthread_kill(tid_, signal_val));
  }
  return true;
}

bool PosixThread::detach()
{
  if (isStarted())
  {
    return PthreadCall("pthread_detach", pthread_detach(tid_));
  }
  return true;
}

} // namespace util
} // namespace mycc