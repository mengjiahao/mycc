
#include "thread_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include "math_util.h"

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

bool PthreadCall(const char *label, int result)
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

uint64_t PosixThread::GetTID()
{
  uint64_t tid = syscall(__NR_gettid);
  return tid;
}

uint64_t PosixThread::PthreadIntId()
{
  pthread_t tid = pthread_self();
  uint64_t thread_id = 0;
  memcpy(&thread_id, &tid, MATH_MIN(sizeof(thread_id), sizeof(tid)));
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
      function_(func),
      arg_(arg),
      isStdFunction_(false)
{
  memset(&tid_, 0, sizeof(tid_));
  pthread_attr_init(&attr_);
}

PosixThread::PosixThread(std::function<void()> func, const string &name)
    : name_(name),
      latch_(1),
      isStdFunction_(true),
      user_proc_(func)
{
  memset(&tid_, 0, sizeof(tid_));
  pthread_attr_init(&attr_);
}

PosixThread::~PosixThread()
{
  pthread_attr_destroy(&attr_);
}

bool PosixThread::isStarted()
{
  return ((uint64_t)tid_) != 0;
}

void PosixThread::setAttr(uint64_t stack_size, bool joinable)
{
  if (!stack_size)
  {
    pthread_attr_setstacksize(&attr_, stack_size);
  }
  if (!joinable)
  {
    pthread_attr_setdetachstate(&attr_, PTHREAD_CREATE_DETACHED);
  }
}

bool PosixThread::start()
{
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

  bool ret = false;
  if (isStdFunction_)
  {
    ret = PthreadCall("pthread_create",
                      pthread_create(&tid_, NULL, &StartProcWrapper, this));
  }
  else
  {
    ThreadData *data = new ThreadData();
    data->user_function = function_;
    data->user_arg = arg_;
    data->name = name_;
    data->tid = tid_;
    data->latch = &latch_;
    ret = PthreadCall("pthread_create",
                      pthread_create(&tid_, NULL, &StartPthreadWrapper, data));
    if (!ret)
    {
      delete data; // or no delete?
    }

    return ret;
  }

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
  tid_ = 0;
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