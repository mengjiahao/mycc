
#ifndef MYCC_UTIL_LOCKS_UTIL_H_
#define MYCC_UTIL_LOCKS_UTIL_H_

#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include "atomic_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

typedef pthread_once_t PthreadOnceType;
void InitPthreadOnce(PthreadOnceType *once, void (*initializer)());

// Thinly wraps std::call_once.
using OnceType = std::once_flag;
inline void InitOnce(OnceType *once, void (*initializer)())
{
  std::call_once(*once, *initializer);
}

// BASE_SCOPED_LOCK
#if !defined(BASE_CXX11_ENABLED)
#define BASE_SCOPED_LOCK(ref_of_lock)       \
  std::lock_guard<BASE_TYPEOF(ref_of_lock)> \
      BASE_CONCAT(scoped_locker_dummy_at_line_, __LINE__)(ref_of_lock)
#else
// c++11 deduces additional reference to the type.
namespace internal
{
template <typename T>
std::lock_guard<typename std::remove_reference<T>::type> get_lock_guard();
} // namespace internal

#define BASE_SCOPED_LOCK(ref_of_lock)                                       \
  decltype(::mycc::util::internal::get_lock_guard<decltype(ref_of_lock)>()) \
      BASE_CONCAT(scoped_locker_dummy_at_line_, __LINE__)(ref_of_lock)
#endif // BASE_SCOPED_LOCK

/**
 * A wrapper for condition variable with mutex.
 */
class LockedCondition : public std::condition_variable
{
public:
  /**
   * @brief execute op and notify one thread which was blocked.
   * @param[in] op a thread can do something in op before notify.
   */
  template <class Op>
  void notify_one(Op op)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    op();
    std::condition_variable::notify_one();
  }

  /**
   * @brief execute op and notify all the threads which were blocked.
   * @param[in] op a thread can do something in op before notify.
   */
  template <class Op>
  void notify_all(Op op)
  {
    std::lock_guard<std::mutex> guard(mutex_);
    op();
    std::condition_variable::notify_all();
  }

  /**
   * @brief wait until pred return ture.
   * @tparam Predicate c++ concepts, describes a function object
   * that takes a single iterator argument
   * that is dereferenced and used to
   * return a value testable as a bool.
   * @note pred shall not apply any non-constant function
   * through the dereferenced iterator.
   */
  template <class Predicate>
  void wait(Predicate pred)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    std::condition_variable::wait(lock, pred);
  }

  /**
   * @brief get mutex.
   */
  std::mutex *mutex() { return &mutex_; }

protected:
  std::mutex mutex_;
};

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
  void broadcast();

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

#ifdef USE_PTHREAD_SPINLOCK
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

private:
  DISALLOW_COPY_AND_ASSIGN(SpinLock);
};
#else
class SpinLock final
{
public:
  inline void lock()
  {
    while (lock_.test_and_set(std::memory_order_acquire))
    {
    }
  }
  inline void unlock() { lock_.clear(std::memory_order_release); }
  inline int tryLock() { return lock_.test_and_set(std::memory_order_acquire); }

  std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
  char padding_[64 - sizeof(lock_)]; // Padding to cache line size

private:
  DISALLOW_COPY_AND_ASSIGN(SpinLock);
};
#endif // USE_PTHREAD_SPINLOCK

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

/*
 * Improves the performance of spin-wait loops. 
 * When executing a “spin-wait loop,” a Pentium 4 or Intel Xeon processor 
 * suffers a severe performance penalty when exiting the loop 
 * because it detects a possible memory order violation. 
 * The PAUSE instruction provides a hint to the processor 
 * that the code sequence is a spin-wait loop. 
 * The processor uses this hint to avoid the memory order violation in most situations,
 * which greatly improves processor performance. 
 * For this reason, it is recommended that a PAUSE instruction 
 * be placed in all spin-wait loops.
 * An additional fucntion of the PAUSE instruction 
 * is to reduce the power consumed by a Pentium 4 processor 
 * while executing a spin loop.
*/
class SpinLockCas
{
public:
  typedef uint32_t handle_type;

private:
  enum state
  {
    initial_pause = 2,
    max_pause = 16
  };

  uint32_t state_;

public:
  SpinLockCas() : state_(0) {}

  bool trylock()
  {
    return (AtomicSyncValCompareAndSwap<uint32_t>((volatile uint32_t *)&state_, 0, 1) == 0);
  }

  bool lock()
  {
    /*register*/ uint32_t pause_count = initial_pause; //'register' storage class specifier is deprecated and incompatible with C++1z
    while (!trylock())
    {
      if (pause_count < max_pause)
      {
        for (/*register*/ uint32_t i = 0; i < pause_count; ++i) //'register' storage class specifier is deprecated and incompatible with C++1z
        {
          AsmVolatileCpuRelax();
        }
        pause_count += pause_count;
      }
      else
      {
        pause_count = initial_pause;
        ::sched_yield();
      }
    }
    return true;
  }

  bool unlock()
  {
    AtomicSyncStore<uint32_t>((volatile uint32_t *)&state_, 0);
    return true;
  }

  uint32_t *internal() { return &state_; }

private:
  DISALLOW_COPY_AND_ASSIGN(SpinLockCas);
};

//
// SpinMutex has very low overhead for low-contention cases.  Method names
// are chosen so you can use std::unique_lock or std::lock_guard with it.
//
class SpinMutex
{
public:
  SpinMutex() : locked_(false) {}

  bool try_lock()
  {
    auto currently_locked = locked_.load(std::memory_order_relaxed);
    return !currently_locked &&
           locked_.compare_exchange_weak(currently_locked, true,
                                         std::memory_order_acquire,
                                         std::memory_order_relaxed);
  }

  void lock()
  {
    for (uint64_t tries = 0;; ++tries)
    {
      if (try_lock())
      {
        // success
        break;
      }
      AsmVolatilePause();
      if (tries > 100)
      {
        std::this_thread::yield();
      }
    }
  }

  void unlock() { locked_.store(false, std::memory_order_release); }

private:
  std::atomic<bool> locked_;
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
      cond_.broadcast();
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

// The AutoResetEvent class represents a local waitable event that resets
// automatically when signaled, after releasing a single waiting thread.
//
// AutoResetEvent allows threads to communicate with each other by signaling.
// Typically, you use this class when threads need exclusive access to a resource.
//
// Important
// There is no guarantee that every call to the Set method will release a thread.
// If two calls are too close together, so that the second call occurs before a
// thread has been released, only one thread is released. It is as if the second
// call did not happen. Also, if Set is called when there are no threads waiting
// and the AutoResetEvent is already signaled, the call has no effect.
//
// If you want to release a thread after each call, Semaphore is a good choice.
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

/**
 * A simple wapper of semaphore which can only be shared in the same process.
 */
class SemaphorePrivate;
class Semaphore
{
public:
  //! Enable move.
  Semaphore(Semaphore &&other) : m(std::move(other.m)) {}

public:
  /**
   * @brief Construct Function.
   * @param[in] initValue the initial value of the
   * semaphore, default 0.
   */
  explicit Semaphore(int32_t initValue = 0);

  ~Semaphore();

  /**
   * @brief The same as wait(), except if the decrement can not
   * be performed until ts return false install of blocking.
   * @param[in] ts an absolute timeout in seconds and nanoseconds
   * since the Epoch 1970-01-01 00:00:00 +0000(UTC).
   * @return ture if the decrement proceeds before ts,
   * else return false.
   */
  bool timeWait(struct timespec *ts);

  /**
   * @brief decrement the semaphore. If the semaphore's value is 0, then call
   * blocks.
   */
  void wait();

  /**
   * @brief increment the semaphore. If the semaphore's value
   * greater than 0, wake up a thread blocked in wait().
   */
  void post();

private:
  SemaphorePrivate *m;

  DISALLOW_COPY_AND_ASSIGN(Semaphore);
};

class CSemaphore
{
private:
  std::condition_variable condition;
  std::mutex mutex;
  int32_t value;

public:
  explicit CSemaphore(int32_t init) : value(init) {}

  void wait()
  {
    std::unique_lock<std::mutex> lock(mutex);
    while (value < 1)
    {
      condition.wait(lock);
    }
    value--;
  }

  bool try_wait()
  {
    std::unique_lock<std::mutex> lock(mutex);
    if (value < 1)
      return false;
    value--;
    return true;
  }

  void post()
  {
    {
      std::unique_lock<std::mutex> lock(mutex);
      value++;
    }
    condition.notify_one();
  }
};

/** RAII-style semaphore lock */
class CSemaphoreGrant
{
private:
  CSemaphore *sem;
  bool fHaveGrant;

public:
  void Acquire()
  {
    if (fHaveGrant)
      return;
    sem->wait();
    fHaveGrant = true;
  }

  void Release()
  {
    if (!fHaveGrant)
      return;
    sem->post();
    fHaveGrant = false;
  }

  bool TryAcquire()
  {
    if (!fHaveGrant && sem->try_wait())
      fHaveGrant = true;
    return fHaveGrant;
  }

  void MoveTo(CSemaphoreGrant &grant)
  {
    grant.Release();
    grant.sem = sem;
    grant.fHaveGrant = fHaveGrant;
    fHaveGrant = false;
  }

  CSemaphoreGrant() : sem(nullptr), fHaveGrant(false) {}

  explicit CSemaphoreGrant(CSemaphore &sema, bool fTry = false) : sem(&sema), fHaveGrant(false)
  {
    if (fTry)
      TryAcquire();
    else
      Acquire();
  }

  ~CSemaphoreGrant()
  {
    Release();
  }

  operator bool() const
  {
    return fHaveGrant;
  }
};

class Notification
{
public:
  Notification() : notified_(0) {}
  ~Notification()
  {
    // In case the notification is being used to synchronize its own deletion,
    // force any prior notifier to leave its critical section before the object
    // is destroyed.
    std::unique_lock<std::mutex> l(mu_);
  }

  void notify()
  {
    std::unique_lock<std::mutex> l(mu_);
    assert(!hasBeenNotified());
    notified_.store(true, std::memory_order_release);
    cv_.notify_all();
  }

  bool hasBeenNotified() const
  {
    return notified_.load(std::memory_order_acquire);
  }

  void waitForNotification()
  {
    if (!hasBeenNotified())
    {
      std::unique_lock<std::mutex> l(mu_);
      while (!hasBeenNotified())
      {
        cv_.wait(l);
      }
    }
  }

private:
  friend bool WaitForNotificationWithTimeout(Notification *n,
                                             int64_t timeout_in_us);
  bool waitForNotificationWithTimeout(int64_t timeout_in_us)
  {
    bool notified = hasBeenNotified();
    if (!notified)
    {
      std::unique_lock<std::mutex> l(mu_);
      do
      {
        notified = hasBeenNotified();
      } while (!notified &&
               cv_.wait_for(l, std::chrono::microseconds(timeout_in_us)) !=
                   std::cv_status::timeout);
    }
    return notified;
  }

  std::mutex mu_;              // protects mutations of notified_
  std::condition_variable cv_; // signalled when notified_ becomes non-zero
  std::atomic<bool> notified_; // mutations under mu_
};

inline bool WaitForNotificationWithTimeout(Notification *n,
                                           int64_t timeout_in_us)
{
  return n->waitForNotificationWithTimeout(timeout_in_us);
}

/*
    A helper class for interruptible sleeps. Calling operator() will interrupt
    any current sleep, and after that point operator bool() will return true
    until reset.
*/
class CThreadInterrupt
{
public:
  explicit operator bool() const;
  void operator()();
  void reset();
  bool sleep_for(std::chrono::milliseconds rel_time);
  bool sleep_for(std::chrono::seconds rel_time);
  bool sleep_for(std::chrono::minutes rel_time);

private:
  std::condition_variable cond;
  std::mutex mut;
  std::atomic<bool> flag;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOCKS_UTIL_H_