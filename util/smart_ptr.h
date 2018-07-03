
#ifndef MYCC_UTIL_SMART_PTR_H_
#define MYCC_UTIL_SMART_PTR_H_

#include <assert.h>
#include <algorithm>

namespace mycc
{
namespace util
{

/////////////////////////////// TBHandle ///////////////////////////////////

template <typename T>
class TBHandleBase
{
public:
  typedef T element_type;

  T *get() const
  {
    return _ptr;
  }

  T *operator->() const
  {
    if (!_ptr)
    {
      assert(false);
    }
    return _ptr;
  }

  T &operator*() const
  {
    if (!_ptr)
    {
      assert(false);
    }
    return *_ptr;
  }

  operator bool() const
  {
    return _ptr ? true : false;
  }

  void swap(TBHandleBase &other)
  {
    std::swap(_ptr, other._ptr);
  }

  T *_ptr;
};

template <typename T, typename U>
inline bool operator==(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  T *l = lhs.get();
  U *r = rhs.get();
  if (l && r)
  {
    return *l == *r;
  }
  return !l && !r;
}

template <typename T, typename U>
inline bool operator!=(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  return !operator==(lhs, rhs);
}

template <typename T, typename U>
inline bool operator<(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  T *l = lhs.get();
  U *r = rhs.get();
  if (l && r)
  {
    return *l < *r;
  }

  return !l && r;
}

template <typename T, typename U>
inline bool operator<=(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  return lhs < rhs || lhs == rhs;
}

template <typename T, typename U>
inline bool operator>(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  return !(lhs < rhs || lhs == rhs);
}

template <typename T, typename U>
inline bool operator>=(const TBHandleBase<T> &lhs, const TBHandleBase<U> &rhs)
{
  return !(lhs < rhs);
}

template <typename T>
class TBHandle : public TBHandleBase<T>
{
public:
  TBHandle(T *p = 0)
  {
    this->_ptr = p;
    if (this->_ptr)
    {
      this->_ptr->__incRef();
    }
  }

  template <typename Y>
  TBHandle(const TBHandle<Y> &r)
  {
    this->_ptr = r._ptr;
    if (this->_ptr)
    {
      this->_ptr->__incRef();
    }
  }

  TBHandle(const TBHandle &r)
  {
    this->_ptr = r._ptr;
    if (this->_ptr)
    {
      this->_ptr->__incRef();
    }
  }

  ~TBHandle()
  {
    if (this->_ptr)
    {
      this->_ptr->__decRef();
    }
  }

  TBHandle &operator=(T *p)
  {
    if (this->_ptr != p)
    {
      if (p)
      {
        p->__incRef();
      }

      T *ptr = this->_ptr;
      this->_ptr = p;

      if (ptr)
      {
        ptr->__decRef();
      }
    }
    return *this;
  }

  template <typename Y>
  TBHandle &operator=(const TBHandle<Y> &r)
  {
    if (this->_ptr != r._ptr)
    {
      if (r._ptr)
      {
        r._ptr->__incRef();
      }

      T *ptr = this->_ptr;
      this->_ptr = r._ptr;

      if (ptr)
      {
        ptr->__decRef();
      }
    }
    return *this;
  }

  TBHandle &operator=(const TBHandle &r)
  {
    if (this->_ptr != r._ptr)
    {
      if (r._ptr)
      {
        r._ptr->__incRef();
      }

      T *ptr = this->_ptr;
      this->_ptr = r._ptr;

      if (ptr)
      {
        ptr->__decRef();
      }
    }
    return *this;
  }

  template <class Y>
  static TBHandle DynamicCast(const TBHandleBase<Y> &r)
  {
    return TBHandle(dynamic_cast<T *>(r._ptr));
  }

  template <class Y>
  static TBHandle DynamicCast(Y *p)
  {
    return TBHandle(dynamic_cast<T *>(p));
  }
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SMART_PTR_H_