
#ifndef MYCC_UTIL_THREADLOCAL_UTIL_H_
#define MYCC_UTIL_THREADLOCAL_UTIL_H_

#include <pthread.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <random>
#include "error_util.h"
#include "macros_util.h"
#include "thread_util.h"

namespace mycc
{
namespace util
{

// Provide thread_local keyword (for primitive types) before C++11
// #define THREAD_LOCAL __thread

template <class T>
class PthreadTLS
{
public:
  typedef void (*CleanupFuncType)(void *);

  PthreadTLS(CleanupFuncType cleanup = NULL)
  {
    pthread_key_create(&key, cleanup);
  }
  ~PthreadTLS()
  {
    pthread_key_delete(key);
  }
  T get() { return (T)pthread_getspecific(key); }
  void set(T value) { pthread_setspecific(key, (void *)value); }

private:
  pthread_key_t key;
};

/**
 * Thread local storage for object.
 * Example:
 *
 * Declarartion:
 * ThreadLocal<vector<int>> vec_;
 *
 * Use in thread:
 * vector<int>& vec = *vec; // obtain the thread specific object
 * vec.resize(100);
 *
 * Note that this ThreadLocal will desconstruct all internal data when thread
 * exits
 * This class is suitable for cases when frequently creating and deleting
 * threads.
 *
 * Consider implementing a new ThreadLocal if one needs to frequently create
 * both instances and threads.
 *
 * see also ThreadLocalD
 */
template <class T>
class ThreadLocal
{
public:
  ThreadLocal()
  {
    PANIC_EQ(pthread_key_create(&threadSpecificKey_, DataDestructor), 0);
  }
  ~ThreadLocal() { pthread_key_delete(threadSpecificKey_); }

  /**
   * @brief get thread local object.
   * @param if createLocal is true and thread local object is never created,
   * return a new object. Otherwise, return nullptr.
   */
  T *get(bool createLocal = true)
  {
    T *p = (T *)pthread_getspecific(threadSpecificKey_);
    if (!p && createLocal)
    {
      p = new T();
      int ret = pthread_setspecific(threadSpecificKey_, p);
      PANIC_EQ(ret, 0);
    }
    return p;
  }

  /**
   * @brief set (overwrite) thread local object. If there is a thread local
   * object before, the previous object will be destructed before.
   *
   */
  void set(T *p)
  {
    if (T *q = get(false))
    {
      DataDestructor(q);
    }
    PANIC_EQ(pthread_setspecific(threadSpecificKey_, p), 0);
  }

  /**
   * return reference.
   */
  T &operator*() { return *get(); }

  /**
   * Implicit conversion to T*
   */
  operator T *() { return get(); }

private:
  static void DataDestructor(void *p)
  {
    T *ptr = (T *)p;
    delete ptr;
  }

  pthread_key_t threadSpecificKey_;
};

/**
 * Almost the same as ThreadLocal, but note that this ThreadLocalD will
 * destruct all internal data when ThreadLocalD instance destructs.
 *
 * This class is suitable for cases when frequently creating and deleting
 * objects.
 *
 * see also ThreadLocal
 *
 * @note The type T must implemented default constructor.
 */
template <class T>
class ThreadLocalD
{
public:
  ThreadLocalD()
  {
    PANIC_EQ(pthread_key_create(&threadSpecificKey_, NULL), 0);
  }

  ~ThreadLocalD()
  {
    pthread_key_delete(threadSpecificKey_);
    for (auto t : threadMap_)
    {
      DataDestructor(t.second);
    }
  }

  /**
   * @brief Get thread local object. If not exists, create new one.
   */
  T *get()
  {
    T *p = (T *)pthread_getspecific(threadSpecificKey_);
    if (!p)
    {
      p = new T();
      PANIC_EQ(pthread_setspecific(threadSpecificKey_, p), 0);
      updateMap(p);
    }
    return p;
  }

  /**
   * @brief Set thread local object. If there is an object create before, the
   * old object will be destructed.
   */
  void set(T *p)
  {
    if (T *q = (T *)pthread_getspecific(threadSpecificKey_))
    {
      DataDestructor(q);
    }
    PANIC_EQ(pthread_setspecific(threadSpecificKey_, p), 0);
    updateMap(p);
  }

  /**
   * @brief Get reference of the thread local object.
   */
  T &operator*() { return *get(); }

private:
  static void DataDestructor(void *p)
  {
    T *ptr = (T *)p;
    delete ptr;
  }

  void updateMap(T *p)
  {
    pid_t tid = syscall(__NR_gettid);
    PANIC_NE(tid, -1);
    std::lock_guard<std::mutex> guard(mutex_);
    auto ret = threadMap_.insert(std::make_pair(tid, p));
    if (!ret.second)
    {
      ret.first->second = p;
    }
  }

  pthread_key_t threadSpecificKey_;
  std::mutex mutex_;
  std::map<pid_t, T *> threadMap_;
};

/*!
 * \brief A threadlocal store to store threadlocal variables.
 *  Will return a thread local singleton of type T
 * \tparam T the type we like to store
 */
template <typename T>
class ThreadLocalStore
{
public:
  /*! \return get a thread local singleton */
  static T *Get()
  {
    static thread_local T inst;
    return &inst;
  }

private:
  /*! \brief constructor */
  ThreadLocalStore() {}
  /*! \brief destructor */
  ~ThreadLocalStore()
  {
    for (uint64_t i = 0; i < data_.size(); ++i)
    {
      delete data_[i];
    }
  }
  /*! \return singleton of the store */
  static ThreadLocalStore<T> *Singleton()
  {
    static ThreadLocalStore<T> inst;
    return &inst;
  }
  /*!
   * \brief register str for internal deletion
   * \param str the string pointer
   */
  void RegisterDelete(T *str)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    data_.push_back(str);
    lock.unlock();
  }
  /*! \brief internal mutex */
  std::mutex mutex_;
  /*!\brief internal data */
  std::vector<T *> data_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADLOCAL_UTIL_H_