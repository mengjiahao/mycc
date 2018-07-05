
#include "refcount.h"
#include <algorithm>
#include <mutex>
#include <thread>
#include <vector>
#include "object_pool.h"

namespace mycc
{
namespace util
{

RefCountedBase::RefCountedBase()
    : ref_count_(0)
#ifndef NDEBUG
      ,
      in_dtor_(false)
#endif
{
}

RefCountedBase::~RefCountedBase()
{
  //DCHECK(in_dtor_) << "RefCounted object deleted without calling Release()";
}

void RefCountedBase::AddRef() const
{
  // TODO(maruel): Add back once it doesn't assert 500 times/sec.
  // Current thread books the critical section "AddRelease" without release it.
  // DFAKE_SCOPED_LOCK_THREAD_LOCKED(add_release_);
  //DCHECK(!in_dtor_);
  ++ref_count_;
}

bool RefCountedBase::Unref() const
{
  // TODO(maruel): Add back once it doesn't assert 500 times/sec.
  // Current thread books the critical section "AddRelease" without release it.
  // DFAKE_SCOPED_LOCK_THREAD_LOCKED(add_release_);
  //DCHECK(!in_dtor_);
  if (--ref_count_ == 0)
  {
#ifndef NDEBUG
    in_dtor_ = true;
#endif
    return true;
  }
  return false;
}

// Trigger an assert if the reference count is 0 but the Ref is still in autorelease pool.
// This happens when 'autorelease/release' were not used in pairs with 'new/retain'.
//
// Wrong usage (1):
//
// auto obj = Node::create();   // Ref = 1, but it's an autorelease Ref which means it was in the autorelease pool.
// obj->autorelease();   // Wrong: If you wish to invoke autorelease several times, you should retain `obj` first.
//
// Wrong usage (2):
//
// auto obj = Node::create();
// obj->release();   // Wrong: obj is an autorelease Ref, it will be released when clearing current pool.
//
// Correct usage (1):
//
// auto obj = Node::create();
//                     |-   new Node();     // `new` is the pair of the `autorelease` of next line
//                     |-   autorelease();  // The pair of `new Node`.
//
// obj->retain();
// obj->autorelease();  // This `autorelease` is the pair of `retain` of previous line.
//
// Correct usage (2):
//
// auto obj = Node::create();
// obj->retain();
// obj->release();   // This `release` is the pair of `retain` of previous line.

#define DEBUG_REF_LEAK_DECTECTION 1

static std::vector<Ref *> __refAllocationList;
static std::mutex __refMutex;

static void trackRef(Ref *ref)
{
  std::lock_guard<std::mutex> refLockGuard(__refMutex);
  //CCASSERT(ref, "Invalid parameter, ref should not be null!");

  // Create memory allocation record.
  __refAllocationList.push_back(ref);
}

static void untrackRef(Ref *ref)
{
  std::lock_guard<std::mutex> refLockGuard(__refMutex);
  auto iter = std::find(__refAllocationList.begin(), __refAllocationList.end(), ref);
  if (iter == __refAllocationList.end())
  {
    //log("[memory] CORRUPTION: Attempting to free (%s) with invalid ref tracking record.\n", typeid(*ref).name());
    return;
  }

  __refAllocationList.erase(iter);
}

Ref::Ref()
    : _referenceCount(1) // when the Ref is created, the reference count of it is 1
{
#if DEBUG_REF_LEAK_DECTECTION
  trackRef(this);
#endif
}

Ref::~Ref()
{
#if DEBUG_REF_LEAK_DECTECTION
  if (_referenceCount != 0)
    untrackRef(this);
#endif
}

void Ref::retain()
{
  //CCASSERT(_referenceCount > 0, "reference count should be greater than 0");
  ++_referenceCount;
}

void Ref::release()
{
  //CCASSERT(_referenceCount > 0, "reference count should be greater than 0");
  --_referenceCount;

  if (_referenceCount == 0)
  {
#if !defined(NDEBUG)
    auto poolManager = RefPoolManager::getInstance();
    if (!poolManager->getCurrentPool()->isClearing() && poolManager->isObjectInPools(this))
    {
      assert(false);
      //CCASSERT(false, "The reference shouldn't be 0 because it is still in autorelease pool.");
    }
#endif

#if DEBUG_REF_LEAK_DECTECTION
    untrackRef(this);
#endif
    delete this;
  }
}

Ref *Ref::autorelease()
{
  RefPoolManager::getInstance()->getCurrentPool()->addObject(this);
  return this;
}

uint32_t Ref::getReferenceCount() const
{
  return _referenceCount;
}

void Ref::printLeaks()
{
  std::lock_guard<std::mutex> refLockGuard(__refMutex);
  // Dump Ref object memory leaks
  if (__refAllocationList.empty())
  {
    //log("[memory] All Ref objects successfully cleaned up (no leaks detected).\n");
  }
  else
  {
    //log("[memory] WARNING: %d Ref objects still active in memory.\n", (int)__refAllocationList.size());
    for (const auto &ref : __refAllocationList)
    {
      assert(ref);
      //const char *type = typeid(*ref).name();
      //log("[memory] LEAK: Ref object '%s' still active with reference count %d.\n", (type ? type : ""), ref->getReferenceCount());
    }
  }
}

////////////// RefObjectFactory ////////////////

RefObjectFactory::TInfo::TInfo(void)
    : _class(""), _fun(nullptr), _func(nullptr)
{
}

RefObjectFactory::TInfo::TInfo(const string &type, Instance ins)
    : _class(type), _fun(ins), _func(nullptr)
{
  RefObjectFactory::getInstance()->registerType(*this);
}

RefObjectFactory::TInfo::TInfo(const string &type, InstanceFunc ins)
    : _class(type), _fun(nullptr), _func(ins)
{
  RefObjectFactory::getInstance()->registerType(*this);
}

RefObjectFactory::TInfo::TInfo(const TInfo &t)
{
  _class = t._class;
  _fun = t._fun;
  _func = t._func;
}

RefObjectFactory::TInfo::~TInfo(void)
{
  _class = "";
  _fun = nullptr;
  _func = nullptr;
}

RefObjectFactory::TInfo &RefObjectFactory::TInfo::operator=(const TInfo &t)
{
  _class = t._class;
  _fun = t._fun;
  _func = t._func;
  return *this;
}

RefObjectFactory *RefObjectFactory::_sharedFactory = nullptr;

RefObjectFactory::RefObjectFactory(void)
{
}

RefObjectFactory::~RefObjectFactory(void)
{
  _typeMap.clear();
}

RefObjectFactory *RefObjectFactory::getInstance()
{
  if (nullptr == _sharedFactory)
  {
    _sharedFactory = new (std::nothrow) RefObjectFactory();
  }
  return _sharedFactory;
}

void RefObjectFactory::destroyInstance()
{
  delete _sharedFactory;
  _sharedFactory = nullptr;
}

Ref *RefObjectFactory::createObject(const string &name)
{
  Ref *o = nullptr;
  do
  {
    const TInfo t = _typeMap[name];
    if (t._fun != nullptr)
    {
      o = t._fun();
    }
    else if (t._func != nullptr)
    {
      o = t._func();
    }
  } while (0);

  return o;
}

void RefObjectFactory::registerType(const TInfo &t)
{
  _typeMap.emplace(t._class, t);
}

} // namespace util
} // namespace mycc