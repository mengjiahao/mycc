
#ifndef MYCC_UTIL_OBJECT_POOL_H_
#define MYCC_UTIL_OBJECT_POOL_H_

#include <stdlib.h>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>
#include "atomic_util.h"
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

/*!
 * \brief Object pool for fast allocation and deallocation.
 */
template <typename T>
class SimpleObjectPool
{
public:
  /*!
   * \brief Destructor.
   */
  ~SimpleObjectPool();
  /*!
   * \brief Create new object.
   * \return Pointer to the new object.
   */
  template <typename... Args>
  T *New(Args &&... args);
  /*!
   * \brief Delete an existing object.
   * \param ptr The pointer to delete.
   *
   * Make sure the pointer to delete is allocated from this pool.
   */
  void Delete(T *ptr);

  /*!
   * \brief Get singleton instance of pool.
   * \return Object Pool.
   */
  static SimpleObjectPool *Get();

  /*!
   * \brief Get a shared ptr of the singleton instance of pool.
   * \return Shared pointer to the Object Pool.
   */
  static std::shared_ptr<SimpleObjectPool> _GetSharedRef();

private:
  /*!
   * \brief Internal structure to hold pointers.
   */
  struct LinkedList
  {
#if defined(_MSC_VER)
    T t;
    LinkedList *next{nullptr};
#else
    union {
      T t;
      LinkedList *next{nullptr};
    };
#endif
  };
  /*!
   * \brief Page size of allocation.
   *
   * Currently defined to be 4KB.
   */
  constexpr static std::size_t kPageSize = 1 << 12;
  /*! \brief internal mutex */
  std::mutex m_;
  /*!
   * \brief Head of free list.
   */
  LinkedList *head_{nullptr};
  /*!
   * \brief Pages allocated.
   */
  std::vector<void *> allocated_;
  /*!
   * \brief Private constructor.
   */
  SimpleObjectPool();
  /*!
   * \brief Allocate a page of raw objects.
   *
   * This function is not protected and must be called with caution.
   */
  void AllocateChunk();
  DISALLOW_COPY_AND_ASSIGN(SimpleObjectPool);
}; // class SimpleObjectPool

/*!
 * \brief Helper trait class for easy allocation and deallocation.
 */
template <typename T>
struct SimpleObjectPoolAllocatable
{
  /*!
   * \brief Create new object.
   * \return Pointer to the new object.
   */
  template <typename... Args>
  static T *New(Args &&... args);
  /*!
   * \brief Delete an existing object.
   * \param ptr The pointer to delete.
   *
   * Make sure the pointer to delete is allocated from this pool.
   */
  static void Delete(T *ptr);
}; // struct SimpleObjectPoolAllocatable

template <typename T>
SimpleObjectPool<T>::~SimpleObjectPool()
{
  // TODO(hotpxl): mind destruction order
  // for (auto i : allocated_) {
  //   free(i);
  // }
}

template <typename T>
template <typename... Args>
T *SimpleObjectPool<T>::New(Args &&... args)
{
  LinkedList *ret;
  {
    std::lock_guard<std::mutex> lock{m_};
    if (head_->next == nullptr)
    {
      AllocateChunk();
    }
    ret = head_;
    head_ = head_->next;
  }
  return new (static_cast<void *>(ret)) T(std::forward<Args>(args)...);
}

template <typename T>
void SimpleObjectPool<T>::Delete(T *ptr)
{
  ptr->~T();
  auto linked_list_ptr = reinterpret_cast<LinkedList *>(ptr);
  {
    std::lock_guard<std::mutex> lock{m_};
    linked_list_ptr->next = head_;
    head_ = linked_list_ptr;
  }
}

template <typename T>
SimpleObjectPool<T> *SimpleObjectPool<T>::Get()
{
  return _GetSharedRef().get();
}

template <typename T>
std::shared_ptr<SimpleObjectPool<T>> SimpleObjectPool<T>::_GetSharedRef()
{
  static std::shared_ptr<SimpleObjectPool<T>> inst_ptr(new SimpleObjectPool<T>());
  return inst_ptr;
}

template <typename T>
SimpleObjectPool<T>::SimpleObjectPool()
{
  AllocateChunk();
}

template <typename T>
void SimpleObjectPool<T>::AllocateChunk()
{
  static_assert(sizeof(LinkedList) <= kPageSize, "Object too big.");
  static_assert(sizeof(LinkedList) % alignof(LinkedList) == 0, "ObjectPooll Invariant");
  static_assert(alignof(LinkedList) % alignof(T) == 0, "ObjectPooll Invariant");
  static_assert(kPageSize % alignof(LinkedList) == 0, "ObjectPooll Invariant");
  void *new_chunk_ptr;

  int ret = posix_memalign(&new_chunk_ptr, kPageSize, kPageSize);
  assert(ret == 0); // "Allocation failed";

  allocated_.emplace_back(new_chunk_ptr);
  auto new_chunk = static_cast<LinkedList *>(new_chunk_ptr);
  auto size = kPageSize / sizeof(LinkedList);
  for (std::size_t i = 0; i < size - 1; ++i)
  {
    new_chunk[i].next = &new_chunk[i + 1];
  }
  new_chunk[size - 1].next = head_;
  head_ = new_chunk;
}

template <typename T>
template <typename... Args>
T *SimpleObjectPoolAllocatable<T>::New(Args &&... args)
{
  return SimpleObjectPool<T>::Get()->New(std::forward<Args>(args)...);
}

template <typename T>
void SimpleObjectPoolAllocatable<T>::Delete(T *ptr)
{
  SimpleObjectPool<T>::Get()->Delete(ptr);
}

// A general-purpose object pool
template <class obj_type>
class TCKnvObjPool;
template <class obj_type>
class TCKnvObjPoolR;

// Base object class for ObjPool
// Objects can be linked in a list through next member
class TCKnvObjBase
{
public:
  TCKnvObjBase() : prev(NULL), next(NULL) {}
  virtual ~TCKnvObjBase() {}

  void *prev; // previous object in obj
  void *next; // Next object in ObjPool

  virtual void ReleaseObject() = 0;
};

//
// Object pool for managing dynamically allocated objects
//
// The implementation is based on the use of pointers
// Restrictions for obj_type:
// 1, obj_type must contain members of curr and next of int type
// 2, obj_type must contain member function of ReleaseObject(), which will release the resources in the object
//
template <class obj_type>
class TCKnvObjPool
{
public:
  TCKnvObjPool() : obj_freelist(NULL) {}
  ~TCKnvObjPool()
  {
    while (obj_freelist)
    {
      obj_type *next = (obj_type *)obj_freelist->next;
      delete obj_freelist;
      obj_freelist = next;
    }
  }

  obj_type *New();           // Create standalone object (not in object list)
  int Delete(obj_type *obj); // Delete standalone object (not in object list)

  obj_type *New(obj_type *&first);             // Create object and insert at list tail pointed to by first, which must be initialized to NULL for new list
  obj_type *NewFront(obj_type *&first);        // Create object and insert at list head pointed to by first, which must be initialized to NULL for new list
  int Delete(obj_type *&first, obj_type *obj); // Delete object obj and remove from list pointed by first
  int Detach(obj_type *&first, obj_type *obj); // Remove object obj from list pointed by first, obj must be deleted with Delete(obj) when no longer in use
  int DeleteAll(obj_type *&first);             // Delete all objects in list pointed by first
  int AddToFreeList(obj_type *first);          // Add list to free list

private:
  obj_type *obj_freelist; // list for keeping released objects
};

//
// Implementation part
//

template <class obj_type>
inline obj_type *TCKnvObjPool<obj_type>::New()
{
  obj_type *o;

  if (obj_freelist)
  {
    o = obj_freelist;
    obj_freelist = (obj_type *)obj_freelist->next;
  }
  else
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
    try
    {
      o = new obj_type();
    }
    catch (...)
    {
      //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
      return NULL;
    }
  }

  // insert to obj list
  o->next = NULL;
  o->prev = o;
  return o;
}

template <class obj_type>
inline int TCKnvObjPool<obj_type>::Delete(obj_type *obj)
{
  obj->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers

  // put to free list
  obj->next = obj_freelist;
  obj_freelist = obj;
  return 0;
}

template <class obj_type>
inline obj_type *TCKnvObjPool<obj_type>::New(obj_type *&first)
{
  obj_type *o;

  if (obj_freelist)
  {
    o = obj_freelist;
    obj_freelist = (obj_type *)obj_freelist->next;
  }
  else
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
    try
    {
      o = new obj_type();
    }
    catch (...)
    {
      //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
      return NULL;
    }
  }

  // put to obj list
  o->next = NULL;
  if (first)
  {
    obj_type *lst = (obj_type *)first->prev; // must not be null
    lst->next = o;
    o->prev = lst;
    first->prev = o;
  }
  else
  {
    first = o;
    o->prev = o;
  }
  return o;
}

template <class obj_type>
inline obj_type *TCKnvObjPool<obj_type>::NewFront(obj_type *&first)
{
  obj_type *o;

  if (obj_freelist)
  {
    o = obj_freelist;
    obj_freelist = (obj_type *)obj_freelist->next;
  }
  else
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
    try
    {
      o = new obj_type();
    }
    catch (...)
    {
      //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
      return NULL;
    }
  }

  // put to obj list head
  o->next = first;
  if (first)
  {
    o->prev = first->prev;
    first->prev = o;
  }
  else
  {
    o->prev = o;
  }
  first = o;
  return o;
}

template <class obj_type>
inline int TCKnvObjPool<obj_type>::Delete(obj_type *&first, obj_type *obj)
{
  if (obj == first)
  {
    if (obj->next) // new first
      ((obj_type *)obj->next)->prev = first->prev;
    first = (obj_type *)obj->next;
  }
  else
  {
    obj_type *p = (obj_type *)obj->prev; // must not null
    obj_type *n = (obj_type *)obj->next;
    p->next = n;
    if (n)
      n->prev = p;
    else // obj is the last
      first->prev = p;
  }

  obj->ReleaseObject();

  obj->next = obj_freelist;
  obj_freelist = obj;

  return 0;
}

template <class obj_type>
inline int TCKnvObjPool<obj_type>::Detach(obj_type *&first, obj_type *obj)
{
  if (obj == first)
  {
    if (obj->next) // new first
      ((obj_type *)obj->next)->prev = first->prev;
    first = (obj_type *)obj->next;
  }
  else
  {
    obj_type *p = (obj_type *)obj->prev; // must not null
    obj_type *n = (obj_type *)obj->next;
    p->next = n;
    if (n)
      n->prev = p;
    else // obj is the last
      first->prev = p;
  }

  obj->next = obj->prev = NULL;
  return 0;
}

template <class obj_type>
inline int TCKnvObjPool<obj_type>::AddToFreeList(obj_type *obj)
{
  ((obj_type *)obj->prev)->next = obj_freelist;
  obj_freelist = obj;
  return 0;
}

template <class obj_type>
inline int TCKnvObjPool<obj_type>::DeleteAll(obj_type *&first)
{
  obj_type *o = first;
  while (o)
  {
    o->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers
    o = (obj_type *)o->next;
  }

  // put obj to free list
  AddToFreeList(first);
  first = NULL;
  return 0;
}

//
// Object pool for managing dynamically allocated objects.
// A general-purpose object pool, muti-thread safe version.
// The implementation is based on the use of pointers
// Restrictions for obj_type:
// 1, obj_type must contain members of curr and next of int type
// 2, obj_type must contain member function of ReleaseObject(), which will release the resources in the object
//
template <class obj_type>
class TCKnvObjPoolR
{
public:
  TCKnvObjPoolR() : obj_freelist(NULL) {}
  ~TCKnvObjPoolR()
  {
    while (obj_freelist)
    {
      obj_type *next = (obj_type *)obj_freelist->next;
      delete obj_freelist;
      obj_freelist = next;
    }
  }

  obj_type *New();           // Create standalone object (not in object list)
  int Delete(obj_type *obj); // Delete standalone object (not in object list)

  obj_type *New(obj_type *&first);             // Create object and insert at list tail pointed to by first, which must be initialized to NULL for new list
  obj_type *NewFront(obj_type *&first);        // Create object and insert at list head pointed to by first, which must be initialized to NULL for new list
  int Delete(obj_type *&first, obj_type *obj); // Delete object obj and remove from list pointed by first
  int Detach(obj_type *&first, obj_type *obj); // Remove object obj from list pointed by first, obj must be deleted with Delete(obj) when no longer in use
  int DeleteAll(obj_type *&first);             // Delete all objects in list pointed by first
  int AddToFreeList(obj_type *first);          // Add list to free list

private:
  volatile obj_type *obj_freelist; // list for keeping released objects
};

//
// Implementation part
//

template <class obj_type>
inline obj_type *TCKnvObjPoolR<obj_type>::New()
{
  obj_type *n;
  volatile obj_type *o = obj_freelist;
  while (o)
  {
    n = (obj_type *)o->next;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, n))
    {
    out:
      // insert to obj list
      o->next = NULL;
      o->prev = (void *)o;
      return (obj_type *)o;
    }
    o = obj_freelist;
  }

  //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
  try
  {
    o = new obj_type();
  }
  catch (...)
  {
    o = NULL;
  }
  if (o == NULL)
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
    return NULL;
  }
  goto out;
}

template <class obj_type>
inline int TCKnvObjPoolR<obj_type>::Delete(obj_type *obj)
{
  obj->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers

  // put to free list
  while (true)
  {
    volatile obj_type *o = obj_freelist;
    obj->next = (void *)o;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, obj))
      return 0;
  }
  return 0;
}

template <class obj_type>
inline obj_type *TCKnvObjPoolR<obj_type>::New(obj_type *&first)
{
  obj_type *n;
  volatile obj_type *o = obj_freelist;
  while (o)
  {
    n = (obj_type *)o->next;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, n))
    {
    out:
      // insert to obj list
      o->next = NULL;
      if (first)
      {
        obj_type *lst = (obj_type *)first->prev; // must not be null
        lst->next = (void *)o;
        o->prev = lst;
        first->prev = (void *)o;
      }
      else
      {
        first = (obj_type *)o;
        o->prev = (void *)o;
      }
      return (obj_type *)o;
    }
    o = obj_freelist;
  }

  //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
  try
  {
    o = new obj_type();
  }
  catch (...)
  {
    o = NULL;
  }
  if (o == NULL)
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
    return NULL;
  }
  goto out;
}

template <class obj_type>
inline obj_type *TCKnvObjPoolR<obj_type>::NewFront(obj_type *&first)
{
  obj_type *n;
  volatile obj_type *o = obj_freelist;
  while (o)
  {
    n = (obj_type *)o->next;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, n))
    {
    out:
      // insert to obj list
      o->next = first;
      if (first)
      {
        o->prev = first->prev;
        first->prev = (void *)o;
      }
      else
      {
        o->prev = (void *)o;
      }
      first = (obj_type *)o;
      return (obj_type *)o;
    }
    o = obj_freelist;
  }

  //Attr_API(ATTR_OBJ_POOL_NEW_OBJ, 1);
  try
  {
    o = new obj_type();
  }
  catch (...)
  {
    o = NULL;
  }
  if (o == NULL)
  {
    //Attr_API(ATTR_OBJ_POOL_NEW_OBJ_FAIL, 1);
    return NULL;
  }
  goto out;
}

template <class obj_type>
inline int TCKnvObjPoolR<obj_type>::Delete(obj_type *&first, obj_type *obj)
{
  if (obj == first)
  {
    if (obj->next) // new first
      ((obj_type *)obj->next)->prev = first->prev;
    first = (obj_type *)obj->next;
  }
  else
  {
    obj_type *p = (obj_type *)obj->prev; // must not null
    obj_type *n = (obj_type *)obj->next;
    p->next = n;
    if (n)
      n->prev = p;
    else // obj is the last
      first->prev = p;
  }

  obj->ReleaseObject();

  // put obj to free list
  while (true)
  {
    volatile obj_type *o = obj_freelist;
    obj->next = (void *)o;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, obj))
      return 0;
  }

  return 0;
}

template <class obj_type>
inline int TCKnvObjPoolR<obj_type>::Detach(obj_type *&first, obj_type *obj)
{
  if (obj == first)
  {
    if (obj->next) // new first
      ((obj_type *)obj->next)->prev = first->prev;
    first = (obj_type *)obj->next;
  }
  else
  {
    obj_type *p = (obj_type *)obj->prev; // must not null
    obj_type *n = (obj_type *)obj->next;
    p->next = n;
    if (n)
      n->prev = p;
    else // obj is the last
      first->prev = p;
  }

  obj->prev = obj->next = NULL;
  return 0;
}

template <class obj_type>
inline int TCKnvObjPoolR<obj_type>::AddToFreeList(obj_type *obj)
{
  // put obj list to free list
  while (true)
  {
    volatile obj_type *o = obj_freelist;
    ((obj_type *)obj->prev)->next = (void *)o;
    if (GCC_SYNC_BOOL_COMPARE_AND_SWAP(&obj_freelist, o, obj))
      return 0;
  }
  return 0;
}

template <class obj_type>
inline int TCKnvObjPoolR<obj_type>::DeleteAll(obj_type *&first)
{
  obj_type *o = first;
  while (o)
  {
    o->ReleaseObject(); // NOTE: ReleaseObject() should not change prev/next pointers
    o = (obj_type *)o->next;
  }

  // put to free list
  AddToFreeList(first);
  first = NULL;
  return 0;
}

/**
 * A pool for managing autorelease objects.
 */
class CCRefAutoreleasePool
{
public:
  /**
   * @warning Don't create an autorelease pool in heap, create it in stack.
   */
  CCRefAutoreleasePool();

  /**
   * Create an autorelease pool with specific name. This name is useful for debugging.
   * @warning Don't create an autorelease pool in heap, create it in stack.
   * @param name The name of created autorelease pool.
   */
  CCRefAutoreleasePool(const string &name);

  ~CCRefAutoreleasePool();

  /**
   * Add a given object to this autorelease pool.
   *
   * The same object may be added several times to an autorelease pool. When the
   * pool is destructed, the object's `CCRef::release()` method will be called
   * the same times as it was added.
   *
   * @param object    The object to be added into the autorelease pool.
   */
  void addObject(CCRef *object);

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
  bool contains(CCRef *object) const;

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
   * CCRef::release() is called outside the array to make sure that the pool
   * does not affect the managed object's reference count. So an object can
   * be destructed properly by calling CCRef::release() even if the object
   * is in the pool.
   */
  std::vector<CCRef *> _managedObjectArray;
  string _name;

  /**
   *  The flag for checking whether the pool is doing `clear` operation.
   */
  bool _isClearing;
};

class CCRefPoolManager
{
public:
  static CCRefPoolManager *getInstance();

  static void destroyInstance();

  /**
   * Get current auto release pool, there is at least one auto release pool that created by engine.
   * You can create your own auto release pool at demand, which will be put into auto release pool stack.
   */
  CCRefAutoreleasePool *getCurrentPool() const;

  bool isObjectInPools(CCRef *obj) const;

  friend class CCRefAutoreleasePool;

private:
  CCRefPoolManager();
  ~CCRefPoolManager();

  void push(CCRefAutoreleasePool *pool);
  void pop();

  static CCRefPoolManager *s_singleInstance;

  std::vector<CCRefAutoreleasePool *> _releasePoolStack;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_OBJECT_POOL_H_