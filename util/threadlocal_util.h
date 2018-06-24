
#ifndef MYCC_UTIL_THREADLOCAL_UTIL_H_
#define MYCC_UTIL_THREADLOCAL_UTIL_H_

#include <pthread.h>
#include <memory>
#include <vector>
#include "macros_util.h"

namespace mycc
{
namespace util
{

template <typename T>
class PthreadKey
{
public:
  PthreadKey()
  {
    pthread_key_create(&key_, &PthreadKey::Delete);
  }
  ~PthreadKey()
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

  DISALLOW_COPY_AND_ASSIGN(PthreadKey);
};



} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_THREADLOCAL_UTIL_H_