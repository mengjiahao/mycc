
#ifndef MYCC_UTIL_LOCK_FREE_UTIL_H_
#define MYCC_UTIL_LOCK_FREE_UTIL_H_

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <atomic>
#include <memory>
#include <mutex>
#include <type_traits>
#include <utility>
#include <vector>
#include "atomic_util.h"
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// These files are about implementation of seqlock in userspace.　
// About seqlock, you can see https://en.wikipedia.org/wiki/Seqlock.
// About memory barrier, there is a paper that is very much to recommend,
// its name is Memory Barriers: a Hardware View for Software Hackers and
// you can get it from http://www.rdrop.com/users/paulmck/scalability/paper/whymb.2010.07.23a.pdf.
// Using write lock, you can put your critical code between write_seqlock(&seqlock) and write_sequnlock(&seqlock).
// Using read lock, you can do as follow:
// uint32_t int start;
// do {
//   start = read_seqbegin(&seqlock);
// } while(read_seqretry(&seqlock, start));

/*
 * Before and after critical code, sequence will be add one.
 * If sequence is odd, it says that wirte critical code isn't executed
 * completely. So if there is a read code in this case, the data getted
 * by read is inconsistent.
 */

struct SeqLock
{
public:
  static inline void lock_init(SeqLock *seqlock)
  {
    int err;
    if ((err = pthread_spin_init(&seqlock->lock, PTHREAD_PROCESS_SHARED)) != 0)
    {
      abort();
    }
    seqlock->sequence = 0;
  }

  static inline void write_seqlock(SeqLock *seqlock)
  {
    pthread_spin_lock(&seqlock->lock);
    ++seqlock->sequence;
    SMP_WMB();
  }

  static inline void write_sequnlock(SeqLock *seqlock)
  {
    SMP_WMB();
    ++seqlock->sequence;
    pthread_spin_unlock(&seqlock->lock);
  }

  static inline uint32_t read_seqbegin(SeqLock *seqlock)
  {
    uint32_t ret;

  repeat:
    ret = ACCESS_ONCE(seqlock->sequence);
    if (UNLIKELY(ret & 1))
    {
      CPU_RELAX();
      goto repeat;
    }
    SMP_RMB();
    return ret;
  }

  static inline uint32_t read_seqretry(SeqLock *seqlock, uint32_t start)
  {
    SMP_RMB();
    return UNLIKELY(seqlock->sequence != start);
  }

private:
  uint32_t sequence;
  pthread_spinlock_t lock;
};

// A seqlock can be used as an alternative to a readers-writer lock.
// It will never block the writer and doesn't require any memory bus locks.
// Implementing the seqlock in portable C++11 is quite tricky.
// The basic seqlock implementation unconditionally loads the sequence number,
// then unconditionally loads the protected data and finally unconditionally
// loads the sequence number again. Since loading the protected data is done
// unconditionally on the sequence number the compiler is free to move these loads
// before or after the loads from the sequence number.
// We see that the compiler did indeed reorder the load of the protected data
// outside the critical section and the data is no longer protected from torn reads.
// For x86 it's enough to insert a compiler barrier using
// std::atomic_signal_fence(std::memory_order_acq_rel).
// This will only work on the x86 memory model.
// On ARM memory model you need to inserts a dmb memory barrier instruction,
// which is not possible in C++11.
template <typename T>
class SeqFreeLock
{
public:
  static_assert(std::is_nothrow_copy_assignable<T>::value,
                "T must satisfy is_nothrow_copy_assignable");
  static_assert(std::is_trivially_copy_assignable<T>::value,
                "T must satisfy is_trivially_copy_assignable");

  SeqFreeLock() : seq_(0) {}

  T load() const noexcept __attribute__((noinline))
  {
    T copy;
    uint64_t seq0, seq1;
    do
    {
      seq0 = seq_.load(std::memory_order_acquire);
      std::atomic_signal_fence(std::memory_order_acq_rel);
      copy = value_;
      std::atomic_signal_fence(std::memory_order_acq_rel);
      seq1 = seq_.load(std::memory_order_acquire);
    } while (seq0 != seq1 || seq0 & 1);
    return copy;
  }

  void store(const T &desired) noexcept __attribute__((noinline))
  {
    uint64_t seq0 = seq_.load(std::memory_order_relaxed);
    seq_.store(seq0 + 1, std::memory_order_release);
    std::atomic_signal_fence(std::memory_order_acq_rel);
    value_ = desired;
    std::atomic_signal_fence(std::memory_order_acq_rel);
    seq_.store(seq0 + 2, std::memory_order_release);
  }

private:
  static const uint64_t kFalseSharingRange = 128;

  // Align to prevent false sharing with adjecent data
  alignas(kFalseSharingRange) T value_;
  std::atomic<uint64_t> seq_;
  // Padding to prevent false sharing with adjecent data
  char padding_[kFalseSharingRange -
                ((sizeof(value_) + sizeof(seq_)) % kFalseSharingRange)];
  static_assert(
      ((sizeof(value_) + sizeof(seq_) + sizeof(padding_)) %
       kFalseSharingRange) == 0,
      "sizeof(SeqFreeLock<T>) should be a multiple of kFalseSharingRange");
};

// Hazard Pointers.
// Each reader thread owns a single-writer/multi-reader shared pointer called hazard pointer.
// When a reader thread assigns the address of a map to its hazard pointer,
// it is basically announcing to other threads (writers),
// "I am reading this map. You can replace it if you want,
// but don’t change its contents and certainly keep your deleteing hands off it."

struct HazardPtrEasy
{
public:
  typedef struct free_t
  {
    void *p;
    struct free_t *next;
  } free_t;

  static const int64_t HAZ_MAX_THREAD = 1024;
  static const int64_t HAZ_MAX_COUNT_PER_THREAD = 4;
  static const int64_t HAZ_MAX_COUNT = HAZ_MAX_THREAD * HAZ_MAX_COUNT_PER_THREAD;
  static int64_t _tidSeed;
  static void *_haz_array[HAZ_MAX_COUNT];
  /* a simple link list to save pending free pointers */
  static free_t _free_list[HAZ_MAX_THREAD];

  static void **haz_get(int64_t idx);
  static void haz_defer_free(void *p);
  static void haz_gc();
  static void haz_set_ptr(void **haz, void *p)
  {
    (*haz) = p;
  }

private:
  static int64_t seq_thread_id();
  static bool haz_confict(int64_t self, void *p);
};

//////////////////////////// URCU //////////////////////////////
// User-Level Implementations of Read-Copy Update
// [2011] M.Desnoyers, P.McKenney, A.Stern, M.Dagenias, J.Walpole "User-Level Implementations of Read-Copy Update
// http://www.dorsal.polymtl.ca/sites/www.dorsal.polymtl.ca/files/publications/desnoyers-ieee-urcu-submitted.pdf

namespace urcu_easy
{

void rcu_read_lock();
void rcu_read_unlock();
void synchronize_rcu();

} // namespace urcu_easy

/*
 * ProducerConsumerFreeQueue is a one producer and one consumer queue
 * without locks.
 */
template <class T>
struct ProducerConsumerFreeQueue
{
  typedef T value_type;

  ProducerConsumerFreeQueue(const ProducerConsumerFreeQueue &) = delete;
  ProducerConsumerFreeQueue &operator=(const ProducerConsumerFreeQueue &) = delete;

  // size must be >= 2.
  //
  // Also, note that the number of usable slots in the queue at any
  // given time is actually (size-1), so if you start with an empty queue,
  // isFull() will return true after size-1 insertions.
  explicit ProducerConsumerFreeQueue(uint32_t size)
      : size_(size),
        records_(static_cast<T *>(::malloc(sizeof(T) * size))),
        readIndex_(0), writeIndex_(0)
  {
    assert(size >= 2);
    if (!records_)
    {
      abort();
    }
  }

  ~ProducerConsumerFreeQueue()
  {
    // We need to destruct anything that may still exist in our queue.
    // (No real synchronization needed at destructor time: only one
    // thread can be doing this.)
    if (!std::is_trivially_destructible<T>::value)
    {
      uint64_t readIndex = readIndex_;
      uint64_t endIndex = writeIndex_;
      while (readIndex != endIndex)
      {
        records_[readIndex].~T();
        if (++readIndex == size_)
        {
          readIndex = 0;
        }
      }
    }

    ::free(records_);
  }

  template <class... Args>
  bool write(Args &&... recordArgs)
  {
    auto const currentWrite = writeIndex_.load(std::memory_order_relaxed);
    auto nextRecord = currentWrite + 1;
    if (nextRecord == size_)
    {
      nextRecord = 0;
    }
    if (nextRecord != readIndex_.load(std::memory_order_acquire))
    {
      new (&records_[currentWrite]) T(std::forward<Args>(recordArgs)...);
      writeIndex_.store(nextRecord, std::memory_order_release);
      return true;
    }

    // queue is full
    return false;
  }

  // move (or copy) the value at the front of the queue to given variable
  bool read(T &record)
  {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    if (currentRead == writeIndex_.load(std::memory_order_acquire))
    {
      // queue is empty
      return false;
    }

    auto nextRecord = currentRead + 1;
    if (nextRecord == size_)
    {
      nextRecord = 0;
    }
    record = std::move(records_[currentRead]);
    records_[currentRead].~T();
    readIndex_.store(nextRecord, std::memory_order_release);
    return true;
  }

  // pointer to the value at the front of the queue (for use in-place) or
  // nullptr if empty.
  T *frontPtr()
  {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    if (currentRead == writeIndex_.load(std::memory_order_acquire))
    {
      // queue is empty
      return nullptr;
    }
    return &records_[currentRead];
  }

  // queue must not be empty
  void popFront()
  {
    auto const currentRead = readIndex_.load(std::memory_order_relaxed);
    assert(currentRead != writeIndex_.load(std::memory_order_acquire));

    auto nextRecord = currentRead + 1;
    if (nextRecord == size_)
    {
      nextRecord = 0;
    }
    records_[currentRead].~T();
    readIndex_.store(nextRecord, std::memory_order_release);
  }

  bool isEmpty() const
  {
    return readIndex_.load(std::memory_order_acquire) ==
           writeIndex_.load(std::memory_order_acquire);
  }

  bool isFull() const
  {
    auto nextRecord = writeIndex_.load(std::memory_order_acquire) + 1;
    if (nextRecord == size_)
    {
      nextRecord = 0;
    }
    if (nextRecord != readIndex_.load(std::memory_order_acquire))
    {
      return false;
    }
    // queue is full
    return true;
  }

  // * If called by consumer, then true size may be more (because producer may
  //   be adding items concurrently).
  // * If called by producer, then true size may be less (because consumer may
  //   be removing items concurrently).
  // * It is undefined to call this from any other thread.
  uint64_t sizeGuess() const
  {
    int ret = writeIndex_.load(std::memory_order_acquire) -
              readIndex_.load(std::memory_order_acquire);
    if (ret < 0)
    {
      ret += size_;
    }
    return ret;
  }

  // maximum number of items in the queue.
  uint64_t capacity() const
  {
    return size_ - 1;
  }

private:
  using AtomicIndex = std::atomic<uint32_t>;

  char pad0_[port::k_hardware_destructive_interference_size];
  const uint32_t size_;
  T *const records_;

  alignas(port::k_hardware_destructive_interference_size) AtomicIndex readIndex_; // c++11
  alignas(port::k_hardware_destructive_interference_size) AtomicIndex writeIndex_;

  char pad1_[port::k_hardware_destructive_interference_size - sizeof(AtomicIndex)];
};

template <class T>
class RingQueue
{
public:
  RingQueue(int32_t size)
  {
    head = 0;
    tail = 0;
    bufferSize = size;
    buffer = new T[size];
  }

  ~RingQueue()
  {
    delete buffer;
  }

  int32_t can_consume_size()
  {
    return (tail + bufferSize - head) % bufferSize;
  }

  bool empty()
  {
    return head == tail;
  }

  bool full()
  {
    return (tail + 1) % bufferSize == head;
  }

  bool put(T &v)
  {
    if (UNLIKELY(full()))
    {
      return false;
    }
    buffer[tail] = v;
    SMP_MB();
    tail = (tail + 1) % bufferSize;
    return true;
  }

  void force_put(T &v)
  {
    while (!put(v))
    {
      CPU_RELAX();
    }
  }

  bool get(T &v)
  {
    if (UNLIKELY(empty()))
    {
      return false;
    }
    v = buffer[head];
    SMP_MB();
    head = (head + 1) % bufferSize;
    return true;
  }

private:
  T *buffer;
  int32_t bufferSize __attribute__((__aligned__(64)));
  int32_t head __attribute__((__aligned__(64)));
  int32_t tail __attribute__((__aligned__(64)));
};

// A simple CAS-based lock-free free list. Not the fastest thing in the world under heavy contention,
// but simple and correct (assuming nodes are never freed until after the free list is destroyed),
// and fairly speedy under low contention.
// http://moodycamel.com/blog/2014/solving-the-aba-problem-for-lock-free-free-lists

template <typename N>
struct LockFreeListNode
{
  LockFreeListNode() : freeListRefs(0), freeListNext(nullptr) {}

  std::atomic<uint32_t> freeListRefs;
  std::atomic<N *> freeListNext;
};

// N must inherit LockFreeListNode or have the same fields (and initialization)
template <typename N>
struct LockFreeList
{
  LockFreeList() : freeListHead(nullptr) {}

  inline void add(N *node)
  {
    // We know that the should-be-on-freelist bit is 0 at this point, so it's safe to
    // set it using a fetch_add
    if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST, std::memory_order_release) == 0)
    {
      // Oh look! We were the last ones referencing this node, and we know
      // we want to add it to the free list, so let's do it!
      add_knowing_refcount_is_zero(node);
    }
  }

  inline N *try_get()
  {
    auto head = freeListHead.load(std::memory_order_acquire);
    while (head != nullptr)
    {
      auto prevHead = head;
      auto refs = head->freeListRefs.load(std::memory_order_relaxed);
      if ((refs & REFS_MASK) == 0 || !head->freeListRefs.compare_exchange_strong(refs, refs + 1,
                                                                                 std::memory_order_acquire, std::memory_order_relaxed))
      {
        head = freeListHead.load(std::memory_order_acquire);
        continue;
      }

      // Good, reference count has been incremented (it wasn't at zero), which means
      // we can read the next and not worry about it changing between now and the time
      // we do the CAS
      auto next = head->freeListNext.load(std::memory_order_relaxed);
      if (freeListHead.compare_exchange_strong(head, next,
                                               std::memory_order_acquire, std::memory_order_relaxed))
      {
        // Yay, got the node. This means it was on the list, which means
        // shouldBeOnFreeList must be false no matter the refcount (because
        // nobody else knows it's been taken off yet, it can't have been put back on).
        assert((head->freeListRefs.load(std::memory_order_relaxed) &
                SHOULD_BE_ON_FREELIST) == 0);

        // Decrease refcount twice, once for our ref, and once for the list's ref
        head->freeListRefs.fetch_add(-2, std::memory_order_relaxed);

        return head;
      }

      // OK, the head must have changed on us, but we still need to decrease the refcount we
      // increased
      refs = prevHead->freeListRefs.fetch_add(-1, std::memory_order_acq_rel);
      if (refs == SHOULD_BE_ON_FREELIST + 1)
      {
        add_knowing_refcount_is_zero(prevHead);
      }
    }

    return nullptr;
  }

  // Useful for traversing the list when there's no contention (e.g. to destroy remaining nodes)
  N *head_unsafe() const { return freeListHead.load(std::memory_order_relaxed); }

private:
  inline void add_knowing_refcount_is_zero(N *node)
  {
    // Since the refcount is zero, and nobody can increase it once it's zero (except us, and we
    // run only one copy of this method per node at a time, i.e. the single thread case), then we
    // know we can safely change the next pointer of the node; however, once the refcount is back
    // above zero, then other threads could increase it (happens under heavy contention, when the
    // refcount goes to zero in between a load and a refcount increment of a node in try_get, then
    // back up to something non-zero, then the refcount increment is done by the other thread) --
    // so, if the CAS to add the node to the actual list fails, decrease the refcount and leave
    // the add operation to the next thread who puts the refcount back at zero (which could be us,
    // hence the loop).
    auto head = freeListHead.load(std::memory_order_relaxed);
    while (true)
    {
      node->freeListNext.store(head, std::memory_order_relaxed);
      node->freeListRefs.store(1, std::memory_order_release);
      if (!freeListHead.compare_exchange_strong(head, node,
                                                std::memory_order_release, std::memory_order_relaxed))
      {
        // Hmm, the add failed, but we can only try again when the refcount goes back to zero
        if (node->freeListRefs.fetch_add(SHOULD_BE_ON_FREELIST - 1,
                                         std::memory_order_release) == 1)
        {
          continue;
        }
      }
      return;
    }
  }

private:
  static const uint32_t REFS_MASK = 0x7FFFFFFF;
  static const uint32_t SHOULD_BE_ON_FREELIST = 0x80000000;

  // Implemented like a stack, but where node order doesn't matter (nodes are
  // inserted out of order under contention)
  std::atomic<N *> freeListHead;
};

template <typename T>
class SPSCBoundedQueue
{

  typedef typename std::aligned_storage<sizeof(T), std::alignment_of<T>::value>::type aligned_t;
  typedef char cache_line_pad_t[64];

  cache_line_pad_t pad0;
  const uint64_t size;
  const uint64_t mask;
  T *const buffer;
  cache_line_pad_t pad1;
  std::atomic<uint64_t> head{0};
  cache_line_pad_t pad2;
  std::atomic<uint64_t> tail{0};

  SPSCBoundedQueue(const SPSCBoundedQueue &) {}
  void operator=(const SPSCBoundedQueue &) {}

public:
  SPSCBoundedQueue(uint64_t size = 1024) : size(size), mask(size - 1), buffer(reinterpret_cast<T *>(new aligned_t[size + 1]))
  {
    assert((size != 0) && ((size & (~size + 1)) == size));
  }

  ~SPSCBoundedQueue()
  {
    delete[] buffer;
  }

  bool produce(T &input)
  {
    const uint64_t h = head.load(std::memory_order_relaxed);

    if (((tail.load(std::memory_order_acquire) - (h + 1)) & mask) >= 1)
    {
      buffer[h & mask] = input;
      head.store(h + 1, std::memory_order_release);
      return true;
    }
    return false;
  }

  bool consume(T &output)
  {
    const uint64_t t = tail.load(std::memory_order_relaxed);

    if (((head.load(std::memory_order_acquire) - t) & mask) >= 1)
    {
      output = buffer[tail & mask];
      tail.store(t + 1, std::memory_order_release);
      return true;
    }
    return false;
  }
};

template <typename T>
class MPMCBoundedQueue
{

  struct node_t
  {
    T data;
    std::atomic<uint64_t> next;
  };
  typedef typename std::aligned_storage<sizeof(node_t), std::alignment_of<node_t>::value>::type aligned_node_t;
  typedef char cache_line_pad_t[64];

  cache_line_pad_t pad0;
  const uint64_t size;
  const uint64_t mask;
  node_t *const buffer;
  cache_line_pad_t pad1;
  std::atomic<uint64_t> head{0};
  cache_line_pad_t pad2;
  std::atomic<uint64_t> tail{0};
  cache_line_pad_t pad3;

  MPMCBoundedQueue(const MPMCBoundedQueue &) {}
  void operator=(const MPMCBoundedQueue &) {}

public:
  MPMCBoundedQueue(uint64_t size = 1024) : size(size), mask(size - 1), buffer(reinterpret_cast<node_t *>(new aligned_node_t[size]))
  {
    assert((size != 0) && ((size & (~size + 1)) == size)); // enforce power of 2
    for (uint64_t i = 0; i < size; ++i)
      buffer[i].next.store(i, std::memory_order_relaxed);
  }

  ~MPMCBoundedQueue()
  {
    delete[] buffer;
  }

  bool sp_produce(T const &input)
  {
    uint64_t headSequence = head.load(std::memory_order_relaxed);

    node_t *node = &buffer[headSequence & mask];
    uint64_t nodeSequence = node->node.load(std::memory_order_acquire);
    intptr_t diff = (intptr_t)nodeSequence - (intptr_t)headSequence;

    if (diff == 0)
    {
      ++head;
      node->data = input;
      node->next.store(headSequence, std::memory_order_release);
      return true;
    }

    assert(diff < 0);
    return false;
  }

  bool mp_produce(const T &input)
  {
    uint64_t headSequence = head.load(std::memory_order_relaxed);

    while (true)
    {
      node_t *node = &buffer[headSequence & mask];
      uint64_t nodeSequence = node->next.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)nodeSequence - (intptr_t)headSequence;

      if (dif == 0)
      {
        if (head.compare_exchange_weak(headSequence, headSequence + 1, std::memory_order_relaxed))
        {
          node->data = input;
          node->next.store(headSequence + 1, std::memory_order_release);
          return true;
        }
      }
      else if (dif < 0)
      {
        return false;
      }
      else
      {
        headSequence = head.load(std::memory_order_relaxed);
      }
    }

    return false;
  }

  bool consume(T &output)
  {
    uint64_t tailSequence = tail.load(std::memory_order_relaxed);

    while (true)
    {
      node_t *node = &buffer[tailSequence & mask];
      uint64_t nodeSequence = node->next.load(std::memory_order_acquire);
      intptr_t dif = (intptr_t)nodeSequence - (intptr_t)(tailSequence + 1);
      if (dif == 0)
      {
        if (tail.compare_exchange_weak(tailSequence, tailSequence + 1, std::memory_order_relaxed))
        {
          output = node->data;
          node->next.store(tailSequence + mask + 1, std::memory_order_release);
          return true;
        }
      }
      else if (dif < 0)
      {
        return false;
      }
      else
      {
        tailSequence = tail.load(std::memory_order_relaxed);
      }
    }
    return false;
  }
};

template <typename T>
class SPSCQueue
{

  struct node_t
  {
    node_t *next;
    T data;
  };
  typedef typename std::aligned_storage<sizeof(node_t), std::alignment_of<node_t>::value>::type node_aligned_t;

  node_t *head;
  char cache_line_pad[64];
  node_t *tail;
  node_t *back;

  SPSCQueue(const SPSCQueue &) {}
  void operator=(const SPSCQueue &) {}

public:
  SPSCQueue() : head(reinterpret_cast<node_t *>(new node_aligned_t)), tail(head)
  {
    head->next = nullptr;
  }

  ~SPSCQueue()
  {
    T output;
    while (this->consume(output))
    {
    }
    delete head;
  }

  bool produce(const T &input)
  {
    node_t *node = reinterpret_cast<node_t *>(new node_aligned_t);
    node->data = input;
    node->next = nullptr;

    std::atomic_thread_fence(std::memory_order_acq_rel);
    head->next = node;
    head = node;
    return true;
  }

  bool consume(T &output)
  {
    std::atomic_thread_fence(std::memory_order_consume);
    if (!tail->next)
      return false;
    output = tail->next->data;
    std::atomic_thread_fence(std::memory_order_acq_rel);
    back = tail;
    tail = back->next;
    delete back;
    return true;
  }
};

template <typename T>
class MPSCQueue
{
  struct buffer_node_t
  {
    T data;
    std::atomic<buffer_node_t *> next;
  };
  typedef typename std::aligned_storage<sizeof(buffer_node_t), std::alignment_of<buffer_node_t>::value>::type buffer_node_aligned_t;

  std::atomic<buffer_node_t *> head;
  std::atomic<buffer_node_t *> tail;

  MPSCQueue(const MPSCQueue &) {}
  void operator=(const MPSCQueue &) {}

public:
  MPSCQueue() : head(reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t)), tail(head.load(std::memory_order_relaxed))
  {
    buffer_node_t *front = head.load(std::memory_order_relaxed);
    front->next.store(nullptr, std::memory_order_relaxed);
  }

  ~MPSCQueue()
  {
    T output;
    while (this->consume(output))
    {
    }
    buffer_node_t *front = head.load(std::memory_order_relaxed);
    delete front;
  }

  bool produce(const T &input)
  {
    buffer_node_t *node = reinterpret_cast<buffer_node_t *>(new buffer_node_aligned_t);
    node->data = input;
    node->next.store(nullptr, std::memory_order_relaxed);
    buffer_node_t *prevhead = head.exchange(node, std::memory_order_acq_rel);
    prevhead->next.store(node, std::memory_order_release);
    return true;
  }

  bool consume(T &output)
  {
    buffer_node_t *t = tail.load(std::memory_order_relaxed);
    buffer_node_t *n = t->next.load(std::memory_order_acquire);
    if (n == nullptr)
      return false;
    output = n->data;
    tail.store(n, std::memory_order_release);
    delete t;
    return true;
  }

  bool available()
  {
    buffer_node_t *tail = tail.load(std::memory_order_relaxed);
    buffer_node_t *next = tail->next.load(std::memory_order_acquire);
    return next != nullptr;
  }
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