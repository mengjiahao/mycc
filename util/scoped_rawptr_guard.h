
#ifndef MYCC_UTIL_SCOPED_RAWPTR_GUARD_H_
#define MYCC_UTIL_SCOPED_RAWPTR_GUARD_H_

#include <malloc.h>
#include "macros_util.h"

namespace mycc
{
namespace util
{

template <typename T>
class ScopedRawPtrGuard
{
public:
  explicit ScopedRawPtrGuard(T *&ptr, bool isFree = false)
      : ptr_(ptr)
  {
  }

  ~ScopedRawPtrGuard()
  {
    if (BUILTIN_UNLIKELY(nullptr == ptr_))
    {
      return;
    }
    if (BUILTIN_UNLIKELY(isFree))
    {
      free(ptr_);
    }
    else
    {
      delete ptr_;
    }
    ptr_ = nullptr;
  }

private:
  T *&ptr_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SCOPED_RAWPTR_GUARD_H_