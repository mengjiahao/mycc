
#include "object_pool.h"

namespace mycc
{
namespace util
{

AutoreleasePool::AutoreleasePool()
    : _name(""),
      _isClearing(false)
{
  _managedObjectArray.reserve(150);
  RefPoolManager::getInstance()->push(this);
}

AutoreleasePool::AutoreleasePool(const string &name)
    : _name(name),
      _isClearing(false)
{
  _managedObjectArray.reserve(150);
  RefPoolManager::getInstance()->push(this);
}

AutoreleasePool::~AutoreleasePool()
{
  //CCLOGINFO("deallocing AutoreleasePool: %p", this);
  clear();

  RefPoolManager::getInstance()->pop();
}

void AutoreleasePool::addObject(Ref *object)
{
  _managedObjectArray.push_back(object);
}

void AutoreleasePool::clear()
{
  _isClearing = true;

  std::vector<Ref *> releasings;
  releasings.swap(_managedObjectArray);
  for (const auto &obj : releasings)
  {
    obj->release();
  }

  _isClearing = false;
}

bool AutoreleasePool::contains(Ref *object) const
{
  for (const auto &obj : _managedObjectArray)
  {
    if (obj == object)
      return true;
  }
  return false;
}

void AutoreleasePool::dump()
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
// RefPoolManager
//
//--------------------------------------------------------------------

RefPoolManager *RefPoolManager::s_singleInstance = nullptr;

RefPoolManager *RefPoolManager::getInstance()
{
  if (s_singleInstance == nullptr)
  {
    s_singleInstance = new (std::nothrow) RefPoolManager();
    // Add the first auto release pool
    new AutoreleasePool("cocos2d autorelease pool");
  }
  return s_singleInstance;
}

void RefPoolManager::destroyInstance()
{
  delete s_singleInstance;
  s_singleInstance = nullptr;
}

RefPoolManager::RefPoolManager()
{
  _releasePoolStack.reserve(10);
}

RefPoolManager::~RefPoolManager()
{
  //CCLOGINFO("deallocing RefPoolManager: %p", this);

  while (!_releasePoolStack.empty())
  {
    AutoreleasePool *pool = _releasePoolStack.back();

    delete pool;
  }
}

AutoreleasePool *RefPoolManager::getCurrentPool() const
{
  return _releasePoolStack.back();
}

bool RefPoolManager::isObjectInPools(Ref *obj) const
{
  for (const auto &pool : _releasePoolStack)
  {
    if (pool->contains(obj))
      return true;
  }
  return false;
}

void RefPoolManager::push(AutoreleasePool *pool)
{
  _releasePoolStack.push_back(pool);
}

void RefPoolManager::pop()
{
  assert(!_releasePoolStack.empty());
  _releasePoolStack.pop_back();
}

} // namespace util
} // namespace mycc