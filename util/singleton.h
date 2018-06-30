
#ifndef MYCC_UTIL_SINGLETON_H_
#define MYCC_UTIL_SINGLETON_H_

#include <atomic>
#include <mutex>
#include "macros_util.h"
#include "scoped_util.h"

namespace mycc
{
namespace util
{

// To be used as base template for class to make it singleton
//
// Example: define a singleton class
// class TestClass : public SingletonStaticBase<TestClass>
// {
//     friend class SingletonStaticBase<TestClass>;
// private:
//     TestClass() {}
//     ~TestClass() {}
// public:
//     int Test() const
//     {
//         return 1;
//     }
// };
//
// Example2: define a singleton class with alt access method
// private inherit make 'Instance' method unaccessable
// class TestClass2 : private SingletonStaticBase<TestClass2>
// {
//     friend class SingletonStaticBase<TestClass2>;
// private:
//     TestClass() {}
//     ~TestClass() {}
// public:
//     // using DefaultInstance to instead Instance
//     static TestClass2* DefaultInstance()
//     {
//         return Instance();
//     }
// };
//
template <typename T>
class SingletonStaticBase
{
private:
  // Used to check destructed of object.
  struct Holder
  {
    T value;
    bool is_alive;
    explicit Holder(Holder **holder) : value(), is_alive(true)
    {
      *holder = this;
    }

    template <typename A1>
    Holder(Holder **holder, const A1 &a1) : value(a1), is_alive(true)
    {
      *holder = this;
    }

    ~Holder()
    {
      is_alive = false;
    }
  };

protected:
  SingletonStaticBase() {}
  ~SingletonStaticBase() {}

public:
  // Default constructed instance.
  static T *Instance()
  {
    if (!s_holder)
      static Holder holder(&s_holder);
    if (!s_holder->is_alive)
      return NULL;
    return &s_holder->value;
  }

  // Construct singleton with parameter.
  template <typename A1>
  static T *Instance(const A1 &a1)
  {
    // Check s_holder before static construct holder to make sure not
    // create another singleton for different parameter.
    if (!s_holder)
      static Holder holder(&s_holder, a1);
    if (!s_holder->is_alive)
      return NULL;
    return &s_holder->value;
  }

  static bool IsAlive()
  {
    return s_holder && s_holder->is_alive;
  }

private:
  static Holder *s_holder;
  DISALLOW_COPY(SingletonStaticBase);
};

template <typename T>
typename SingletonStaticBase<T>::Holder *SingletonStaticBase<T>::s_holder;

// Make singleton for any existed class.
//
// Example:
// class TestClass3
// {
// };
//
// typedef SingletonStaticBaseT<TestClass3> SingletonTestClass3;
// TestClass3* instance = SingletonTestClass3::Instance();
//
template <typename T>
class SingletonStaticBaseT : public SingletonStaticBase<T>
{
};

// Usage:
// public MySingleton : public Singleton<MySingleton>{
// friend class Singleton<MySingleton>;
// public:
//     std::string my_method() { return something;}
// private:
//     MySingleton() {// do some initialize operations}
// };
//
// MySingleton& instance = MySingleton::GetInstance();

template <typename T>
class SingletonMutexBase
{
public:
  static T &GetInstance()
  {
    if (m_instance.get() == NULL)
    {
      std::lock_guard<std::mutex> guard(m_lock);
      if (m_instance.get() == NULL)
      {
        m_instance.reset(new T());
        assert(m_instance.get() != nullptr);
      }
    }
    return *m_instance.get();
  }

private:
  static ScopedPtr<T> m_instance;
  static std::mutex m_lock;
};

template <typename T>
ScopedPtr<T> SingletonMutexBase<T>::m_instance;

template <typename T>
std::mutex SingletonMutexBase<T>::m_lock;

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SINGLETON_H_