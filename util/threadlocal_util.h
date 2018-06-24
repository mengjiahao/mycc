
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

template <typename T>
class PthreadTss
{
public:
  PthreadTss()
  {
    pthread_key_create(&key_, &PthreadTss::Delete);
  }

  ~PthreadTss()
  {
    pthread_key_delete(key_);
  }

  T *get()
  {
    T *result = static_cast<T *>(pthread_getspecific(key_));
    if (result == nullptr)
    {
      result = new T();
      pthread_setspecific(key_, result);
    }
    return result;
  }

private:
  static void Delete(void *value)
  {
    delete static_cast<T *>(value);
  }

  pthread_key_t key_;

  DISALLOW_COPY_AND_ASSIGN(PthreadTss);
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
  static void DataDestructor(void *p) { T* ptr = (T*)p; delete ptr; }

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
  ThreadLocalD() { PANIC_EQ(pthread_key_create(&threadSpecificKey_, NULL), 0); }
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
  static void DataDestructor(void *p) { T* ptr = (T*)p; delete ptr; }

  void updateMap(T *p)
  {
    pid_t tid = GetTID();
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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADLOCAL_UTIL_H_