
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

void InitPthreadOnce(PthreadOnceType *once, void (*initializer)())
{
  PthreadCall("pthread_once", pthread_once(once, initializer));
}

Mutex::Mutex()
{
  PthreadCall("init mutex default", pthread_mutex_init(&mu_, nullptr));
#ifdef NDEBUG
  locked_ = false;
  owner_ = 0;
#endif
}

Mutex::Mutex(bool adaptive)
{
#ifdef NDEBUG
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
  int ret = pthread_mutex_trylock(&mu_);
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
  int ret = pthread_mutex_timedlock(&mu_, &ts);
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
  int ret = pthread_mutex_trylock(&mu_);
  if (0 == ret)
    unlock();
  return 0 != ret;
}

void Mutex::assertHeld()
{
#ifdef NDEBUG
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
#ifdef NDEBUG
  locked_ = true;
  owner_ = pthread_self();
#endif
}

void Mutex::beforeUnlock()
{
#ifdef NDEBUG
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
#ifdef NDEBUG
  mu_->beforeUnlock();
#endif
  PthreadCall("wait cv", pthread_cond_wait(&cv_, &mu_->mu_));
#ifdef NDEBUG
  mu_->afterLock();
#endif
}

bool CondVar::waitUtil(int64_t abs_time_ms)
{
  struct timespec ts;
  ts.tv_sec = static_cast<time_t>(abs_time_ms / 1000);
  ts.tv_nsec = static_cast<suseconds_t>((abs_time_ms % 1000) * 1000000);

#ifdef NDEBUG
  mu_->beforeUnlock();
#endif
  int err = pthread_cond_timedwait(&cv_, &mu_->mu_, &ts);
#ifdef NDEBUG
  mu_->afterLock();
#endif

  if (err == 0 || err == ETIMEDOUT)
  {
    return true;
  }
  if (err != 0)
  {
    PthreadCall("waitUtil cv", err);
  }
  return false;
}

bool CondVar::timedWaitAbsolute(const struct timespec &absolute_time)
{
#ifdef NDEBUG
  mu_->beforeUnlock();
#endif
  int err = pthread_cond_timedwait(&cv_, &mu_->mu_, &absolute_time);
#ifdef NDEBUG
  mu_->afterLock();
#endif

  if (err == 0 || err == ETIMEDOUT)
  {
    return true;
  }
  if (err != 0)
  {
    PthreadCall("timedWaitAbsolute cv", err);
  }
  return false;
}

bool CondVar::waitFor(int64_t time_ms)
{
  // pthread_cond_timedwait api use absolute API
  // so we need gettimeofday + relative_time
  struct timespec ts;
  MakeTimeoutMs(&ts, time_ms);

#ifdef NDEBUG
  mu_->beforeUnlock();
#endif
  int err = pthread_cond_timedwait(&cv_, &mu_->mu_, &ts);
#ifdef NDEBUG
  mu_->afterLock();
#endif

  if (err == 0 || err == ETIMEDOUT)
  {
    return true;
  }
  if (err != 0)
  {
    PthreadCall("waitFor cv", err);
  }
  return false;
}

// Calls timedwait with a relative, instead of an absolute, timeout.
bool CondVar::timedWaitRelative(const struct timespec &relative_time)
{
  struct timespec absolute;
  // clock_gettime would be more convenient, but that needs librt
  // int status = clock_gettime(CLOCK_REALTIME, &absolute);
  struct timeval tv;
  int status = gettimeofday(&tv, NULL);
  assert(status == 0);
  absolute.tv_sec = tv.tv_sec + relative_time.tv_sec;
  absolute.tv_nsec = tv.tv_usec * 1000 + relative_time.tv_nsec;

  return timedWaitAbsolute(absolute);
}

void CondVar::signal()
{
  PthreadCall("signal cv", pthread_cond_signal(&cv_));
}

void CondVar::broadcast()
{
  PthreadCall("broadcast cv", pthread_cond_broadcast(&cv_));
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
  int ret = pthread_rwlock_tryrdlock(&mu_);
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
  int ret = pthread_rwlock_trywrlock(&mu_);
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

bool CondLock::waitUtil(int64_t time_ms)
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

CThreadInterrupt::operator bool() const
{
  return flag.load(std::memory_order_acquire);
}

void CThreadInterrupt::reset()
{
  flag.store(false, std::memory_order_release);
}

void CThreadInterrupt::operator()()
{
  {
    std::unique_lock<std::mutex> lock(mut);
    flag.store(true, std::memory_order_release);
  }
  cond.notify_all();
}

bool CThreadInterrupt::sleep_for(std::chrono::milliseconds rel_time)
{
  std::unique_lock<std::mutex> lock(mut);
  return !cond.wait_for(lock, rel_time, [this]() { return flag.load(std::memory_order_acquire); });
}

bool CThreadInterrupt::sleep_for(std::chrono::seconds rel_time)
{
  return sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(rel_time));
}

bool CThreadInterrupt::sleep_for(std::chrono::minutes rel_time)
{
  return sleep_for(std::chrono::duration_cast<std::chrono::milliseconds>(rel_time));
}

class SemaphorePrivate
{
public:
  sem_t sem;
};

Semaphore::Semaphore(int32_t initValue) : m(new SemaphorePrivate())
{
  sem_init(&m->sem, 0, initValue);
}

Semaphore::~Semaphore()
{
  sem_destroy(&m->sem);
  delete m;
}

bool Semaphore::timeWait(struct timespec *ts)
{
  return (0 == sem_timedwait(&m->sem, ts));
}

void Semaphore::wait()
{
  // errno == EINTR ?
  sem_wait(&m->sem);
}

void Semaphore::post()
{
  sem_post(&m->sem);
}

void Semaphore::post(uint32_t count)
{
  while (count-- > 0)
  {
    sem_post(&m->sem);
  }
}

////////////////////// TCSem /////////////////////////////

struct sembuf g_sem_lock = {0, -1, SEM_UNDO};
struct sembuf g_sem_unlock = {0, 1, SEM_UNDO | IPC_NOWAIT};

TCSemMutex::TCSemMutex() : _semID(-1), _semKey(-1)
{
}

TCSemMutex::TCSemMutex(key_t iKey)
{
  init(iKey);
}

void TCSemMutex::init(key_t iKey)
{
#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
  /* according to X/OPEN we have to define it ourselves */
  union semun {
    int val;               /* value for SETVAL */
    struct semid_ds *buf;  /* buffer for IPC_STAT, IPC_SET */
    unsigned short *array; /* array for GETALL, SETALL */
                           /* Linux specific part: */
    struct seminfo *__buf; /* buffer for IPC_INFO */
  };
#endif

  int iSemID;
  union semun arg;
  u_short array[2] = {0, 0};

  // 2 sem
  if ((iSemID = semget(iKey, 2, IPC_CREAT | IPC_EXCL | 0666)) != -1)
  {
    arg.array = &array[0];
    if (semctl(iSemID, 0, SETALL, arg) == -1)
    {
      //throw TC_SemMutex_Exception("[TCSemMutex::init] semctl error:" + string(strerror(errno)));
      return;
    }
  }
  else
  {
    if (errno != EEXIST)
    {
      //throw TC_SemMutex_Exception("[TCSemMutex::init] sem has exist error:" + string(strerror(errno)));
      return;
    }

    if ((iSemID = semget(iKey, 2, 0666)) == -1)
    {
      //throw TC_SemMutex_Exception("[TCSemMutex::init] connect sem error:" + string(strerror(errno)));
      return;
    }
  }

  _semKey = iKey;
  _semID = iSemID;
}

int TCSemMutex::rlock() const
{
  // wait fisrt sem util 0, second sem (semval+1)
  struct sembuf sops[2] = {{0, 0, SEM_UNDO}, {1, 1, SEM_UNDO}};
  uint64_t nsops = 2;
  int ret = -1;

  do
  {
    ret = semop(_semID, &sops[0], nsops);
  } while ((ret == -1) && (errno == EINTR));

  return ret;
  //return semop( _semID, &sops[0], nsops);
}

int TCSemMutex::unrlock() const
{
  // wait second sem (semval>=1)
  struct sembuf sops[1] = {{1, -1, SEM_UNDO}};
  uint64_t nsops = 1;
  int ret = -1;

  do
  {
    ret = semop(_semID, &sops[0], nsops);
  } while ((ret == -1) && (errno == EINTR));

  return ret;
  //return semop( _semID, &sops[0], nsops);
}

bool TCSemMutex::tryrlock() const
{
  struct sembuf sops[2] = {{0, 0, SEM_UNDO | IPC_NOWAIT}, {1, 1, SEM_UNDO | IPC_NOWAIT}};
  uint64_t nsops = 2;

  int iRet = semop(_semID, &sops[0], nsops);
  if (iRet == -1)
  {
    if (errno == EAGAIN)
    {
      return false;
    }
    else
    {
      //throw TC_SemMutex_Exception("[TCSemMutex::tryrlock] semop error : " + string(strerror(errno)));
      return false;
    }
  }
  return true;
}

int TCSemMutex::wlock() const
{
  // wait first and second sem to 0, release first sem (semval+1)
  struct sembuf sops[3] = {{0, 0, SEM_UNDO}, {1, 0, SEM_UNDO}, {0, 1, SEM_UNDO}};
  uint64_t nsops = 3;
  int ret = -1;

  do
  {
    ret = semop(_semID, &sops[0], nsops);
  } while ((ret == -1) && (errno == EINTR));

  return ret;
  //return semop( _semID, &sops[0], nsops);
}

int TCSemMutex::unwlock() const
{
  // wait first sem (semval >=1)
  struct sembuf sops[1] = {{0, -1, SEM_UNDO}};
  uint64_t nsops = 1;
  int ret = -1;

  do
  {
    ret = semop(_semID, &sops[0], nsops);
  } while ((ret == -1) && (errno == EINTR));

  return ret;
  //return semop( _semID, &sops[0], nsops);
}

bool TCSemMutex::trywlock() const
{
  struct sembuf sops[3] = {{0, 0, SEM_UNDO | IPC_NOWAIT}, {1, 0, SEM_UNDO | IPC_NOWAIT}, {0, 1, SEM_UNDO | IPC_NOWAIT}};
  uint64_t nsops = 3;

  int iRet = semop(_semID, &sops[0], nsops);
  if (iRet == -1)
  {
    if (errno == EAGAIN)
    {
      return false;
    }
    else
    {
      //throw TC_SemMutex_Exception("[TCSemMutex::trywlock] semop error : " + string(strerror(errno)));
      return false;
    }
  }
  return true;
}

} // namespace util
} // namespace mycc