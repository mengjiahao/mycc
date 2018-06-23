
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
  explicit ScopedRawPtrGuard(T *&ptr, bool isNeedFree = false)
      : ptr_(ptr), is_need_free_(isNeedFree)
  {
  }

  ~ScopedRawPtrGuard()
  {
    if (UNLIKELY(NULL == ptr_))
    {
      return;
    }
    if (UNLIKELY(is_need_free_))
    {
      ::free(ptr_);
    }
    else
    {
      delete ptr_;
    }
    ptr_ = NULL;
  }

private:
  T *&ptr_;
  bool is_need_free_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SCOPED_RAWPTR_GUARD_H_