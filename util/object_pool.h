
#ifndef MYCC_UTIL_OBJECT_POOL_H_
#define MYCC_UTIL_OBJECT_POOL_H_

#include <functional>
#include <mutex>
#include <vector>
#include "locks_util.h"

namespace mycc
{
namespace util
{

// An ObjectPool maintains a list of C++ objects which are deallocated
// by destroying the pool.
// Thread-safe.

class ObjectPool
{
public:
  ObjectPool() : objs_() {}

  ~ObjectPool()
  {
    clear();
  }

  template <class T>
  T *add(T *t)
  {
    // Create the object to be pushed to the shared vector outside the critical section.
    // TODO: Consider using a lock-free structure.
    SpecificElement<T> *obj = new SpecificElement<T>(t);
    std::lock_guard<SpinMutex> l(lock_);
    objs_.push_back(obj);
    return t;
  }

  void clear()
  {
    std::lock_guard<SpinMutex> l(lock_);
    for (auto i = objs_.rbegin(); i != objs_.rend(); ++i)
    {
      delete *i;
    }
    objs_.clear();
  }

private:
  struct GenericElement
  {
    virtual ~GenericElement() {}
  };

  template <class T>
  struct SpecificElement : GenericElement
  {
    SpecificElement(T *t) : t(t) {}
    ~SpecificElement()
    {
      delete t;
    }

    T *t;
  };

  typedef std::vector<GenericElement *> ElementVector;
  ElementVector objs_;
  SpinMutex lock_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_OBJECT_POOL_H_