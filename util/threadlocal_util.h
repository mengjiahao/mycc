
#ifndef MYCC_UTIL_THREADLOCAL_UTIL_H_
#define MYCC_UTIL_THREADLOCAL_UTIL_H_

#include <pthread.h>
#include "macros_util.h"

namespace mycc
{
namespace util
{

template <typename T>
class ThreadLocalStorage
{
public:
  ThreadLocalStorage()
  {
    pthread_key_create(&key_, &ThreadLocalStorage::Delete);
  }
  ~ThreadLocalStorage()
  {
    pthread_key_delete(key_);
  }
  T *Get()
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

  DISALLOW_COPY_AND_ASSIGN(ThreadLocalStorage);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADLOCAL_UTIL_H_