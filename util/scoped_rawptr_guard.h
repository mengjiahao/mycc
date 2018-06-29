
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
  enum Flag
  {
    kIsDelete,
    kIsDeleteArray,
    kIsFree,
  };

  explicit ScopedRawPtrGuard(T *&ptr, Flag flag = kIsDelete)
      : ptr_(ptr), flag_(flag)
  {
  }

  ~ScopedRawPtrGuard()
  {
    if (UNLIKELY(NULL == ptr_))
    {
      return;
    }
    switch (flag_)
    {
    case kIsDelete:
      delete ptr_;
      break;
    case kIsDeleteArray:
      delete[] ptr_;
      break;
    case kIsFree:
      ::free(ptr_);
      break;
    }
    ptr_ = NULL;
  }

private:
  T *&ptr_;
  Flag flag_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SCOPED_RAWPTR_GUARD_H_