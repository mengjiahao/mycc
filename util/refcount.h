
#ifndef MYCC_UTIL_REFCOUNT_H_
#define MYCC_UTIL_REFCOUNT_H_

#include <atomic>
#include <assert.h>
#include <functional>
#include <unordered_map>
#include "atomic_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class RefCountedBase
{
public:
  bool RefCountIsOne() const { return ref_count_ == 1; }

protected:
  RefCountedBase();
  ~RefCountedBase();

  void AddRef() const;

  // Returns true if the object should self-delete.
  bool Unref() const;

private:
  mutable int ref_count_;
#ifndef NDEBUG
  mutable bool in_dtor_;
#endif

  DISALLOW_COPY_AND_ASSIGN(RefCountedBase);
};

//
// A base class for reference counted classes.  Otherwise, known as a cheap
// knock-off of WebKit's RefCounted<T> class.  To use this guy just extend your
// class from it like so:
//
//   class MyFoo : public RefCounted<MyFoo> {
//    ...
//    private:
//     friend class RefCounted<MyFoo>;
//     ~MyFoo();
//   };
//
// You should always make your destructor private, to avoid any code deleting
// the object accidently while there are references to it.

class RefCounted
{
public:
  // Initial reference count is one.
  RefCounted();

  // Increments reference count by one.
  void Ref() const;

  // Decrements reference count by one.  If the count remains
  // positive, returns false.  When the count reaches zero, returns
  // true and deletes this, in which case the caller must not access
  // the object afterward.
  bool Unref() const;

  // Return whether the reference count is one.
  // If the reference count is used in the conventional way, a
  // reference count of 1 implies that the current thread owns the
  // reference and no other thread shares it.
  // This call performs the test for a reference count of one, and
  // performs the memory barrier needed for the owning thread
  // to act on the object, knowing that it has exclusive access to the
  // object.
  bool RefCountIsOne() const;

protected:
  // Make destructor protected so that RefCounted objects cannot
  // be instantiated directly. Only subclasses can be instantiated.
  virtual ~RefCounted();

private:
  mutable std::atomic_int_fast32_t ref_;

  DISALLOW_COPY_AND_ASSIGN(RefCounted);
};

// Inlined routines, since these are performance critical
inline RefCounted::RefCounted() : ref_(1) {}

inline RefCounted::~RefCounted() { assert(ref_.load() == 0); }

inline void RefCounted::Ref() const
{
  //DCHECK_GE(ref_.load(), 1);
  ref_.fetch_add(1, std::memory_order_relaxed);
}

inline bool RefCounted::Unref() const
{
  //DCHECK_GT(ref_.load(), 0);
  // If ref_==1, this object is owned only by the caller. Bypass a locked op
  // in that case.
  if (RefCountIsOne() || ref_.fetch_sub(1) == 1)
  {
    // Make DCHECK in ~RefCounted happy
    //DCHECK((ref_.store(0), true));
    delete this;
    return true;
  }
  else
  {
    return false;
  }
}

inline bool RefCounted::RefCountIsOne() const
{
  return (ref_.load(std::memory_order_acquire) == 1);
}

// Helper class to unref an object when out-of-scope.
class ScopedUnref
{
public:
  explicit ScopedUnref(RefCounted *o) : obj_(o) {}
  ~ScopedUnref()
  {
    if (obj_)
      obj_->Unref();
  }

private:
  RefCounted *obj_;

  DISALLOW_COPY_AND_ASSIGN(ScopedUnref);
};

//
// A smart pointer class for reference counted objects.  Use this class instead
// of calling AddRef and Release manually on a reference counted object to
// avoid common memory leaks caused by forgetting to Release an object
// reference.  Sample usage:
//
//   class MyFoo : public RefCounted<MyFoo> {
//    ...
//   };
//
//   void some_function() {
//     ScopedRefCountedPtr<MyFoo> foo = new MyFoo();
//     foo->Method(param);
//     // |foo| is released when this function returns
//   }
//
//   void some_other_function() {
//     ScopedRefCountedPtr<MyFoo> foo = new MyFoo();
//     ...
//     foo = NULL;  // explicitly releases |foo|
//     ...
//     if (foo)
//       foo->Method(param);
//   }
//
// The above examples show how ScopedRefCountedPtr<T> acts like a pointer to T.
// Given two ScopedRefCountedPtr<T> classes, it is also possible to exchange
// references between the two objects, like so:
//
//   {
//     ScopedRefCountedPtr<MyFoo> a = new MyFoo();
//     ScopedRefCountedPtr<MyFoo> b;
//
//     b.swap(a);
//     // now, |b| references the MyFoo object, and |a| references NULL.
//   }
//
// To make both |a| and |b| in the above example reference the same MyFoo
// object, simply use the assignment operator:
//
//   {
//     ScopedRefCountedPtr<MyFoo> a = new MyFoo();
//     ScopedRefCountedPtr<MyFoo> b;
//
//     b = a;
//     // now, |a| and |b| each own a reference to the same MyFoo object.
//   }
//
template <class T>
class ScopedRefCountedPtr
{
public:
  typedef T element_type;

  ScopedRefCountedPtr() : ptr_(NULL)
  {
  }

  ScopedRefCountedPtr(T *p) : ptr_(p)
  {
    if (ptr_)
      ptr_->AddRef();
  }

  ScopedRefCountedPtr(const ScopedRefCountedPtr<T> &r) : ptr_(r.ptr_)
  {
    if (ptr_)
      ptr_->AddRef();
  }

  template <typename U>
  ScopedRefCountedPtr(const ScopedRefCountedPtr<U> &r) : ptr_(r.get())
  {
    if (ptr_)
      ptr_->AddRef();
  }

  ~ScopedRefCountedPtr()
  {
    if (ptr_)
      ptr_->Unref();
  }

  T *get() const { return ptr_; }

  // Allow ScopedRefCountedPtr<C> to be used in boolean expression
  // and comparison operations.
  operator T *() const { return ptr_; }

  T *operator->() const
  {
    assert(ptr_ != NULL);
    return ptr_;
  }

  ScopedRefCountedPtr<T> &operator=(T *p)
  {
    // AddRef first so that self assignment should work
    if (p)
      p->AddRef();
    T *old_ptr = ptr_;
    ptr_ = p;
    if (old_ptr)
      old_ptr->Unref();
    return *this;
  }

  ScopedRefCountedPtr<T> &operator=(const ScopedRefCountedPtr<T> &r)
  {
    return *this = r.ptr_;
  }

  template <typename U>
  ScopedRefCountedPtr<T> &operator=(const ScopedRefCountedPtr<U> &r)
  {
    return *this = r.get();
  }

  void swap(T **pp)
  {
    T *p = ptr_;
    ptr_ = *pp;
    *pp = p;
  }

  void swap(ScopedRefCountedPtr<T> &r)
  {
    swap(&r.ptr_);
  }

  // Release ownership of ptr_, keeping its reference counter unchanged.
  T *release() WARN_UNUSED_RESULT
  {
    T *saved_ptr = NULL;
    swap(&saved_ptr);
    return saved_ptr;
  }

protected:
  T *ptr_;
};

// Handy utility for creating a ScopedRefCountedPtr<T> out of a T* explicitly without
// having to retype all the template arguments
template <typename T>
ScopedRefCountedPtr<T> MakeScopedRefCountedPtr(T *t)
{
  return ScopedRefCountedPtr<T>(t);
}

////////////////////// Ref //////////////////////////////////////

class Ref;

/** 
  * Interface that defines how to clone an Ref.
  */
class RefClonable
{
public:
  /** Returns a copy of the Ref. */
  virtual RefClonable *clone() const = 0;

  virtual ~RefClonable(){};
};

/**
 * Ref is used for reference count management. If a class inherits from Ref,
 * then it is easy to be shared in different places.
 */
class Ref
{
public:
  /**
    * Retains the ownership.
    *
    * This increases the Ref's reference count.
    *
    * @see release, autorelease
    */
  void retain();

  /**
    * Releases the ownership immediately.
    *
    * This decrements the Ref's reference count.
    *
    * If the reference count reaches 0 after the decrement, this Ref is
    * destructed.
    *
    * @see retain, autorelease
    */
  void release();

  /**
    * Releases the ownership sometime soon automatically.
    *
    * This decrements the Ref's reference count at the end of current
    * autorelease pool block.
    *
    * If the reference count reaches 0 after the decrement, this Ref is
    * destructed.
    *
    * @returns The Ref itself.
    *
    * @see AutoreleasePool, retain, release
    */
  Ref *autorelease();

  /**
    * Returns the Ref's current reference count.
    *
    * @returns The Ref's reference count.
    */
  uint32_t getReferenceCount() const;

protected:
  /**
    * Constructor
    * The Ref's reference count is 1 after construction.
    */
  Ref();

public:
  virtual ~Ref();

protected:
  /// count of references
  uint32_t _referenceCount;

  friend class AutoreleasePool;

  // Memory leak diagnostic data (only included when CC_REF_LEAK_DETECTION is defined and its value isn't zero)
public:
  static void printLeaks();
};

class RefObjectFactory
{
public:
  typedef Ref *(*Instance)(void);
  typedef std::function<Ref *(void)> InstanceFunc;
  struct TInfo
  {
    TInfo(void);
    TInfo(const string &type, Instance ins = nullptr);
    TInfo(const string &type, InstanceFunc ins = nullptr);
    TInfo(const TInfo &t);
    ~TInfo(void);
    TInfo &operator=(const TInfo &t);
    string _class;
    Instance _fun;
    InstanceFunc _func;
  };
  typedef std::unordered_map<string, TInfo> FactoryMap;

  static RefObjectFactory *getInstance();
  static void destroyInstance();
  Ref *createObject(const string &name);

  void registerType(const TInfo &t);
  void removeAll();

protected:
  RefObjectFactory(void);
  virtual ~RefObjectFactory(void);

private:
  static RefObjectFactory *_sharedFactory;
  FactoryMap _typeMap;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_REFCOUNT_H_