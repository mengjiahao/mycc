
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

// Trigger an assert if the reference count is 0 but the CCRef is still in autorelease pool.
// This happens when 'autorelease/release' were not used in pairs with 'new/retain'.
//
// Wrong usage (1):
//
// auto obj = Node::create();   // CCRef = 1, but it's an autorelease CCRef which means it was in the autorelease pool.
// obj->autorelease();   // Wrong: If you wish to invoke autorelease several times, you should retain `obj` first.
//
// Wrong usage (2):
//
// auto obj = Node::create();
// obj->release();   // Wrong: obj is an autorelease CCRef, it will be released when clearing current pool.
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

#define DEBUG_CCREF_LEAK_DECTECTION 1

static std::vector<CCRef *> g_ccrefAllocationList;
static std::mutex g_ccrefMutex;

static void TrackCCRef(CCRef *ref)
{
  std::lock_guard<std::mutex> refLockGuard(g_ccrefMutex);
  //CCASSERT(ref, "Invalid parameter, ref should not be null!");

  // Create memory allocation record.
  g_ccrefAllocationList.push_back(ref);
}

static void untrackRef(CCRef *ref)
{
  std::lock_guard<std::mutex> refLockGuard(g_ccrefMutex);
  auto iter = std::find(g_ccrefAllocationList.begin(), g_ccrefAllocationList.end(), ref);
  if (iter == g_ccrefAllocationList.end())
  {
    //log("[memory] CORRUPTION: Attempting to free (%s) with invalid ref tracking record.\n", typeid(*ref).name());
    return;
  }

  g_ccrefAllocationList.erase(iter);
}

CCRef::CCRef()
    : _referenceCount(1) // when the CCRef is created, the reference count of it is 1
{
#if DEBUG_CCREF_LEAK_DECTECTION
  TrackCCRef(this);
#endif
}

CCRef::~CCRef()
{
#if DEBUG_CCREF_LEAK_DECTECTION
  if (_referenceCount != 0)
    untrackRef(this);
#endif
}

void CCRef::retain()
{
  //CCASSERT(_referenceCount > 0, "reference count should be greater than 0");
  ++_referenceCount;
}

void CCRef::release()
{
  //CCASSERT(_referenceCount > 0, "reference count should be greater than 0");
  --_referenceCount;

  if (_referenceCount == 0)
  {
#if !defined(NDEBUG)
    auto poolManager = CCRefPoolManager::getInstance(); 
    if (!poolManager->getCurrentPool()->isClearing() && poolManager->isObjectInPools(this))
    {
      assert(false);
      //CCASSERT(false, "The reference shouldn't be 0 because it is still in autorelease pool.");
    }
#endif

#if DEBUG_CCREF_LEAK_DECTECTION
    untrackRef(this);
#endif
    delete this;
  }
}

CCRef *CCRef::autorelease()
{
  CCRefPoolManager::getInstance()->getCurrentPool()->addObject(this);
  return this;
}

uint32_t CCRef::getReferenceCount() const
{
  return _referenceCount;
}

void CCRef::printLeaks()
{
  std::lock_guard<std::mutex> refLockGuard(g_ccrefMutex);
  // Dump CCRef object memory leaks
  if (g_ccrefAllocationList.empty())
  {
    //log("[memory] All CCRef objects successfully cleaned up (no leaks detected).\n");
  }
  else
  {
    //log("[memory] WARNING: %d CCRef objects still active in memory.\n", (int)g_ccrefAllocationList.size());
    for (const auto &ref : g_ccrefAllocationList)
    {
      assert(ref);
      //const char *type = typeid(*ref).name();
      //log("[memory] LEAK: CCRef object '%s' still active with reference count %d.\n", (type ? type : ""), ref->getReferenceCount());
    }
  }
}

////////////// CCRefObjectFactory ////////////////

CCRefObjectFactory::TInfo::TInfo(void)
    : _class(""), _fun(nullptr), _func(nullptr)
{
}

CCRefObjectFactory::TInfo::TInfo(const string &type, Instance ins)
    : _class(type), _fun(ins), _func(nullptr)
{
  CCRefObjectFactory::getInstance()->registerType(*this);
}

CCRefObjectFactory::TInfo::TInfo(const string &type, InstanceFunc ins)
    : _class(type), _fun(nullptr), _func(ins)
{
  CCRefObjectFactory::getInstance()->registerType(*this);
}

CCRefObjectFactory::TInfo::TInfo(const TInfo &t)
{
  _class = t._class;
  _fun = t._fun;
  _func = t._func;
}

CCRefObjectFactory::TInfo::~TInfo(void)
{
  _class = "";
  _fun = nullptr;
  _func = nullptr;
}

CCRefObjectFactory::TInfo &CCRefObjectFactory::TInfo::operator=(const TInfo &t)
{
  _class = t._class;
  _fun = t._fun;
  _func = t._func;
  return *this;
}

CCRefObjectFactory *CCRefObjectFactory::_sharedFactory = nullptr;

CCRefObjectFactory::CCRefObjectFactory(void)
{
}

CCRefObjectFactory::~CCRefObjectFactory(void)
{
  _typeMap.clear();
}

CCRefObjectFactory *CCRefObjectFactory::getInstance()
{
  if (nullptr == _sharedFactory)
  {
    _sharedFactory = new (std::nothrow) CCRefObjectFactory();
  }
  return _sharedFactory;
}

void CCRefObjectFactory::destroyInstance()
{
  delete _sharedFactory;
  _sharedFactory = nullptr;
}

CCRef *CCRefObjectFactory::createObject(const string &name)
{
  CCRef *o = nullptr;
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

void CCRefObjectFactory::registerType(const TInfo &t)
{
  _typeMap.emplace(t._class, t);
}

} // namespace util
} // namespace mycc