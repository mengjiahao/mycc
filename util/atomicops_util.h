
#ifndef MYCC_UTIL_ATOMICOPS_UTIL_H_
#define MYCC_UTIL_ATOMICOPS_UTIL_H_

#include <atomic>

namespace mycc
{
namespace util
{

// Use gcc c++11 built-in functions for memory model aware atomic operations

enum AtomicMemoryOrder
{
  MEMORY_ORDER_ATOMIC_RELAXED = __ATOMIC_RELAXED,
  MEMORY_ORDER_ATOMIC_CONSUME = __ATOMIC_CONSUME,
  MEMORY_ORDER_ATOMIC_ACQUIRE = __ATOMIC_ACQUIRE,
  MEMORY_ORDER_ATOMIC_RELEASE = __ATOMIC_RELEASE,
  MEMORY_ORDER_ATOMIC_ACQ_REL = __ATOMIC_ACQ_REL,
  MEMORY_ORDER_ATOMIC_SEQ_CST = __ATOMIC_SEQ_CST,
};

// This built-in function implements an atomic load operation. It returns the contents of *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, and __ATOMIC_CONSUME.
template <typename T>
T AtomicLoadN(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_load_n(ptr, memorder);
}

// This is the generic version of an atomic load. It returns the contents of *ptr in *ret.
template <typename T>
void AtomicLoad(volatile T *ptr, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_load(ptr, ret, memorder);
}

// This built-in function implements an atomic store operation. It writes val into *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
void AtomicStoreN(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store_n(ptr, val, memorder);
}

// This is the generic version of an atomic store. It stores the value of *val into *ptr.
template <typename T>
void AtomicStore(volatile T *ptr, T *val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store(ptr, val, memorder);
}

// This built-in function implements an atomic exchange operation.
// It writes val into *ptr, and returns the previous contents of *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, __ATOMIC_RELEASE, and __ATOMIC_ACQ_REL.
template <typename T>
T AtomicExchangeN(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_exchange_n(ptr, val, memorder);
}

// This is the generic version of an atomic exchange. It stores the contents of *val into *ptr.
// The original value of *ptr is copied into *ret.
template <typename T>
void AtomicExchange(volatile T *ptr, T *val, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_exchange(ptr, val, ret, memorder);
}

// This built-in function implements an atomic compare and exchange operation.
// This compares the contents of *ptr with the contents of *expected.
// If equal, the operation is a read-modify-write operation that writes desired into *ptr.
// If they are not equal, the operation is a read and the current contents of *ptr are written into *expected.
// weak is true for weak compare_exchange, which may fail spuriously, and false for the strong variation, which never fails spuriously.
// Many targets only offer the strong variation and ignore the parameter. When in doubt, use the strong variation.
// If desired is written into *ptr then true is returned and memory is affected according to the memory order specified by success_memorder.
// There are no restrictions on what memory order can be used here.
// Otherwise, false is returned and memory is affected according to failure_memorder.
// This memory order cannot be __ATOMIC_RELEASE nor __ATOMIC_ACQ_REL.
// It also cannot be a stronger order than that specified by success_memorder.
template <typename T>
bool AtomicCompareExchangeN(volatile T *ptr, T *expected, T desired, bool weak,
                            AtomicMemoryOrder success_memorder, AtomicMemoryOrder failure_memorder)
{
  return __atomic_compare_exchange_n(ptr, expected, desired, weak,
                                     success_memorder, failure_memorder);
}

// This built-in function implements the generic version of __atomic_compare_exchange.
// The function is virtually identical to __atomic_compare_exchange_n,
// except the desired value is also a pointer.
template <typename T>
bool AtomicCompareExchange(volatile T *ptr, T *expected, T *desired, bool weak,
                           AtomicMemoryOrder success_memorder, AtomicMemoryOrder failure_memorder)
{
  return __atomic_compare_exchange(ptr, expected, desired, weak,
                                   success_memorder, failure_memorder);
}

// These built-in functions perform the operation suggested by the name,
// and return the result of the operation.
// Operations on pointer arguments are performed as if the operands were of the uintptr_t type.
// That is, they are not scaled by the size of the type to which the pointer points.
// The object pointed to by the first argument must be of integer or pointer type.
// It must not be a boolean type.
// All memory orders are valid.
template <typename T>
T AtomicAddFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicSubFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_sub_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicAndFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_and_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicXorFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_xor_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicOrFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_or_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicNandFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_nand_fetch(ptr, val, memorder);
}

// These built-in functions perform the operation suggested by the name,
// and return the value that had previously been in *ptr.
// Operations on pointer arguments are performed as if the operands were of the uintptr_t type.
// That is, they are not scaled by the size of the type to which the pointer points.
// The same constraints on arguments apply as for the corresponding __atomic_op_fetch built-in functions.
// All memory orders are valid.
template <typename T>
T AtomicFetchAdd(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_add(ptr, val, memorder);
}

template <typename T>
T AtomicFetchSub(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_sub(ptr, val, memorder);
}

template <typename T>
T AtomicFetchAnd(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_and(ptr, val, memorder);
}

template <typename T>
T AtomicFetchXor(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_xor(ptr, val, memorder);
}

template <typename T>
T AtomicFetchOr(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_or(ptr, val, memorder);
}

template <typename T>
T AtomicFetchNand(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_nand(ptr, val, memorder);
}

template <typename T>
T AtomicIncrement(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, 1, memorder);
}

template <typename T>
T AtomicDecrement(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_sub_fetch(ptr, 1, memorder);
}

// This built-in function performs an atomic test-and-set operation on the byte at *ptr.
// The byte is set to some implementation defined nonzero “set” value and the return value is true
// if and only if the previous contents were “set”.
// It should be only used for operands of type bool or char.
// For other types only part of the value may be set.
// All memory orders are valid.
template <typename T>
bool AtomicTestAndSet(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_test_and_set(ptr, memorder);
}

// This built-in function performs an atomic clear operation on *ptr.
// After the operation, *ptr contains 0. It should be only used for operands of type bool or char and in conjunction with __atomic_test_and_set.
// For other types it may only clear partially. If the type is not bool prefer using __atomic_store.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
bool AtomicClear(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_clear(ptr, memorder);
}

// This built-in function acts as a synchronization fence between threads based on the specified memory order.
// All memory orders are valid.
void AtomicThreadFence(AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_thread_fence(memorder);
}

/// asm atomicops

#if !defined(__i386__) && !defined(__x86_64__)
#error "Arch not supprot asm atomic!"
#endif

#define ATOMICOPS_COMPILER_BARRIER() __asm__ __volatile__("" \
                                                          :  \
                                                          :  \
                                                          : "memory")
typedef int32_t Atomic32;
typedef intptr_t Atomic64;
// Use AtomicWord for a machine-sized pointer.  It will use the Atomic32 or
// Atomic64 routines below, depending on your architecture.
typedef intptr_t AtomicWord;

// This struct is not part of the public API of this module; clients may not
// use it.  (However, it's exported via BASE_EXPORT because clients implicitly
// do use it at link time by inlining these functions.)
// Features of this x86.  Values may not be correct before main() is run,
// but are set conservatively.
static const bool k_has_amd_lock_mb_bug = false; // Processor has AMD memory-barrier bug; do lfence
                                                 // after acquire compare-and-swap.

// 32-bit low-level operations on any platform.

// Atomically execute:
//      result = *ptr;
//      if (*ptr == old_value)
//        *ptr = new_value;
//      return result;
//
// I.e., replace "*ptr" with "new_value" if "*ptr" used to be "old_value".
// Always return the old value of "*ptr"
//
// This routine implies no memory barriers.
Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32 *ptr,
                                  Atomic32 old_value,
                                  Atomic32 new_value);

// Atomically store new_value into *ptr, returning the previous value held in
// *ptr.  This routine implies no memory barriers.
Atomic32 NoBarrier_AtomicExchange(volatile Atomic32 *ptr, Atomic32 new_value);

// Atomically increment *ptr by "increment".  Returns the new value of
// *ptr with the increment applied.  This routine implies no memory barriers.
Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32 *ptr, Atomic32 increment);

Atomic32 Barrier_AtomicIncrement(volatile Atomic32 *ptr,
                                 Atomic32 increment);

// These following lower-level operations are typically useful only to people
// implementing higher-level synchronization operations like spinlocks,
// mutexes, and condition-variables.  They combine CompareAndSwap(), a load, or
// a store with appropriate memory-ordering instructions.  "Acquire" operations
// ensure that no later memory access can be reordered ahead of the operation.
// "Release" operations ensure that no previous memory access can be reordered
// after the operation.  "Barrier" operations have both "Acquire" and "Release"
// semantics.   A MemoryBarrier() has "Barrier" semantics, but does no memory
// access.
Atomic32 Acquire_CompareAndSwap(volatile Atomic32 *ptr,
                                Atomic32 old_value,
                                Atomic32 new_value);
Atomic32 Release_CompareAndSwap(volatile Atomic32 *ptr,
                                Atomic32 old_value,
                                Atomic32 new_value);

void MemoryBarrier();
void NoBarrier_Store(volatile Atomic32 *ptr, Atomic32 value);
void Acquire_Store(volatile Atomic32 *ptr, Atomic32 value);
void Release_Store(volatile Atomic32 *ptr, Atomic32 value);

Atomic32 NoBarrier_Load(volatile const Atomic32 *ptr);
Atomic32 Acquire_Load(volatile const Atomic32 *ptr);
Atomic32 Release_Load(volatile const Atomic32 *ptr);

// 64-bit atomic operations (only available on 64-bit processors).

Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64 *ptr,
                                  Atomic64 old_value,
                                  Atomic64 new_value);
Atomic64 NoBarrier_AtomicExchange(volatile Atomic64 *ptr, Atomic64 new_value);
Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64 *ptr, Atomic64 increment);
Atomic64 Barrier_AtomicIncrement(volatile Atomic64 *ptr, Atomic64 increment);

Atomic64 Acquire_CompareAndSwap(volatile Atomic64 *ptr,
                                Atomic64 old_value,
                                Atomic64 new_value);
Atomic64 Release_CompareAndSwap(volatile Atomic64 *ptr,
                                Atomic64 old_value,
                                Atomic64 new_value);
void NoBarrier_Store(volatile Atomic64 *ptr, Atomic64 value);
void Acquire_Store(volatile Atomic64 *ptr, Atomic64 value);
void Release_Store(volatile Atomic64 *ptr, Atomic64 value);
Atomic64 NoBarrier_Load(volatile const Atomic64 *ptr);
Atomic64 Acquire_Load(volatile const Atomic64 *ptr);
Atomic64 Release_Load(volatile const Atomic64 *ptr);

/// internal asm atomicops

inline Atomic32 NoBarrier_CompareAndSwap(volatile Atomic32 *ptr,
                                         Atomic32 old_value,
                                         Atomic32 new_value)
{
  Atomic32 prev;
  __asm__ __volatile__("lock; cmpxchgl %1,%2"
                       : "=a"(prev)
                       : "q"(new_value), "m"(*ptr), "0"(old_value)
                       : "memory");
  return prev;
}

inline Atomic32 NoBarrier_AtomicExchange(volatile Atomic32 *ptr,
                                         Atomic32 new_value)
{
  __asm__ __volatile__("xchgl %1,%0" // The lock prefix is implicit for xchg.
                       : "=r"(new_value)
                       : "m"(*ptr), "0"(new_value)
                       : "memory");
  return new_value; // Now it's the previous value.
}

inline Atomic32 NoBarrier_AtomicIncrement(volatile Atomic32 *ptr,
                                          Atomic32 increment)
{
  Atomic32 temp = increment;
  __asm__ __volatile__("lock; xaddl %0,%1"
                       : "+r"(temp), "+m"(*ptr)
                       :
                       : "memory");
  // temp now holds the old value of *ptr
  return temp + increment;
}

inline Atomic32 Barrier_AtomicIncrement(volatile Atomic32 *ptr,
                                        Atomic32 increment)
{
  Atomic32 temp = increment;
  __asm__ __volatile__("lock; xaddl %0,%1"
                       : "+r"(temp), "+m"(*ptr)
                       :
                       : "memory");
  // temp now holds the old value of *ptr
  if (k_has_amd_lock_mb_bug)
  {
    __asm__ __volatile__("lfence"
                         :
                         :
                         : "memory");
  }
  return temp + increment;
}

inline Atomic32 Acquire_CompareAndSwap(volatile Atomic32 *ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value)
{
  Atomic32 x = NoBarrier_CompareAndSwap(ptr, old_value, new_value);
  if (k_has_amd_lock_mb_bug)
  {
    __asm__ __volatile__("lfence"
                         :
                         :
                         : "memory");
  }
  return x;
}

inline Atomic32 Release_CompareAndSwap(volatile Atomic32 *ptr,
                                       Atomic32 old_value,
                                       Atomic32 new_value)
{
  return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

inline void NoBarrier_Store(volatile Atomic32 *ptr, Atomic32 value)
{
  *ptr = value;
}

inline void MemoryBarrier()
{
  __asm__ __volatile__("mfence"
                       :
                       :
                       : "memory");
}

inline void Acquire_Store(volatile Atomic32 *ptr, Atomic32 value)
{
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic32 *ptr, Atomic32 value)
{
  ATOMICOPS_COMPILER_BARRIER();
  *ptr = value; // An x86 store acts as a release barrier.
  // See comments in Atomic64 version of Release_Store(), below.
}

inline Atomic32 NoBarrier_Load(volatile const Atomic32 *ptr)
{
  return *ptr;
}

inline Atomic32 Acquire_Load(volatile const Atomic32 *ptr)
{
  Atomic32 value = *ptr; // An x86 load acts as a acquire barrier.
  // See comments in Atomic64 version of Release_Store(), below.
  ATOMICOPS_COMPILER_BARRIER();
  return value;
}

inline Atomic32 Release_Load(volatile const Atomic32 *ptr)
{
  MemoryBarrier();
  return *ptr;
}

// 64-bit low-level operations on 64-bit platform.

inline Atomic64 NoBarrier_CompareAndSwap(volatile Atomic64 *ptr,
                                         Atomic64 old_value,
                                         Atomic64 new_value)
{
  Atomic64 prev;
  __asm__ __volatile__("lock; cmpxchgq %1,%2"
                       : "=a"(prev)
                       : "q"(new_value), "m"(*ptr), "0"(old_value)
                       : "memory");
  return prev;
}

inline Atomic64 NoBarrier_AtomicExchange(volatile Atomic64 *ptr,
                                         Atomic64 new_value)
{
  __asm__ __volatile__("xchgq %1,%0" // The lock prefix is implicit for xchg.
                       : "=r"(new_value)
                       : "m"(*ptr), "0"(new_value)
                       : "memory");
  return new_value; // Now it's the previous value.
}

inline Atomic64 NoBarrier_AtomicIncrement(volatile Atomic64 *ptr,
                                          Atomic64 increment)
{
  Atomic64 temp = increment;
  __asm__ __volatile__("lock; xaddq %0,%1"
                       : "+r"(temp), "+m"(*ptr)
                       :
                       : "memory");
  // temp now contains the previous value of *ptr
  return temp + increment;
}

inline Atomic64 Barrier_AtomicIncrement(volatile Atomic64 *ptr,
                                        Atomic64 increment)
{
  Atomic64 temp = increment;
  __asm__ __volatile__("lock; xaddq %0,%1"
                       : "+r"(temp), "+m"(*ptr)
                       :
                       : "memory");
  // temp now contains the previous value of *ptr
  if (k_has_amd_lock_mb_bug)
  {
    __asm__ __volatile__("lfence"
                         :
                         :
                         : "memory");
  }
  return temp + increment;
}

inline void NoBarrier_Store(volatile Atomic64 *ptr, Atomic64 value)
{
  *ptr = value;
}

inline void Acquire_Store(volatile Atomic64 *ptr, Atomic64 value)
{
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic64 *ptr, Atomic64 value)
{
  ATOMICOPS_COMPILER_BARRIER();

  *ptr = value; // An x86 store acts as a release barrier
                // for current AMD/Intel chips as of Jan 2008.
                // See also Acquire_Load(), below.

  // When new chips come out, check:
  //  IA-32 Intel Architecture Software Developer's Manual, Volume 3:
  //  System Programming Guide, Chatper 7: Multiple-processor management,
  //  Section 7.2, Memory Ordering.
  // Last seen at:
  //   http://developer.intel.com/design/pentium4/manuals/index_new.htm
  //
  // x86 stores/loads fail to act as barriers for a few instructions (clflush
  // maskmovdqu maskmovq movntdq movnti movntpd movntps movntq) but these are
  // not generated by the compiler, and are rare.  Users of these instructions
  // need to know about cache behaviour in any case since all of these involve
  // either flushing cache lines or non-temporal cache hints.
}

inline Atomic64 NoBarrier_Load(volatile const Atomic64 *ptr)
{
  return *ptr;
}

inline Atomic64 Acquire_Load(volatile const Atomic64 *ptr)
{
  Atomic64 value = *ptr; // An x86 load acts as a acquire barrier,
                         // for current AMD/Intel chips as of Jan 2008.
                         // See also Release_Store(), above.
  ATOMICOPS_COMPILER_BARRIER();
  return value;
}

inline Atomic64 Release_Load(volatile const Atomic64 *ptr)
{
  MemoryBarrier();
  return *ptr;
}

inline Atomic64 Acquire_CompareAndSwap(volatile Atomic64 *ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value)
{
  Atomic64 x = NoBarrier_CompareAndSwap(ptr, old_value, new_value);
  if (k_has_amd_lock_mb_bug)
  {
    __asm__ __volatile__("lfence"
                         :
                         :
                         : "memory");
  }
  return x;
}

inline Atomic64 Release_CompareAndSwap(volatile Atomic64 *ptr,
                                       Atomic64 old_value,
                                       Atomic64 new_value)
{
  return NoBarrier_CompareAndSwap(ptr, old_value, new_value);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ATOMICOPS_UTIL_H_
