
#include "locks_util.h"
#include <errno.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include "time_util.h"

namespace mycc
{
namespace util
{

namespace
{ // anonymous namespace

static int PthreadCall(const char *label, int result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthreadcall %s: %s\n", label, strerror(result));
    abort();
  }
  return result;
}

} // namespace

Mutex::Mutex()
{
#ifdef MUTEX_DEBUG
  locked_ = false;
  owner_ = 0;
#endif

  PthreadCall("init mutex default", pthread_mutex_init(&mu_, nullptr));
}

Mutex::Mutex(bool adaptive)
{
#ifdef MUTEX_DEBUG
  locked_ = false;
  owner_ = 0;
#endif

  if (!adaptive)
  {
    // prevent called by the same thread.
    pthread_mutexattr_t attr;
    PthreadCall("init mutex attr", pthread_mutexattr_init(&attr));
    PthreadCall("set mutex attr errorcheck", pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK));
    PthreadCall("init mutex errorcheck", pthread_mutex_init(&mu_, &attr));
    PthreadCall("destroy mutex attr errorcheck", pthread_mutexattr_destroy(&attr));
  }
  else
  {
    pthread_mutexattr_t mutex_attr;
    PthreadCall("init mutex attr", pthread_mutexattr_init(&mutex_attr));
    PthreadCall("set mutex attr adaptive_np", pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ADAPTIVE_NP));
    PthreadCall("init mutex adaptive_np", pthread_mutex_init(&mu_, &mutex_attr));
    PthreadCall("destroy mutex attr adaptive_np", pthread_mutexattr_destroy(&mutex_attr));
  }
}

Mutex::~Mutex() { PthreadCall("destroy mutex", pthread_mutex_destroy(&mu_)); }

void Mutex::lock()
{
  PthreadCall("lock mutex", pthread_mutex_lock(&mu_));
  afterLock();
}

bool Mutex::tryLock()
{
  int32_t ret = pthread_mutex_trylock(&mu_);
  switch (ret)
  {
  case 0:
  {
    afterLock();
    return true;
  }
  case EBUSY:
    return false;
  case EINVAL:
    abort();
  case EAGAIN:
    abort();
  case EDEADLK:
    abort();
  default:
    abort();
  }
  return false;
}

bool Mutex::timedLock(int64_t time_ms)
{
  struct timespec ts;
  MakeTimeoutMs(&ts, time_ms);
  int32_t ret = pthread_mutex_timedlock(&mu_, &ts);
  switch (ret)
  {
  case 0:
  {
    afterLock();
    return true;
  }
  case ETIMEDOUT:
    return false;
  case EAGAIN:
    abort();
  case EDEADLK:
    abort();
  case EINVAL:
    abort();
  default:
    abort();
  }
  return false;
}

void Mutex::unlock()
{
  beforeUnlock();
  PthreadCall("unlock mutex", pthread_mutex_unlock(&mu_));
}

bool Mutex::isLocked()
{
  int32_t ret = pthread_mutex_trylock(&mu_);
  if (0 == ret)
    unlock();
  return 0 != ret;
}

void Mutex::assertHeld()
{
#ifdef MUTEX_DEBUG
  assert(locked_);
  if (0 == pthread_equal(owner_, pthread_self()))
  {
    fprintf(stderr, "mutex is held by two calling threads %" PRIu64 ":%" PRIu64 "\n",
            (uint64_t)owner_, (uint64_t)pthread_self());
    abort();
  }
#endif
}

void Mutex::afterLock()
{
#ifdef MUTEX_DEBUG
  locked_ = true;
  owner_ = pthread_self();
#endif
}

void Mutex::beforeUnlock()
{
#ifdef MUTEX_DEBUG
  locked_ = false;
  owner_ = 0;
#endif
}

CondVar::CondVar(Mutex *mu)
    : mu_(mu)
{
  PthreadCall("init cv", pthread_cond_init(&cv_, nullptr));
}

CondVar::~CondVar() { PthreadCall("destroy cv", pthread_cond_destroy(&cv_)); }

void CondVar::wait()
{
#ifdef MUTEX_DEBUG
  mu_->beforeUnlock();
#endif
  PthreadCall("wait cv", pthread_cond_wait(&cv_, &mu_->mu_));
#ifdef MUTEX_DEBUG
  mu_->afterLock();
#endif
}

bool CondVar::timedWait(int64_t abs_time_ms)
{
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(abs_time_ms / 1000);
  ts.tv_nsec = static_cast<suseconds_t>((abs_time_ms % 1000) * 1000000);

#ifdef MUTEX_DEBUG
  mu_->beforeUnlock();
#endif
  int32_t err = pthread_cond_timedwait(&cv_, &mu_->mu_, &ts);
#ifdef MUTEX_DEBUG
  mu_->afterLock();
#endif

  if (err == ETIMEDOUT)
  {
    return true;
  }
  if (err != 0)
  {
    PthreadCall("timedwait cv", err);
  }
  return false;
}

bool CondVar::timedWaitAbsolute(const struct timespec &absolute_time)
{
#ifdef MUTEX_DEBUG
  mu_->beforeUnlock();
#endif
  int32_t status = pthread_cond_timedwait(&cv_, &mu_->mu_, &absolute_time);
#ifdef MUTEX_DEBUG
  mu_->afterLock();
#endif

  if (status == ETIMEDOUT)
  {
    return false;
  }
  assert(status == 0);
  return true;
}

bool CondVar::timedWaitRelative(int64_t time_ms)
{
  // pthread_cond_timedwait api use absolute API
  // so we need gettimeofday + relative_time
  struct timespec ts;
  MakeTimeoutMs(&ts, time_ms);

#ifdef MUTEX_DEBUG
  mu_->beforeUnlock();
#endif
  int32_t ret = pthread_cond_timedwait(&cv_, &mu_->mu_, &ts);
#ifdef MUTEX_DEBUG
  mu_->afterLock();
#endif

  return (ret == 0);
}

// Calls timedwait with a relative, instead of an absolute, timeout.
bool CondVar::timedWaitRelative(const struct timespec &relative_time)
{
  struct timespec absolute;
  // clock_gettime would be more convenient, but that needs librt
  // int status = clock_gettime(CLOCK_REALTIME, &absolute);
  struct timeval tv;
  int32_t status = gettimeofday(&tv, NULL);
  assert(status == 0);
  absolute.tv_sec = tv.tv_sec + relative_time.tv_sec;
  absolute.tv_nsec = tv.tv_usec * 1000 + relative_time.tv_nsec;

  return timedWaitAbsolute(absolute);
}

void CondVar::signal()
{
  PthreadCall("signal cv", pthread_cond_signal(&cv_));
}

void CondVar::signalAll()
{
  PthreadCall("signalAll cv", pthread_cond_broadcast(&cv_));
}

RWMutex::RWMutex()
{
  PthreadCall("init rwmutex", pthread_rwlock_init(&mu_, nullptr));
}

RWMutex::~RWMutex() { PthreadCall("destroy rwmutex", pthread_rwlock_destroy(&mu_)); }

void RWMutex::readLock() { PthreadCall("read lock", pthread_rwlock_rdlock(&mu_)); }

/*!
    Attempts to lock for reading. If the lock was obtained, this
    function returns true, otherwise it returns false instead of
    waiting for the lock to become available, i.e. it does not block.
    The lock attempt will fail if another thread has locked for writing.
    If the lock was obtained, the lock must be unlocked with unlock()
    before another thread can successfully lock it.
    \sa unlock() lockForRead()
*/
bool RWMutex::tryReadLock()
{
  int32_t ret = pthread_rwlock_tryrdlock(&mu_);
  switch (ret)
  {
  case 0:
    return true;
  case EBUSY:
    return false;
  case EINVAL:
    abort();
  case EAGAIN:
    abort();
  case EDEADLK:
    abort();
  default:
    abort();
  }
  return false;
}

void RWMutex::writeLock() { PthreadCall("write lock", pthread_rwlock_wrlock(&mu_)); }

/*!
    Attempts to lock for writing. If the lock was obtained, this
    function returns true; otherwise, it returns false immediately.
    The lock attempt will fail if another thread has locked for
    reading or writing.
    If the lock was obtained, the lock must be unlocked with unlock()
    before another thread can successfully lock it.
    \sa unlock() lockForWrite()
*/
bool RWMutex::tryWriteLock()
{
  int32_t ret = pthread_rwlock_trywrlock(&mu_);
  switch (ret)
  {
  case 0:
    return true;
  case EBUSY:
    return false;
  case EINVAL:
    abort();
  case EAGAIN:
    abort();
  case EDEADLK:
    abort();
  default:
    abort();
  }
  return false;
}

/*!
    Unlocks the lock.
    Attempting to unlock a lock that is not locked is an error, and will result
    in program termination.
    \sa lockForRead() lockForWrite() tryLockForRead() tryLockForWrite()
*/
void RWMutex::unlock()
{
  PthreadCall("unlock rwmutex", pthread_rwlock_unlock(&mu_));
}

void RWMutex::readUnlock() { PthreadCall("read unlock", pthread_rwlock_unlock(&mu_)); }

void RWMutex::writeUnlock() { PthreadCall("write unlock", pthread_rwlock_unlock(&mu_)); }

RefMutex::RefMutex()
{
  refs_ = 0;
  PthreadCall("init refmutex", pthread_mutex_init(&mu_, nullptr));
}

RefMutex::~RefMutex()
{
  PthreadCall("destroy refmutex", pthread_mutex_destroy(&mu_));
}

void RefMutex::ref()
{
  ++refs_;
}
void RefMutex::unref()
{
  --refs_;
  if (refs_ == 0)
  {
    delete this;
  }
}

void RefMutex::lock()
{
  PthreadCall("lock refmutex", pthread_mutex_lock(&mu_));
}

void RefMutex::unlock()
{
  PthreadCall("unlock refmutex", pthread_mutex_unlock(&mu_));
}

CondLock::CondLock()
{
  PthreadCall("init condlock", pthread_mutex_init(&mutex_, nullptr));
}

CondLock::~CondLock()
{
  PthreadCall("destroy condlock", pthread_mutex_unlock(&mutex_));
}

void CondLock::lock()
{
  PthreadCall("lock condlock", pthread_mutex_lock(&mutex_));
}

void CondLock::unlock()
{
  PthreadCall("unlock condlock", pthread_mutex_unlock(&mutex_));
}

void CondLock::wait()
{
  PthreadCall("condlock wait", pthread_cond_wait(&cond_, &mutex_));
}

bool CondLock::timedWait(uint64_t time_ms)
{
  struct timespec ts;
  MakeTimeoutMs(&ts, time_ms);
  int32_t ret = pthread_cond_timedwait(&cond_, &mutex_, &ts);
  return (0 == ret);
}

void CondLock::signal()
{
  PthreadCall("condlock signal", pthread_cond_signal(&cond_));
}

void CondLock::broadcast()
{
  PthreadCall("condlock broadcast", pthread_cond_broadcast(&cond_));
}

CountDownLatch::CountDownLatch(int32_t count)
    : mutex_(),
      condition_(&mutex_),
      count_(count)
{
}

void CountDownLatch::wait()
{
  MutexLock lock(&mutex_);
  while (count_ > 0)
  {
    condition_.wait();
  }
}

void CountDownLatch::countDown()
{
  MutexLock lock(&mutex_);
  --count_;
  if (count_ == 0)
  {
    condition_.signalAll();
  }
}

int32_t CountDownLatch::getCount() const
{
  MutexLock lock(&mutex_);
  return count_;
}

} // namespace util
} // namespace mycc