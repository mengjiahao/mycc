
#ifndef MYCC_UTIL_LOCKS_UTIL_H_
#define MYCC_UTIL_LOCKS_UTIL_H_

#include <pthread.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "macros_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

/**
 * A simple wrapper of thread barrier.
 * The ThreadBarrier disable copy.
 */
class ThreadBarrier
{
public:
  pthread_barrier_t barrier_;

  inline explicit ThreadBarrier(int32_t count)
  {
    pthread_barrier_init(&barrier_, nullptr, count);
  }

  inline ~ThreadBarrier() { pthread_barrier_destroy(&barrier_); }

  inline void wait() { pthread_barrier_wait(&barrier_); }

  DISALLOW_COPY_AND_ASSIGN(ThreadBarrier);
};

// A Mutex represents an exclusive lock.
class Mutex
{
public:
  // We want to give users opportunity to default all the mutexes to adaptive if
  // not specified otherwise. This enables a quick way to conduct various
  // performance related experiements.
  //
  // NB! Support for adaptive mutexes is turned on by definining
  // ROCKSDB_PTHREAD_ADAPTIVE_MUTEX during the compilation. If you use RocksDB
  // build environment then this happens automatically; otherwise it's up to the
  // consumer to define the identifier.
  Mutex();
  Mutex(bool adaptive);
  ~Mutex();

  void lock();
  bool tryLock();
  bool timedLock(uint64_t time_us);
  void unlock();
  bool isLocked();
  // this will assert if the mutex is not locked
  // it does NOT verify that mutex is held by a calling thread
  // use when MUTEX_DEBUG is defined
  void assertHeld();
  pthread_mutex_t *getMutex()
  {
    return &mu_;
  }

private:
  void afterLock();
  void beforeUnlock();

  friend class CondVar;
  pthread_mutex_t mu_;

#ifdef MUTEX_DEBUG
  pthread_t owner_;
  bool locked_;
#endif

  DISALLOW_COPY_AND_ASSIGN(Mutex);
};

class MutexLock
{
public:
  explicit MutexLock(Mutex *mu) : mu_(mu)
  {
    mu_->lock();
  }
  ~MutexLock()
  {
    mu_->unlock();
  }

private:
  Mutex *const mu_;
  DISALLOW_COPY_AND_ASSIGN(MutexLock);
};

class CondVar
{
public:
  explicit CondVar(Mutex *mu);
  ~CondVar();
  void wait();
  // Timed condition wait.  Returns true if timeout occurred.
  bool timedWait(uint64_t abs_time_us);
  bool timedWaitAbsolute(const struct timespec &absolute_time);
  // Time wait in timeout us, return true if timeout
  bool timedWaitRelative(uint64_t rel_time_us);
  bool timedWaitRelative(const struct timespec &relative_time);
  void signal();
  void signalall();

private:
  pthread_cond_t cv_;
  Mutex *mu_;

  DISALLOW_COPY_AND_ASSIGN(CondVar);
};

class RWMutex
{
public:
  RWMutex();
  ~RWMutex();

  void readLock();
  bool tryReadLock();
  void writeLock();
  bool tryWriteLock();
  void unlock();
  void readUnlock();
  void writeUnlock();
  void assertHeld() {}

private:
  pthread_rwlock_t mu_; // the underlying platform mutex
  DISALLOW_COPY_AND_ASSIGN(RWMutex);
};

//
// Acquire a ReadLock on the specified RWMutex.
// The Lock will be automatically released then the
// object goes out of scope.
//
class ReadLock
{
public:
  explicit ReadLock(RWMutex *mu) : mu_(mu)
  {
    this->mu_->readLock();
  }
  ~ReadLock() { this->mu_->readUnlock(); }

private:
  RWMutex *const mu_;
  DISALLOW_COPY_AND_ASSIGN(ReadLock);
};

//
// Automatically unlock a locked mutex when the object is destroyed
//
class ReadUnlock
{
public:
  explicit ReadUnlock(RWMutex *mu) : mu_(mu) { mu->assertHeld(); }
  ~ReadUnlock() { mu_->readUnlock(); }

private:
  RWMutex *const mu_;
  DISALLOW_COPY_AND_ASSIGN(ReadUnlock);
};

//
// Acquire a WriteLock on the specified RWMutex.
// The Lock will be automatically released then the
// object goes out of scope.
//
class WriteLock
{
public:
  explicit WriteLock(RWMutex *mu) : mu_(mu)
  {
    this->mu_->writeLock();
  }
  ~WriteLock() { this->mu_->writeUnlock(); }

private:
  RWMutex *const mu_;
  DISALLOW_COPY_AND_ASSIGN(WriteLock);
};

/**
 * A simple wrapper for spin lock.
 * The lock() method of SpinLock is busy-waiting
 * which means it will keep trying to lock until lock on successfully.
 * The SpinLock disable copy.
 */
class SpinLock
{
public:
  inline SpinLock() { pthread_spin_init(&lock_, PTHREAD_PROCESS_PRIVATE); }
  inline ~SpinLock() { pthread_spin_destroy(&lock_); }

  inline void lock() { pthread_spin_lock(&lock_); }
  inline void unlock() { pthread_spin_unlock(&lock_); }
  inline int32_t tryLock() { return pthread_spin_trylock(&lock_); }

  pthread_spinlock_t lock_;
  char padding_[64 - sizeof(pthread_spinlock_t)];

  DISALLOW_COPY_AND_ASSIGN(SpinLock);
};

/**
 * The SpinLockGuard is a SpinLock
 * using RAII management mechanism.
 */
class SpinLockGuard
{
public:
  explicit SpinLockGuard(SpinLock *spin_lock) : spin_lock_(spin_lock)
  {
    spin_lock_->lock();
  }

  ~SpinLockGuard()
  {
    spin_lock_->unlock();
  }

protected:
  SpinLock *spin_lock_;
};

// SpinMutex has very low overhead for low-contention cases.
class SpinMutex
{
public:
  inline void lock()
  {
    while (lock_.test_and_set(std::memory_order_acquire))
    {
    }
  }
  inline void unlock() { lock_.clear(std::memory_order_release); }
  inline int32_t tryLock() { return lock_.test_and_set(std::memory_order_acquire); }

  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  char padding_[64 - sizeof(lock_)]; // Padding to cache line size

  DISALLOW_COPY_AND_ASSIGN(SpinMutex);
};

class SpinMutexGuard
{
public:
  explicit SpinMutexGuard(SpinMutex *spin_mutex) : spin_mutex_(spin_mutex)
  {
    spin_mutex_->lock();
  }

  ~SpinMutexGuard()
  {
    spin_mutex_->unlock();
  }

protected:
  SpinMutex *spin_mutex_;
};

class RefMutex
{
public:
  RefMutex();
  ~RefMutex();

  // Lock and Unlock will increase and decrease refs_,
  // should check refs before Unlock
  void lock();
  void unlock();

  void ref();
  void unref();
  bool isLastRef()
  {
    return refs_ == 1;
  }

private:
  pthread_mutex_t mu_;
  int32_t refs_;
  DISALLOW_COPY_AND_ASSIGN(RefMutex);
};

class RecordMutex
{
public:
  RecordMutex(){};
  ~RecordMutex();

  void lock(const string &key);
  void unlock(const string &key);

private:
  Mutex mutex_;
  std::unordered_map<string, RefMutex *> records_;

  DISALLOW_COPY_AND_ASSIGN(RecordMutex);
};

class RecordLock
{
public:
  RecordLock(RecordMutex *mu, const string &key)
      : mu_(mu), key_(key)
  {
    mu_->lock(key_);
  }
  ~RecordLock() { mu_->unlock(key_); }

private:
  RecordMutex *const mu_;
  string key_;

  DISALLOW_COPY_AND_ASSIGN(RecordLock);
};

/*
 * CondLock is a wrapper for condition variable.
 * It contain a mutex in it's class, so we don't need other to protect the 
 * condition variable.
 */
class CondLock
{
public:
  CondLock();
  ~CondLock();

  void lock();
  void unlock();
  void wait();
  void timedWait(uint64_t time_us);
  void signal();
  void broadcast();

private:
  pthread_mutex_t mutex_;
  pthread_cond_t cond_;

  DISALLOW_COPY_AND_ASSIGN(CondLock);
};

class BlockingCounter
{
public:
  explicit BlockingCounter(uint64_t cnt) : cond_(&mutex_), counter_(cnt) {}

  bool decrement()
  {
    MutexLock lock(&mutex_);
    --counter_;

    if (counter_ == 0)
    {
      cond_.signalall();
    }

    return counter_ == 0u;
  }

  void reset(uint64_t cnt)
  {
    MutexLock lock(&mutex_);
    counter_ = cnt;
  }

  void wait()
  {
    MutexLock lock(&mutex_);

    while (counter_ != 0)
    {
      cond_.wait();
    }
  }

private:
  Mutex mutex_;
  CondVar cond_;
  uint64_t counter_;
  DISALLOW_COPY_AND_ASSIGN(BlockingCounter);
};

class AutoResetEvent
{
public:
  AutoResetEvent()
      : cv_(&mutex_), signaled_(false)
  {
  }
  
  /// Wait for signal
  void wait()
  {
    MutexLock lock(&mutex_);
    while (!signaled_)
    {
      cv_.wait();
    }
    signaled_ = false;
  }

  bool timedWaitRelative(int64_t timeout_us)
  {
    MutexLock lock(&mutex_);
    if (!signaled_)
    {
      cv_.timedWaitRelative(timeout_us);
    }
    bool ret = signaled_;
    signaled_ = false;
    return ret;
  }

  /// Signal one
  void set()
  {
    MutexLock lock(&mutex_);
    signaled_ = true;
    cv_.signal();
  }

private:
  Mutex mutex_;
  CondVar cv_;
  bool signaled_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOCKS_UTIL_H_