
#include "object_pool.h"

namespace mycc
{
namespace util
{

CCRefAutoreleasePool::CCRefAutoreleasePool()
    : _name(""),
      _isClearing(false)
{
  _managedObjectArray.reserve(150);
  CCRefPoolManager::getInstance()->push(this);
}

CCRefAutoreleasePool::CCRefAutoreleasePool(const string &name)
    : _name(name),
      _isClearing(false)
{
  _managedObjectArray.reserve(150);
  CCRefPoolManager::getInstance()->push(this);
}

CCRefAutoreleasePool::~CCRefAutoreleasePool()
{
  //CCLOGINFO("deallocing CCRefAutoreleasePool: %p", this);
  clear();

  CCRefPoolManager::getInstance()->pop();
}

void CCRefAutoreleasePool::addObject(CCRef *object)
{
  _managedObjectArray.push_back(object);
}

void CCRefAutoreleasePool::clear()
{
  _isClearing = true;

  std::vector<CCRef *> releasings;
  releasings.swap(_managedObjectArray);
  for (const auto &obj : releasings)
  {
    obj->release();
  }

  _isClearing = false;
}

bool CCRefAutoreleasePool::contains(CCRef *object) const
{
  for (const auto &obj : _managedObjectArray)
  {
    if (obj == object)
      return true;
  }
  return false;
}

void CCRefAutoreleasePool::dump()
{
  //CCLOG("autorelease pool: %s, number of managed object %d\n", _name.c_str(), static_cast<int>(_managedObjectArray.size()));
  //CCLOG("%20s%20s%20s", "Object pointer", "Object id", "reference count");
  for (const auto &obj : _managedObjectArray)
  {
    printf("%20p%20u\n", obj, obj->getReferenceCount());
  }
}

//--------------------------------------------------------------------
//
// CCRefPoolManager
//
//--------------------------------------------------------------------

CCRefPoolManager *CCRefPoolManager::s_singleInstance = nullptr;

CCRefPoolManager *CCRefPoolManager::getInstance()
{
  if (s_singleInstance == nullptr)
  {
    s_singleInstance = new (std::nothrow) CCRefPoolManager();
    // Add the first auto release pool
    new CCRefAutoreleasePool("cocos2d autorelease pool");
  }
  return s_singleInstance;
}

void CCRefPoolManager::destroyInstance()
{
  delete s_singleInstance;
  s_singleInstance = nullptr;
}

CCRefPoolManager::CCRefPoolManager()
{
  _releasePoolStack.reserve(10);
}

CCRefPoolManager::~CCRefPoolManager()
{
  //CCLOGINFO("deallocing CCRefPoolManager: %p", this);

  while (!_releasePoolStack.empty())
  {
    CCRefAutoreleasePool *pool = _releasePoolStack.back();

    delete pool;
  }
}

CCRefAutoreleasePool *CCRefPoolManager::getCurrentPool() const
{
  return _releasePoolStack.back();
}

bool CCRefPoolManager::isObjectInPools(CCRef *obj) const
{
  for (const auto &pool : _releasePoolStack)
  {
    if (pool->contains(obj))
      return true;
  }
  return false;
}

void CCRefPoolManager::push(CCRefAutoreleasePool *pool)
{
  _releasePoolStack.push_back(pool);
}

void CCRefPoolManager::pop()
{
  assert(!_releasePoolStack.empty());
  _releasePoolStack.pop_back();
}

} // namespace util
} // namespace mycc