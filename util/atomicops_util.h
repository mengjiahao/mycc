
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
T AtomicLoadN(T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_load_n(ptr, memorder);
}

// This is the generic version of an atomic load. It returns the contents of *ptr in *ret.
template <typename T>
void AtomicLoad(T *ptr, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_load(ptr, ret, memorder);
}

// This built-in function implements an atomic store operation. It writes val into *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
void AtomicStoreN(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store_n(ptr, val, memorder);
}

// This is the generic version of an atomic store. It stores the value of *val into *ptr.
template <typename T>
void AtomicStore(T *ptr, T *val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store(ptr, val, memorder);
}

// This built-in function implements an atomic exchange operation.
// It writes val into *ptr, and returns the previous contents of *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, __ATOMIC_RELEASE, and __ATOMIC_ACQ_REL.
template <typename T>
T AtomicExchangeN(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_exchange_n(ptr, val, memorder);
}

// This is the generic version of an atomic exchange. It stores the contents of *val into *ptr.
// The original value of *ptr is copied into *ret.
template <typename T>
void AtomicExchange(T *ptr, T *val, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
bool AtomicCompareExchangeN(T *ptr, T *expected, T desired, bool weak,
                            AtomicMemoryOrder success_memorder, AtomicMemoryOrder failure_memorder)
{
  return __atomic_compare_exchange_n(ptr, expected, desired, weak,
                                     success_memorder, failure_memorder);
}

// This built-in function implements the generic version of __atomic_compare_exchange.
// The function is virtually identical to __atomic_compare_exchange_n,
// except the desired value is also a pointer.
template <typename T>
bool AtomicCompareExchange(T *ptr, T *expected, T *desired, bool weak,
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
T AtomicAddFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicSubFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_sub_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicAndFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_and_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicXorFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_xor_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicOrFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_or_fetch(ptr, val, memorder);
}

template <typename T>
T AtomicNandFetch(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
T AtomicFetchAdd(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_add(ptr, val, memorder);
}

template <typename T>
T AtomicFetchSub(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_sub(ptr, val, memorder);
}

template <typename T>
T AtomicFetchAnd(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_and(ptr, val, memorder);
}

template <typename T>
T AtomicFetchXor(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_xor(ptr, val, memorder);
}

template <typename T>
T AtomicFetchOr(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_or(ptr, val, memorder);
}

template <typename T>
T AtomicFetchNand(T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_nand(ptr, val, memorder);
}

template <typename T>
T AtomicIncrement(T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, 1, memorder);
}

template <typename T>
T AtomicDecrement(T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
bool AtomicTestAndSet(T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_test_and_set(ptr, memorder);
}

// This built-in function performs an atomic clear operation on *ptr.
// After the operation, *ptr contains 0. It should be only used for operands of type bool or char and in conjunction with __atomic_test_and_set.
// For other types it may only clear partially. If the type is not bool prefer using __atomic_store.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
bool AtomicClear(T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_clear(ptr, memorder);
}

// This built-in function acts as a synchronization fence between threads based on the specified memory order.
// All memory orders are valid.
void AtomicThreadFence(AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_thread_fence(memorder);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ATOMICOPS_UTIL_H_
