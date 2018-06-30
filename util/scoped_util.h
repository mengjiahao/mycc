
#ifndef MYCC_UTIL_SCOPED_UTIL_H_
#define MYCC_UTIL_SCOPED_UTIL_H_

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

//  This is an implementation designed to match the anticipated future TR2
//  implementation of the ScopedPtr class, and its closely-related brethren,
//  ScopedArray, ScopedPtr_malloc, and make_ScopedPtr.

// A ScopedPtr<T> is like a T*, except that the destructor of ScopedPtr<T>
// automatically deletes the pointer it holds (if any).
// That is, ScopedPtr<T> owns the T object that it points to.
// Like a T*, a ScopedPtr<T> may hold either NULL or a pointer to a T object.
//
// The size of a ScopedPtr is small:
// sizeof(ScopedPtr<C>) == sizeof(C*)
template <class C>
class ScopedPtr
{
  // Forbid comparison of ScopedPtr types.  If C2 != C, it totally doesn't
  // make sense, and if C2 == C, it still doesn't make sense because you should
  // never have the same object owned by two different ScopedPtrs.
  template <class C2>
  bool operator==(ScopedPtr<C2> const &p2) const;
  template <class C2>
  bool operator!=(ScopedPtr<C2> const &p2) const;

  // Disallow evil constructors
  ScopedPtr(const ScopedPtr &);
  void operator=(const ScopedPtr &);

  typedef void *SafeBool;

public:
  // The element type
  typedef C element_type;

  // Constructor.  Defaults to intializing with NULL.
  // There is no way to create an uninitialized ScopedPtr.
  // The input parameter must be allocated with new.
  explicit ScopedPtr(C *p = NULL) : ptr_(p) {}

  // Destructor.  If there is a C object, delete it.
  // We don't need to test ptr_ == NULL because C++ does that for us.
  ~ScopedPtr()
  {
    enum
    {
      type_must_be_complete = sizeof(C)
    };
    delete ptr_;
    ptr_ = reinterpret_cast<C *>(-1);
  }

  // implicit cast to bool
  operator SafeBool() const
  {
    return ptr_;
  }

  bool operator!() const
  {
    return ptr_ == 0;
  }

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  void reset(C *p = NULL)
  {
    if (p != ptr_)
    {
      enum
      {
        type_must_be_complete = sizeof(C)
      };
      delete ptr_;
      ptr_ = p;
    }
  }

  // Accessors to get the owned object.
  // operator* and operator-> will assert() if there is no current object.
  C &operator*() const
  {
    assert(ptr_ != NULL);
    return *ptr_;
  }
  C *operator->() const
  {
    assert(ptr_ != NULL);
    return ptr_;
  }
  C *get() const { return ptr_; }

  // Comparison operators.
  // These return whether two ScopedPtr refer to the same object, not just to
  // two different but equal objects.
  bool operator==(C *p) const { return ptr_ == p; }
  bool operator!=(C *p) const { return ptr_ != p; }

  // Swap two scoped pointers.
  void swap(ScopedPtr &p2)
  {
    C *tmp = ptr_;
    ptr_ = p2.ptr_;
    p2.ptr_ = tmp;
  }

  // Release a pointer.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the object any more.
  C *release()
  {
    C *retVal = ptr_;
    ptr_ = NULL;
    return retVal;
  }

private:
  C *ptr_;
};

// ScopedArray<C> is like scoped_ptr<C>, except that the caller must allocate
// with new [] and the destructor deletes objects with delete [].
//
// As with scoped_ptr<C>, a ScopedArray<C> either points to an object
// or is NULL.  A ScopedArray<C> owns the object that it points to.
//
// Size: sizeof(ScopedArray<C>) == sizeof(C*)
template <class C>
class ScopedArray
{
  // Forbid comparison of different ScopedArray types.
  template <class C2>
  bool operator==(ScopedArray<C2> const &p2) const;
  template <class C2>
  bool operator!=(ScopedArray<C2> const &p2) const;

  // Disallow evil constructors
  ScopedArray(const ScopedArray &);
  void operator=(const ScopedArray &);

public:
  // The element type
  typedef C element_type;

  // Constructor.  Defaults to intializing with NULL.
  // There is no way to create an uninitialized ScopedArray.
  // The input parameter must be allocated with new [].
  explicit ScopedArray(C *p = NULL) : array_(p) {}

  // Destructor.  If there is a C object, delete it.
  // We don't need to test ptr_ == NULL because C++ does that for us.
  ~ScopedArray()
  {
    enum
    {
      type_must_be_complete = sizeof(C)
    };
    delete[] array_;
    array_ = reinterpret_cast<C *>(-1);
  }

  // implicit cast to bool
  operator void *() const
  {
    return array_;
  }

  bool operator!() const
  {
    return array_ == 0;
  }

  // Reset.  Deletes the current owned object, if any.
  // Then takes ownership of a new object, if given.
  // this->reset(this->get()) works.
  void reset(C *p = NULL)
  {
    if (p != array_)
    {
      enum
      {
        type_must_be_complete = sizeof(C)
      };
      delete[] array_;
      array_ = p;
    }
  }

  // Get one element of the current object.
  // Will assert() if there is no current object, or index i is negative.
  C &operator[](ptrdiff_t i) const
  {
    assert(i >= 0);
    assert(array_ != NULL);
    return array_[i];
  }

  // Get a pointer to the zeroth element of the current object.
  // If there is no current object, return NULL.
  C *get() const
  {
    return array_;
  }

  // Comparison operators.
  // These return whether two ScopedArray refer to the same object, not just to
  // two different but equal objects.
  bool operator==(C *p) const { return array_ == p; }
  bool operator!=(C *p) const { return array_ != p; }

  // Swap two scoped arrays.
  void swap(ScopedArray &p2)
  {
    C *tmp = array_;
    array_ = p2.array_;
    p2.array_ = tmp;
  }

  // Release an array.
  // The return value is the current pointer held by this object.
  // If this object holds a NULL pointer, the return value is NULL.
  // After this operation, this object will hold a NULL pointer,
  // and will not own the object any more.
  C *release()
  {
    C *retVal = array_;
    array_ = NULL;
    return retVal;
  }

private:
  C *array_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SCOPED_UTIL_H_