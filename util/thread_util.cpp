
#include "thread_util.h"
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>

namespace mycc
{
namespace util
{

struct ThreadData
{
  void (*user_function)(void *);
  void *user_arg;
  string name;
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
  ::prctl(PR_SET_NAME, (data->name).c_str());
  data->user_function(data->user_arg);
  delete data;
  return NULL;
}

} // namespace

void *PosixThread::StartProcWrapper(void *arg)
{
  PosixThread *pth = reinterpret_cast<PosixThread *>(arg);
  ::prctl(PR_SET_NAME, pth->getName());
  pth->user_proc_();
  return nullptr;
}

PosixThread::PosixThread(void (*func)(void *arg), void *arg, const string &name)
    : name_(name),
      started_(NULL),
      function_(func),
      arg_(arg)
{
  memset(&tid_, 0, sizeof(tid_));
}

PosixThread::PosixThread(std::function<void()> func, const string &name)
    : name_(name),
      started_(NULL),
      isProc_(true),
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

  if (isProc_)
  {
    bool ret = PthreadCall("pthread_create",
                           pthread_create(&tid_, NULL, &PosixThread::StartProcWrapper, this));
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
    bool ret = PthreadCall("pthread_create",
                           pthread_create(&tid_, NULL, &StartPthreadWrapper, data));
    if (!ret)
    {
      return false;
    }
  }

  started_.Release_Store(this);
  return true;
}

bool PosixThread::join()
{
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