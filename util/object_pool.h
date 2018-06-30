
#ifndef MYCC_UTIL_OBJECT_POOL_H_
#define MYCC_UTIL_OBJECT_POOL_H_

#include <functional>
#include <mutex>
#include <vector>
#include "locks_util.h"
#include "refcount.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// An DeleteObjectPool maintains a list of C++ objects which are deallocated
// by destroying the pool.
// Thread-safe.

class DeleteObjectPool
{
public:
  DeleteObjectPool() : objs_() {}

  ~DeleteObjectPool()
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

// A object pool to cache resuable objects.

template <typename T>
class ReusableObjectPool
{
public:
  static const int32_t kMaxCacheObject = 32;

  static ReusableObjectPool *Instance()
  {
    static ReusableObjectPool pool;
    return &pool;
  }

  T *GetObject()
  {
    T *t = NULL;
    if (m_count == 0)
    {
      t = new T();
    }
    else
    {
      t = m_objects[m_count - 1];
      m_count--;
    }
    return t;
  }

  void ReleaseObject(T *t)
  {
    if (m_count < kMaxCacheObject)
    {
      m_objects[m_count] = t;
      m_count++;
    }
    else
    {
      delete t;
    }
  }

private:
  ReusableObjectPool() : m_count(0) {}
  ~ReusableObjectPool()
  {
    for (int32_t i = 0; i < m_count; ++i)
    {
      delete m_objects[i];
    }
  }

private:
  T *m_objects[kMaxCacheObject];
  int32_t m_count;
};

/**
 * A pool for managing autorelease objects.
 */
class AutoreleasePool
{
public:
  /**
   * @warning Don't create an autorelease pool in heap, create it in stack.
   */
  AutoreleasePool();

  /**
   * Create an autorelease pool with specific name. This name is useful for debugging.
   * @warning Don't create an autorelease pool in heap, create it in stack.
   * @param name The name of created autorelease pool.
   */
  AutoreleasePool(const string &name);

  ~AutoreleasePool();

  /**
   * Add a given object to this autorelease pool.
   *
   * The same object may be added several times to an autorelease pool. When the
   * pool is destructed, the object's `Ref::release()` method will be called
   * the same times as it was added.
   *
   * @param object    The object to be added into the autorelease pool.
   */
  void addObject(Ref *object);

  /**
   * Clear the autorelease pool.
   *
   * It will invoke each element's `release()` function.
   */
  void clear();

  /**
   * Whether the autorelease pool is doing `clear` operation.
   *
   * @return True if autorelease pool is clearing, false if not.
   *
   */
  bool isClearing() const { return _isClearing; };

  /**
   * Checks whether the autorelease pool contains the specified object.
   *
   * @param object The object to be checked.
   * @return True if the autorelease pool contains the object, false if not
   */
  bool contains(Ref *object) const;

  /**
   * Dump the objects that are put into the autorelease pool. It is used for debugging.
   *
   * The result will look like:
   * Object pointer address     object id     reference count
   */
  void dump();

private:
  /**
   * The underlying array of object managed by the pool.
   *
   * Although Array retains the object once when an object is added, proper
   * Ref::release() is called outside the array to make sure that the pool
   * does not affect the managed object's reference count. So an object can
   * be destructed properly by calling Ref::release() even if the object
   * is in the pool.
   */
  std::vector<Ref *> _managedObjectArray;
  string _name;

  /**
   *  The flag for checking whether the pool is doing `clear` operation.
   */
  bool _isClearing;
};

class RefPoolManager
{
public:
  static RefPoolManager *getInstance();

  static void destroyInstance();

  /**
   * Get current auto release pool, there is at least one auto release pool that created by engine.
   * You can create your own auto release pool at demand, which will be put into auto release pool stack.
   */
  AutoreleasePool *getCurrentPool() const;

  bool isObjectInPools(Ref *obj) const;

  friend class AutoreleasePool;

private:
  RefPoolManager();
  ~RefPoolManager();

  void push(AutoreleasePool *pool);
  void pop();

  static RefPoolManager *s_singleInstance;

  std::vector<AutoreleasePool *> _releasePoolStack;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_OBJECT_POOL_H_