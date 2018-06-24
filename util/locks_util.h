
#ifndef MYCC_UTIL_LOCKS_UTIL_H_
#define MYCC_UTIL_LOCKS_UTIL_H_

#include <pthread.h>
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <unordered_map>
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
  bool timedLock(int64_t time_ms);
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
  Mutex *const mu_ = nullptr;
  DISALLOW_COPY_AND_ASSIGN(MutexLock);
};

// ConditionVariable wraps pthreads condition variable synchronization or, on
// Windows, simulates it.  This functionality is very helpful for having
// several threads wait for an event, as is common with a thread pool managed
// by a master.  The meaning of such an event in the (worker) thread pool
// scenario is that additional tasks are now available for processing.  It is
// used in Chrome in the DNS prefetching system to notify worker threads that
// a queue now has items (tasks) which need to be tended to.  A related use
// would have a pool manager waiting on a ConditionVariable, waiting for a
// thread in the pool to announce (signal) that there is now more room in a
// (bounded size) communications queue for the manager to deposit tasks, or,
// as a second example, that the queue of tasks is completely empty and all
// workers are waiting.
//
// USAGE NOTE 1: spurious signal events are possible with this and
// most implementations of condition variables.  As a result, be
// *sure* to retest your condition before proceeding.  The following
// is a good example of doing this correctly:
//
// while (!work_to_be_done()) Wait(...);
//
// In contrast do NOT do the following:
//
// if (!work_to_be_done()) Wait(...);  // Don't do this.
//
// Especially avoid the above if you are relying on some other thread only
// issuing a signal up *if* there is work-to-do.  There can/will
// be spurious signals.  Recheck state on waiting thread before
// assuming the signal was intentional. Caveat caller ;-).
//
// USAGE NOTE 2: Broadcast() frees up all waiting threads at once,
// which leads to contention for the locks they all held when they
// called Wait().  This results in POOR performance.  A much better
// approach to getting a lot of threads out of Wait() is to have each
// thread (upon exiting Wait()) call Signal() to free up another
// Wait'ing thread.  Look at condition_variable_unittest.cc for
// both examples.
//
// Broadcast() can be used nicely during teardown, as it gets the job
// done, and leaves no sleeping threads... and performance is less
// critical at that point.
//
// The semantics of Broadcast() are carefully crafted so that *all*
// threads that were waiting when the request was made will indeed
// get signaled.  Some implementations mess up, and don't signal them
// all, while others allow the wait to be effectively turned off (for
// a while while waiting threads come around).  This implementation
// appears correct, as it will not "lose" any signals, and will guarantee
// that all threads get signaled by Broadcast().
//
// This implementation offers support for "performance" in its selection of
// which thread to revive.  Performance, in direct contrast with "fairness,"
// assures that the thread that most recently began to Wait() is selected by
// Signal to revive.  Fairness would (if publicly supported) assure that the
// thread that has Wait()ed the longest is selected. The default policy
// may improve performance, as the selected thread may have a greater chance of
// having some of its stack data in various CPU caches.
//

class CondVar
{
public:
  explicit CondVar(Mutex *mu);
  ~CondVar();
  // Wait() releases the caller's critical section atomically as it starts to
  // sleep, and the reacquires it when it is signaled.
  void wait();
  // Timed condition wait.  Returns true if timeout occurred.
  bool timedWait(int64_t abs_time_ms);
  bool timedWaitAbsolute(const struct timespec &absolute_time);
  // Time wait in timeout us, return true if timeout
  bool timedWaitRelative(int64_t time_ms);
  bool timedWaitRelative(const struct timespec &relative_time);
  void signal();
  void signalAll();
  void broadcast() { signalAll(); }

private:
  pthread_cond_t cv_;
  Mutex *mu_ = nullptr;

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
  RWMutex *const mu_ = nullptr;
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
  RWMutex *const mu_ = nullptr;
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
  RWMutex *const mu_ = nullptr;
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
  SpinLock *spin_lock_ = nullptr;
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
  bool timedWait(uint64_t time_ms);
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
  explicit BlockingCounter(uint64_t cnt)
      : cond_(&mutex_), counter_(cnt) {}

  bool decrement()
  {
    MutexLock lock(&mutex_);
    --counter_;

    if (counter_ == 0)
    {
      cond_.signalAll();
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

  bool timedWaitRelative(int64_t timeout_ms)
  {
    MutexLock lock(&mutex_);
    if (!signaled_)
    {
      cv_.timedWaitRelative(timeout_ms);
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

class CountDownLatch
{
public:
  explicit CountDownLatch(int32_t count);

  void wait();

  void countDown();

  int32_t getCount() const;

private:
  mutable Mutex mutex_;
  CondVar condition_;
  int32_t count_;

  DISALLOW_COPY_AND_ASSIGN(CountDownLatch);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOCKS_UTIL_H_