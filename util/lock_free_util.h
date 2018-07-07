
#ifndef MYCC_UTIL_LOCK_FREE_UTIL_H_
#define MYCC_UTIL_LOCK_FREE_UTIL_H_

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <atomic>
#include <mutex>
#include <type_traits>
#include <vector>
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

/**
 * Single Producer Single Consumer Lockless Q
 */

template <typename T>
class SpscQueue
{
public:
  SpscQueue()
      : _head(reinterpret_cast<node_t *>(new node_aligned_t)),
        _tail(_head)
  {
    _head->next = NULL;
  }

  ~SpscQueue()
  {
    T output;
    while (this->dequeue(output))
    {
    }
    delete _head;
  }

  void enqueue(const T &input)
  {
    node_t *node = reinterpret_cast<node_t *>(new node_aligned_t);
    node->data = input;
    node->next = NULL;

    std::atomic_thread_fence(std::memory_order_acq_rel);
    _head->next = node;
    _head = node;
  }

  bool dequeue(T &output)
  {
    std::atomic_thread_fence(std::memory_order_consume);
    if (!_tail->next)
    {
      return false;
    }

    output = _tail->next->data;
    std::atomic_thread_fence(std::memory_order_acq_rel);
    _back = _tail;
    _tail = _back->next;

    delete _back;
    return true;
  }

private:
  struct node_t
  {
    node_t *next;
    T data;
  };

  typedef typename std::aligned_storage<sizeof(node_t), std::alignment_of<node_t>::value>::type node_aligned_t;

  node_t *_head;
  char _cache_line[64];
  node_t *_tail;
  node_t *_back;

  DISALLOW_COPY_AND_ASSIGN(SpscQueue);
};

template <typename T>
class SpscBoundedQueue
{
public:
  SpscBoundedQueue(uint64_t size)
      : _size(size),
        _mask(size - 1),
        _buffer(reinterpret_cast<T *>(new aligned_t[_size + 1])), // need one extra element for a guard
        _head(0),
        _tail(0)
  {
    // make sure it's a power of 2
    assert((_size != 0) && ((_size & (~_size + 1)) == _size));
  }

  ~SpscBoundedQueue()
  {
    delete[] _buffer;
  }

  bool enqueue(T &input)
  {
    const uint64_t head = _head.load(std::memory_order_relaxed);

    if (((_tail.load(std::memory_order_acquire) - (head + 1)) & _mask) >= 1)
    {
      _buffer[head & _mask] = input;
      _head.store(head + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

  bool dequeue(T &output)
  {
    const uint64_t tail = _tail.load(std::memory_order_relaxed);

    if (((_head.load(std::memory_order_acquire) - tail) & _mask) >= 1)
    {
      output = _buffer[_tail & _mask];
      _tail.store(tail + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

private:
  typedef typename std::aligned_storage<sizeof(T), std::alignment_of<T>::value>::type aligned_t;
  typedef char cache_line_pad_t[64];

  cache_line_pad_t _pad0;
  const uint64_t _size;
  const uint64_t _mask;
  T *const _buffer;

  cache_line_pad_t _pad1;
  std::atomic<uint64_t> _head;

  cache_line_pad_t _pad2;
  std::atomic<uint64_t> _tail;

  DISALLOW_COPY_AND_ASSIGN(SpscBoundedQueue);
};

/**
 * Multiple Producer Single Consumer Lockless Q
 */

template <typename T>
class MpscQueue
{
public:
  struct buffer_node_t
  {
    T data;
    std::atomic<buffer_node_t *> next;
  };

  MpscQueue()
      : _head(reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t)),
        _tail(_head.load(std::memory_order_relaxed))
  {
    buffer_node_t *front = _head.load(std::memory_order_relaxed);
    front->next.store(NULL, std::memory_order_relaxed);
  }

  ~MpscQueue()
  {
    T output;
    while (this->dequeue(output))
    {
    }
    buffer_node_t *front = _head.load(std::memory_order_relaxed);
    delete front;
  }

  void enqueue(const T &input)
  {
    buffer_node_t *node = reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t);
    node->data = input;
    node->next.store(NULL, std::memory_order_relaxed);

    buffer_node_t *prev_head = _head.exchange(node, std::memory_order_acq_rel);
    prev_head->next.store(node, std::memory_order_release);
  }

  bool dequeue(T *output)
  {
    buffer_node_t *tail = _tail.load(std::memory_order_relaxed);
    buffer_node_t *next = tail->next.load(std::memory_order_acquire);

    if (next == NULL)
    {
      return false;
    }

    output = next->data;
    _tail.store(next, std::memory_order_release);
    delete tail;
    return true;
  }

  // you can only use pop_all if the queue is SPSC
  buffer_node_t *pop_all()
  {
    // nobody else can move the tail pointer.
    buffer_node_t *tptr = _tail.load(std::memory_order_relaxed);
    buffer_node_t *next =
        tptr->next.exchange(nullptr, std::memory_order_acquire);
    _head.exchange(tptr, std::memory_order_acquire);

    // there is a race condition here
    return next;
  }

private:
  typedef typename std::aligned_storage<
      sizeof(buffer_node_t), std::alignment_of<buffer_node_t>::value>::type
      buffer_node_aligned_t;

  std::atomic<buffer_node_t *> _head;
  std::atomic<buffer_node_t *> _tail;

  DISALLOW_COPY_AND_ASSIGN(MpscQueue);
};

template <typename T>
class MpmcBoundedQueue
{
public:
  MpmcBoundedQueue(uint64_t size)
      : _size(size),
        _mask(size - 1),
        _buffer(reinterpret_cast<node_t *>(new aligned_node_t[_size])),
        _head_seq(0),
        _tail_seq(0)
  {
    // make sure it's a power of 2
    assert((_size != 0) && ((_size & (~_size + 1)) == _size));

    // populate the sequence initial values
    for (uint64_t i = 0; i < _size; ++i)
    {
      _buffer[i].seq.store(i, std::memory_order_relaxed);
    }
  }

  ~MpmcBoundedQueue()
  {
    delete[] _buffer;
  }

  bool enqueue(const T &data)
  {
    // _head_seq only wraps at MAX(_head_seq) instead we use a mask to convert the sequence to an array index
    // this is why the ring buffer must be a size which is a power of 2. this also allows the sequence to double as a ticket/lock.
    uint64_t head_seq = _head_seq.load(std::memory_order_relaxed);

    for (;;)
    {
      node_t *node = &_buffer[head_seq & _mask];
      uint64_t node_seq = node->seq.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)node_seq - (intptr_t)head_seq;

      // if seq and head_seq are the same then it means this slot is empty
      if (dif == 0)
      {
        // claim our spot by moving head
        // if head isn't the same as we last checked then that means someone beat us to the punch
        // weak compare is faster, but can return spurious results
        // which in this instance is OK, because it's in the loop
        if (_head_seq.compare_exchange_weak(head_seq, head_seq + 1, std::memory_order_relaxed))
        {
          // set the data
          node->data = data;
          // increment the sequence so that the tail knows it's accessible
          node->seq.store(head_seq + 1, std::memory_order_release);
          return true;
        }
      }
      else if (dif < 0)
      {
        // if seq is less than head seq then it means this slot is full and therefore the buffer is full
        return false;
      }
      else
      {
        // under normal circumstances this branch should never be taken
        head_seq = _head_seq.load(std::memory_order_relaxed);
      }
    }

    // never taken
    return false;
  }

  bool dequeue(T &data)
  {
    uint64_t tail_seq = _tail_seq.load(std::memory_order_relaxed);

    for (;;)
    {
      node_t *node = &_buffer[tail_seq & _mask];
      uint64_t node_seq = node->seq.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)node_seq - (intptr_t)(tail_seq + 1);

      // if seq and head_seq are the same then it means this slot is empty
      if (dif == 0)
      {
        // claim our spot by moving head
        // if head isn't the same as we last checked then that means someone beat us to the punch
        // weak compare is faster, but can return spurious results
        // which in this instance is OK, because it's in the loop
        if (_tail_seq.compare_exchange_weak(tail_seq, tail_seq + 1, std::memory_order_relaxed))
        {
          // set the output
          data = node->data;
          // set the sequence to what the head sequence should be next time around
          node->seq.store(tail_seq + _mask + 1, std::memory_order_release);
          return true;
        }
      }
      else if (dif < 0)
      {
        // if seq is less than head seq then it means this slot is full and therefore the buffer is full
        return false;
      }
      else
      {
        // under normal circumstances this branch should never be taken
        tail_seq = _tail_seq.load(std::memory_order_relaxed);
      }
    }

    // never taken
    return false;
  }

private:
  struct node_t
  {
    T data;
    std::atomic<uint64_t> seq;
  };

  typedef typename std::aligned_storage<sizeof(node_t), std::alignment_of<node_t>::value>::type aligned_node_t;
  typedef char cache_line_pad_t[64]; // it's either 32 or 64 so 64 is good enough

  cache_line_pad_t _pad0;
  const uint64_t _size;
  const uint64_t _mask;
  node_t *const _buffer;
  cache_line_pad_t _pad1;
  std::atomic<uint64_t> _head_seq;
  cache_line_pad_t _pad2;
  std::atomic<uint64_t> _tail_seq;
  cache_line_pad_t _pad3;

  DISALLOW_COPY_AND_ASSIGN(MpmcBoundedQueue);
};

///////////////// WorkStealingQueue //////////////////////

template <typename T>
class WorkStealingQueue
{
public:
  WorkStealingQueue()
      : _bottom(1), _capacity(0), _buffer(NULL), _top(1)
  {
  }

  ~WorkStealingQueue()
  {
    delete[] _buffer;
    _buffer = NULL;
  }

  int32_t init(uint64_t capacity)
  {
    if (_capacity != 0)
    {
      //LOG(ERROR) << "Already initialized";
      return -1;
    }
    if (capacity == 0)
    {
      //LOG(ERROR) << "Invalid capacity=" << capacity;
      return -1;
    }
    if (capacity & (capacity - 1))
    {
      //LOG(ERROR) << "Invalid capacity=" << capacity
      //           << " which must be power of 2";
      return -1;
    }
    _buffer = new (std::nothrow) T[capacity];
    if (NULL == _buffer)
    {
      return -1;
    }
    _capacity = capacity;
    return 0;
  }

  // Push an item into the queue.
  // Returns true on pushed.
  // May run in parallel with steal().
  // Never run in parallel with pop() or another push().
  bool push(const T &x)
  {
    const uint64_t b = _bottom.load(std::memory_order_relaxed);
    const uint64_t t = _top.load(std::memory_order_acquire);
    if (b >= t + _capacity)
    { // Full queue.
      return false;
    }
    _buffer[b & (_capacity - 1)] = x;
    _bottom.store(b + 1, std::memory_order_release);
    return true;
  }

  // Pop an item from the queue.
  // Returns true on popped and the item is written to `val'.
  // May run in parallel with steal().
  // Never run in parallel with push() or another pop().
  bool pop(T *val)
  {
    const uint64_t b = _bottom.load(std::memory_order_relaxed);
    uint64_t t = _top.load(std::memory_order_relaxed);
    if (t >= b)
    {
      // fast check since we call pop() in each sched.
      // Stale _top which is smaller should not enter this branch.
      return false;
    }
    const uint64_t newb = b - 1;
    _bottom.store(newb, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    t = _top.load(std::memory_order_relaxed);
    if (t > newb)
    {
      _bottom.store(b, std::memory_order_relaxed);
      return false;
    }
    *val = _buffer[newb & (_capacity - 1)];
    if (t != newb)
    {
      return true;
    }
    // Single last element, compete with steal()
    const bool popped = _top.compare_exchange_strong(
        t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed);
    _bottom.store(b, std::memory_order_relaxed);
    return popped;
  }

  // Steal one item from the queue.
  // Returns true on stolen.
  // May run in parallel with push() pop() or another steal().
  bool steal(T *val)
  {
    uint64_t t = _top.load(std::memory_order_acquire);
    uint64_t b = _bottom.load(std::memory_order_acquire);
    if (t >= b)
    {
      // Permit false negative for performance considerations.
      return false;
    }
    do
    {
      std::atomic_thread_fence(std::memory_order_seq_cst);
      b = _bottom.load(std::memory_order_acquire);
      if (t >= b)
      {
        return false;
      }
      *val = _buffer[t & (_capacity - 1)];
    } while (!_top.compare_exchange_strong(t, t + 1,
                                           std::memory_order_seq_cst,
                                           std::memory_order_relaxed));
    return true;
  }

  uint64_t volatile_size() const
  {
    const uint64_t b = _bottom.load(std::memory_order_relaxed);
    const uint64_t t = _top.load(std::memory_order_relaxed);
    return (b <= t ? 0 : (b - t));
  }

  uint64_t capacity() const { return _capacity; }

private:
  std::atomic<uint64_t> _bottom;
  uint64_t _capacity;
  T *_buffer;
  std::atomic<uint64_t> CACHE_LINE_ALIGNMENT _top;

  // Copying a concurrent structure makes no sense.
  DISALLOW_COPY_AND_ASSIGN(WorkStealingQueue);
};

// This data structure makes Read() almost lock-free by making Modify()
// *much* slower. It's very suitable for implementing LoadBalancers which
// have a lot of concurrent read-only ops from many threads and occasional
// modifications of data. As a side effect, this data structure can store
// a thread-local data for user.
//
// Read(): begin with a thread-local mutex locked then read the foreground
// instance which will not be changed before the mutex is unlocked. Since the
// mutex is only locked by Modify() with an empty critical section, the
// function is almost lock-free.
//
// Modify(): Modify background instance which is not used by any Read(), flip
// foreground and background, lock thread-local mutexes one by one to make
// sure all existing Read() finish and later Read() see new foreground,
// then modify background(foreground before flip) again.

template <typename T, typename TLS>
class DoublyBufferedData
{
  class Wrapper;

public:
  class ScopedPtr
  {
    friend class DoublyBufferedData;

  public:
    ScopedPtr() : _data(NULL), _w(NULL) {}
    ~ScopedPtr()
    {
      if (_w)
      {
        _w->EndRead();
      }
    }
    const T *get() const { return _data; }
    const T &operator*() const { return *_data; }
    const T *operator->() const { return _data; }
    TLS &tls() { return _w->user_tls(); }

  private:
    DISALLOW_COPY_AND_ASSIGN(ScopedPtr);
    const T *_data;
    Wrapper *_w;
  };

  DoublyBufferedData();
  ~DoublyBufferedData();

  // Put foreground instance into ptr. The instance will not be changed until
  // ptr is destructed.
  // This function is not blocked by Read() and Modify() in other threads.
  // Returns 0 on success, -1 otherwise.
  int Read(ScopedPtr *ptr);

  // Modify background and foreground instances. fn(T&, ...) will be called
  // twice. Modify() from different threads are exclusive from each other.
  // NOTE: Call same series of fn to different equivalent instances should
  // result in equivalent instances, otherwise foreground and background
  // instance will be inconsistent.
  template <typename Fn>
  uint64_t Modify(Fn &fn);
  template <typename Fn, typename Arg1>
  uint64_t Modify(Fn &fn, const Arg1 &);
  template <typename Fn, typename Arg1, typename Arg2>
  uint64_t Modify(Fn &fn, const Arg1 &, const Arg2 &);

  // fn(T& background, const T& foreground, ...) will be called to background
  // and foreground instances respectively.
  template <typename Fn>
  uint64_t ModifyWithForeground(Fn &fn);
  template <typename Fn, typename Arg1>
  uint64_t ModifyWithForeground(Fn &fn, const Arg1 &);
  template <typename Fn, typename Arg1, typename Arg2>
  uint64_t ModifyWithForeground(Fn &fn, const Arg1 &, const Arg2 &);

private:
  template <typename Fn>
  struct WithFG0
  {
    WithFG0(Fn &fn, T *data) : _fn(fn), _data(data) {}
    uint64_t operator()(T &bg)
    {
      return _fn(bg, (const T &)_data[&bg == _data]);
    }

  private:
    Fn &_fn;
    T *_data;
  };

  template <typename Fn, typename Arg1>
  struct WithFG1
  {
    WithFG1(Fn &fn, T *data, const Arg1 &arg1)
        : _fn(fn), _data(data), _arg1(arg1) {}
    uint64_t operator()(T &bg)
    {
      return _fn(bg, (const T &)_data[&bg == _data], _arg1);
    }

  private:
    Fn &_fn;
    T *_data;
    const Arg1 &_arg1;
  };

  template <typename Fn, typename Arg1, typename Arg2>
  struct WithFG2
  {
    WithFG2(Fn &fn, T *data, const Arg1 &arg1, const Arg2 &arg2)
        : _fn(fn), _data(data), _arg1(arg1), _arg2(arg2) {}
    uint64_t operator()(T &bg)
    {
      return _fn(bg, (const T &)_data[&bg == _data], _arg1, _arg2);
    }

  private:
    Fn &_fn;
    T *_data;
    const Arg1 &_arg1;
    const Arg2 &_arg2;
  };

  template <typename Fn, typename Arg1>
  struct Closure1
  {
    Closure1(Fn &fn, const Arg1 &arg1) : _fn(fn), _arg1(arg1) {}
    uint64_t operator()(T &bg) { return _fn(bg, _arg1); }

  private:
    Fn &_fn;
    const Arg1 &_arg1;
  };

  template <typename Fn, typename Arg1, typename Arg2>
  struct Closure2
  {
    Closure2(Fn &fn, const Arg1 &arg1, const Arg2 &arg2)
        : _fn(fn), _arg1(arg1), _arg2(arg2) {}
    uint64_t operator()(T &bg) { return _fn(bg, _arg1, _arg2); }

  private:
    Fn &_fn;
    const Arg1 &_arg1;
    const Arg2 &_arg2;
  };

  const T *UnsafeRead() const { return _data + _index; }
  Wrapper *AddWrapper();
  void RemoveWrapper(Wrapper *);

  // Foreground and background void.
  T _data[2];

  // Index of foreground instance.
  short _index;

  // Key to access thread-local wrappers.
  bool _created_key;
  pthread_key_t _wrapper_key;

  // All thread-local instances.
  std::vector<Wrapper *> _wrappers;

  // Sequence access to _wrappers.
  Mutex _wrappers_mutex;

  // Sequence modifications.
  Mutex _modify_mutex;
};

static const pthread_key_t INVALID_PTHREAD_KEY = (pthread_key_t)-1;

template <typename T, typename TLS>
class DoublyBufferedDataWrapperBase
{
public:
  TLS &user_tls() { return _user_tls; }

protected:
  TLS _user_tls;
};

// template <typename T>
// class DoublyBufferedDataWrapperBase<T, Void>
// {
// };

template <typename T, typename TLS>
class DoublyBufferedData<T, TLS>::Wrapper
    : public DoublyBufferedDataWrapperBase<T, TLS>
{
  friend class DoublyBufferedData;

public:
  explicit Wrapper(DoublyBufferedData *c) : _control(c)
  {
  }

  ~Wrapper()
  {
    if (_control != NULL)
    {
      _control->RemoveWrapper(this);
    }
  }

  // _mutex will be locked by the calling pthread and DoublyBufferedData.
  // Most of the time, no modifications are done, so the mutex is
  // uncontended and fast.
  inline void BeginRead()
  {
    _mutex.lock();
  }

  inline void EndRead()
  {
    _mutex.unlock();
  }

  inline void WaitReadDone()
  {
    MutexLock lock(&_mutex);
  }

private:
  DoublyBufferedData *_control;
  Mutex _mutex;
};

// Called when thread initializes thread-local wrapper.
template <typename T, typename TLS>
typename DoublyBufferedData<T, TLS>::Wrapper *
DoublyBufferedData<T, TLS>::AddWrapper()
{
  Wrapper *w = new (std::nothrow) Wrapper(this);
  if (NULL == w)
  {
    return NULL;
  }
  try
  {
    MutexLock wlock(&_wrappers_mutex);
    _wrappers.push_back(w);
  }
  catch (std::exception &e)
  {
    return NULL;
  }
  return w;
}

// Called when thread quits.
template <typename T, typename TLS>
void DoublyBufferedData<T, TLS>::RemoveWrapper(
    typename DoublyBufferedData<T, TLS>::Wrapper *w)
{
  if (NULL == w)
  {
    return;
  }
  MutexLock wlock(&_wrappers_mutex);
  for (uint64_t i = 0; i < _wrappers.size(); ++i)
  {
    if (_wrappers[i] == w)
    {
      _wrappers[i] = _wrappers.back();
      _wrappers.pop_back();
      return;
    }
  }
}

template <typename T, typename TLS>
DoublyBufferedData<T, TLS>::DoublyBufferedData()
    : _index(0), _created_key(false), _wrapper_key(0)
{
  _wrappers.reserve(64);
  const int rc = pthread_key_create(&_wrapper_key,
                                    delete_object<Wrapper>);
  if (rc != 0)
  {
    //LOG(FATAL) << "Fail to pthread_key_create: " << strerror(rc);
    assert(false);
  }
  else
  {
    _created_key = true;
  }
  // Initialize _data for some POD types. This is essential for pointer
  // types because they should be Read() as NULL before any Modify().
  if (std::is_integral<T>::value || std::is_floating_point<T>::value ||
      std::is_pointer<T>::value || std::is_member_function_pointer<T>::value)
  {
    _data[0] = T();
    _data[1] = T();
  }
}

template <typename T, typename TLS>
DoublyBufferedData<T, TLS>::~DoublyBufferedData()
{
  // User is responsible for synchronizations between Read()/Modify() and
  // this function.
  if (_created_key)
  {
    pthread_key_delete(_wrapper_key);
  }

  {
    MutexLock wlock(&_wrappers_mutex);
    for (uint64_t i = 0; i < _wrappers.size(); ++i)
    {
      _wrappers[i]->_control = NULL; // hack: disable removal.
      delete _wrappers[i];
    }
    _wrappers.clear();
  }
}

template <typename T, typename TLS>
int DoublyBufferedData<T, TLS>::Read(
    typename DoublyBufferedData<T, TLS>::ScopedPtr *ptr)
{
  if (UNLIKELY(!_created_key))
  {
    return -1;
  }
  Wrapper *w = static_cast<Wrapper *>(pthread_getspecific(_wrapper_key));
  if (LIKELY(w != NULL))
  {
    w->BeginRead();
    ptr->_data = UnsafeRead();
    ptr->_w = w;
    return 0;
  }
  w = AddWrapper();
  if (LIKELY(w != NULL))
  {
    const int rc = pthread_setspecific(_wrapper_key, w);
    if (rc == 0)
    {
      w->BeginRead();
      ptr->_data = UnsafeRead();
      ptr->_w = w;
      return 0;
    }
  }
  return -1;
}

template <typename T, typename TLS>
template <typename Fn>
uint64_t DoublyBufferedData<T, TLS>::Modify(Fn &fn)
{
  // _modify_mutex sequences modifications. Using a separate mutex rather
  // than _wrappers_mutex is to avoid blocking threads calling
  // AddWrapper() or RemoveWrapper() too long. Most of the time, modifications
  // are done by one thread, contention should be negligible.
  MutexLock mlock(&_modify_mutex);
  // background instance is not accessed by other threads, being safe to
  // modify.
  const uint64_t ret = fn(_data[!_index]);
  if (!ret)
  {
    return 0;
  }

  // Publish, flip background and foreground.
  _index = !_index;

  // Wait until all threads finishes current reading. When they begin next
  // read, they should see updated _index.
  {
    MutexLock wlock(&_wrappers_mutex);
    for (uint64_t i = 0; i < _wrappers.size(); ++i)
    {
      _wrappers[i]->WaitReadDone();
    }
  }

  const uint64_t ret2 = fn(_data[!_index]);
  //CHECK_EQ(ret2, ret) << "index=" << _index;
  return ret2;
}

template <typename T, typename TLS>
template <typename Fn, typename Arg1>
uint64_t DoublyBufferedData<T, TLS>::Modify(Fn &fn, const Arg1 &arg1)
{
  Closure1<Fn, Arg1> c(fn, arg1);
  return Modify(c);
}

template <typename T, typename TLS>
template <typename Fn, typename Arg1, typename Arg2>
uint64_t DoublyBufferedData<T, TLS>::Modify(
    Fn &fn, const Arg1 &arg1, const Arg2 &arg2)
{
  Closure2<Fn, Arg1, Arg2> c(fn, arg1, arg2);
  return Modify(c);
}

template <typename T, typename TLS>
template <typename Fn>
uint64_t DoublyBufferedData<T, TLS>::ModifyWithForeground(Fn &fn)
{
  WithFG0<Fn> c(fn, _data);
  return Modify(c);
}

template <typename T, typename TLS>
template <typename Fn, typename Arg1>
uint64_t DoublyBufferedData<T, TLS>::ModifyWithForeground(Fn &fn, const Arg1 &arg1)
{
  WithFG1<Fn, Arg1> c(fn, _data, arg1);
  return Modify(c);
}

template <typename T, typename TLS>
template <typename Fn, typename Arg1, typename Arg2>
uint64_t DoublyBufferedData<T, TLS>::ModifyWithForeground(
    Fn &fn, const Arg1 &arg1, const Arg2 &arg2)
{
  WithFG2<Fn, Arg1, Arg2> c(fn, _data, arg1, arg2);
  return Modify(c);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOCK_FREE_UTIL_H_