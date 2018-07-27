
#ifndef MYCC_UTIL_ATOMIC_UTIL_H_
#define MYCC_UTIL_ATOMIC_UTIL_H_

#include <sched.h>
#include <atomic>
#include "macros_util.h"

namespace mycc
{
namespace util
{

#if !defined(__i386__) && !defined(__x86_64__)
#error "Arch not supprot asm atomic!"
#endif

/*
 * Instruct the compiler to perform only a single access to a variable
 * (prohibits merging and refetching). The compiler is also forbidden to reorder
 * successive instances of ACCESS_ONCE(), but only when the compiler is aware of
 * particular ordering. Compiler ordering can be ensured, for example, by
 * putting two ACCESS_ONCE() in separate C statements.
 *
 * This macro does absolutely -nothing- to prevent the CPU from reordering,
 * merging, or refetching absolutely anything at any time.  Its main intended
 * use is to mediate communication between process-level code and irq/NMI
 * handlers, all running on the same CPU.
 */
#define ACCESS_ONCE(x) (*(volatile __typeof__(x) *)&(x))

inline void CompilerBarrier()
{
  __asm__ __volatile__(""
                       :
                       :
                       : "memory");
}

#if defined __i386__ || defined __x86_64__
// x86/x64 has a relatively strong memory model,
// but on x86/x64 StoreLoad reordering (later loads passing earlier stores) can happen.
// Both a compiler barrier and CPU barrier.
inline void MemoryBarrier()
{
  __asm__ __volatile__("mfence"
                       :
                       :
                       : "memory");
}
inline void MemoryReadBarrier() { __asm__ __volatile__("lfence" ::
                                                           : "memory"); }
inline void MemoryWriteBarrier() { __asm__ __volatile__("sfence" ::
                                                            : "memory"); }
#else
#error Unsupportted platform.
#endif

/*
 * lfence : performs a serializing operation on all load-from-memory
 * instructions that were issued prior the LFENCE instruction.
 */
inline void SMP_RMB()
{
  asm volatile("lfence" ::
                   : "memory");
}

/*
 * sfence : the means same as lfence.
 */
inline void SMP_WMB()
{
  asm volatile("sfence" ::
                   : "memory");
}

/*
 * mfence : performs a serializing operation on all load-from-memory and
 * store-to-memory instructions that were issued prior the MFENCE instructions.
 */
inline void SMP_MB()
{
  asm volatile("mfence" ::
                   : "memory");
}

inline void REP_NOP()
{
  asm volatile("rep;nop" ::
                   : "memory");
}

inline void CPU_RELAX()
{
  asm volatile("rep;nop" ::
                   : "memory");
}

/*
 * Load a data from shared memory, doing a cache flush if required.
 * A cmm_smp_rmc() or cmm_smp_mc() should come before the load.
 */
#define MM_LOAD_SHARED(p) \
  __extension__({         \
    CompilerBarrier();    \
    ACCESS_ONCE(p);       \
  })

/*
 * Store v into x, where x is located in shared memory.
 * Performs the required cache flush after writing. Returns v.
 * A cmm_smp_wmc() or cmm_smp_mc() should follow the store.
 */
#define MM_STORE_SHARED(x, v)                         \
  __extension__							\
	({								\
		__typeof__(x) _v = { ACCESS_ONCE(x) = (v); };		\
		CompilerBarrier();						\
		_v = _v;	/* Work around clang "unused result" */ \
  })

inline void AsmVolatilePause()
{
#if defined(__i386__) || defined(__x86_64__)
  asm volatile("pause");
#elif defined(__aarch64__)
  asm volatile("wfe");
#elif defined(__powerpc64__)
  asm volatile("or 27,27,27");
#endif
  // it's okay for other platforms to be no-ops
}

// Pause instruction to prevent excess processor bus usage, only works in GCC
inline void AsmVolatileCpuRelax()
{
  asm volatile("pause\n"
               :
               :
               : "memory");
}

/*
 * Atomic operations that C can't guarantee us.  
 * Useful for resource counting etc..
 */

#define LOCK_PREFIX "lock ; "

//return: the incremented value;
#ifdef __x86_64__
static __inline__ uint64_t atomic_inc(volatile uint64_t *pv)
{
  register unsigned long __res;
  __asm__ __volatile__(
      "movq $1,%0;" LOCK_PREFIX "xaddq %0,(%1);"
      "incq %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}
#endif

static __inline__ uint32_t atomic_inc(volatile uint32_t *pv)
{
  register unsigned int __res;
  __asm__ __volatile__(
      "movl $1,%0;" LOCK_PREFIX "xaddl %0,(%1);"
      "incl %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

static __inline__ uint16_t atomic_inc(volatile uint16_t *pv)
{
  register unsigned short __res;
  __asm__ __volatile__(
      "movw $1,%0;" LOCK_PREFIX "xaddw %0,(%1);"
      "incw %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

static __inline__ uint8_t atomic_inc(volatile uint8_t *pv)
{
  register unsigned char __res;
  __asm__ __volatile__(
      "movb $1,%0;" LOCK_PREFIX "xaddb %0,(%1);"
      "incb %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

//return: the decremented value;
#ifdef __x86_64__
static __inline__ uint64_t atomic_dec(volatile uint64_t *pv)
{
  register unsigned long __res;
  __asm__ __volatile__(
      "movq $0xffffffffffffffff,%0;" LOCK_PREFIX "xaddq %0,(%1);"
      "decq %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}
#endif

static __inline__ uint32_t atomic_dec(volatile uint32_t *pv)
{
  register unsigned int __res;
  __asm__ __volatile__(
      "movl $0xffffffff,%0;" LOCK_PREFIX "xaddl %0,(%1);"
      "decl %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

static __inline__ uint16_t atomic_dec(volatile uint16_t *pv)
{
  register unsigned short __res;
  __asm__ __volatile__(
      "movw $0xffff,%0;" LOCK_PREFIX "xaddw %0,(%1);"
      "decw %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

static __inline__ uint8_t atomic_dec(volatile uint8_t *pv)
{
  register unsigned char __res;
  __asm__ __volatile__(
      "movb $0xff,%0;" LOCK_PREFIX "xaddb %0,(%1);"
      "decb %0"
      : "=a"(__res), "=q"(pv)
      : "1"(pv));
  return __res;
}

//return: the initial value of *pv
#ifdef __x86_64__
static __inline__ uint64_t atomic_add(volatile uint64_t *pv, const uint64_t av)
{
  //:"=a" (__res), "=q" (pv): "m"(av), "1" (pv));
  register unsigned long __res;
  __asm__ __volatile__(
      "movq %2,%0;" LOCK_PREFIX "xaddq %0,(%1);"
      : "=a"(__res), "=q"(pv)
      : "mr"(av), "1"(pv));
  return __res;
}
#endif

static __inline__ uint32_t atomic_add(volatile uint32_t *pv, const uint32_t av)
{
  //:"=a" (__res), "=q" (pv): "m"(av), "1" (pv));
  register unsigned int __res;
  __asm__ __volatile__(
      "movl %2,%0;" LOCK_PREFIX "xaddl %0,(%1);"
      : "=a"(__res), "=q"(pv)
      : "mr"(av), "1"(pv));
  return __res;
}

static __inline__ uint16_t atomic_add(volatile uint16_t *pv, const uint16_t av)
{
  //:"=a" (__res), "=q" (pv): "m"(av), "1" (pv));
  register unsigned short __res;
  __asm__ __volatile__(
      "movw %2,%0;" LOCK_PREFIX "xaddw %0,(%1);"
      : "=a"(__res), "=q"(pv)
      : "mr"(av), "1"(pv));
  return __res;
}

static __inline__ uint8_t atomic_add(volatile uint8_t *pv, const uint8_t av)
{
  //:"=a" (__res), "=q" (pv): "m"(av), "1" (pv));
  register unsigned char __res;
  __asm__ __volatile__(
      "movb %2,%0;" LOCK_PREFIX "xaddb %0,(%1);"
      : "=a"(__res), "=q"(pv)
      : "mr"(av), "1"(pv));
  return __res;
}

//function: set *pv to nv
//return: the initial value of *pv
#ifdef __x86_64__
static __inline__ uint64_t atomic_exchange(volatile uint64_t *pv, const uint64_t nv)
{
  register unsigned long __res;
  __asm__ __volatile__(
      "1:" LOCK_PREFIX "cmpxchgq %3,(%1);"
      "jne 1b"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(*pv));
  return __res;
}
#endif

static __inline__ uint32_t atomic_exchange(volatile uint32_t *pv, const uint32_t nv)
{
  register unsigned int __res;
  __asm__ __volatile__(
      "1:" LOCK_PREFIX "cmpxchgl %3,(%1);"
      "jne 1b"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(*pv));
  return __res;
}

static __inline__ uint16_t atomic_exchange(volatile uint16_t *pv, const uint16_t nv)
{
  register unsigned short __res;
  __asm__ __volatile__(
      "1:" LOCK_PREFIX "cmpxchgw %3,(%1);"
      "jne 1b"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(*pv));
  return __res;
}

static __inline__ uint8_t atomic_exchange(volatile uint8_t *pv, const uint8_t nv)
{
  register unsigned char __res;
  __asm__ __volatile__(
      "1:" LOCK_PREFIX "cmpxchgb %3,(%1);"
      "jne 1b"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(*pv));
  return __res;
}

//function: compare *pv to cv, if equal, set *pv to nv, otherwise do nothing.
//return: the initial value of *pv
#ifdef __x86_64__
static __inline__ uint64_t atomic_compare_exchange(volatile uint64_t *pv,
                                                   const uint64_t nv, const uint64_t cv)
{
  register unsigned long __res;
  __asm__ __volatile__(
      LOCK_PREFIX "cmpxchgq %3,(%1)"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(cv));
  return __res;
}
#endif

static __inline__ uint32_t atomic_compare_exchange(volatile uint32_t *pv,
                                                   const uint32_t nv, const uint32_t cv)
{
  register unsigned int __res;
  __asm__ __volatile__(
      LOCK_PREFIX "cmpxchgl %3,(%1)"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(cv));
  return __res;
}

static __inline__ uint16_t atomic_compare_exchange(volatile uint16_t *pv,
                                                   const uint16_t nv, const uint16_t cv)
{
  register unsigned short __res;
  __asm__ __volatile__(
      LOCK_PREFIX "cmpxchgw %3,(%1)"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(cv));
  return __res;
}

static __inline__ uint8_t atomic_compare_exchange(volatile uint8_t *pv,
                                                  const uint8_t nv, const uint8_t cv)
{
  register unsigned char __res;
  __asm__ __volatile__(
      LOCK_PREFIX "cmpxchgb %3,(%1)"
      : "=a"(__res), "=q"(pv)
      : "1"(pv), "q"(nv), "0"(cv));
  return __res;
}

//function: set *pv to nv
//return: the initial value of *pv
static __inline__ void *atomic_exchange_pointer(volatile void **pv, const void *nv)
{
#ifdef __x86_64__
  return (void *)atomic_exchange((uint64_t *)pv, (uint64_t)nv);
#else
  return (void *)atomic_exchange((uint32_t *)pv, (uint32_t)nv);
#endif
}

//function: compare *pv to cv, if equal, set *pv to nv, otherwise do nothing.
//return: the initial value of *pv
static __inline__ void *atomic_compare_exchange_pointer(volatile void **pv,
                                                        const void *nv, const void *cv)
{
#ifdef __x86_64__
  return (void *)atomic_compare_exchange((uint64_t *)pv, (uint64_t)nv, (uint64_t)cv);
#else
  return (void *)atomic_compare_exchange((uint32_t *)pv, (uint32_t)nv, (uint32_t)cv);
#endif
}

static __inline__ int32_t atomic_cmp_set(volatile int32_t *lock, int32_t old, int32_t set)
{
  uint8_t res;
  __asm__ volatile(
      LOCK_PREFIX "cmpxchgl %3, %1; sete %0"
      : "=a"(res)
      : "m"(*lock), "a"(old), "r"(set)
      : "cc", "memory");
  return res;
}

#define atomic_trylock(lock) (*(lock) == 0 && atomic_cmp_set(lock, 0, 1))
#define atomic_unlock(lock)  \
  {                          \
    __asm__("" ::            \
                : "memory"); \
    *(lock) = 0;             \
  }
#define atomic_spin_unlock atomic_unlock

static __inline__ void atomic_spin_lock(volatile int32_t *lock)
{
  int32_t i, n;

  for (;;)
  {
    if (*lock == 0 && atomic_cmp_set(lock, 0, 1))
    {
      return;
    }

    for (n = 1; n < 1024; n <<= 1)
    {

      for (i = 0; i < n; i++)
      {
        __asm__(".byte 0xf3, 0x90");
      }

      if (*lock == 0 && atomic_cmp_set(lock, 0, 1))
      {
        return;
      }
    }

    sched_yield();
  }
}

// Legacy __sync Built-in Functions for Atomic Memory Access.
// https://gcc.gnu.org/onlinedocs/gcc-4.4.4/gcc/Atomic-Builtins.html
// The definition given in the Intel documentation allows only for
// the use of the types int, long, long long or their unsigned counterparts.

#define GCC_SYNC_FETCH_AND_ADD(ptr, value, ...) __sync_fetch_and_add(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_FETCH_AND_SUB(ptr, value, ...) __sync_fetch_and_sub(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_FETCH_AND_OR(ptr, value, ...) __sync_fetch_and_or(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_FETCH_AND_AND(ptr, value, ...) __sync_fetch_and_and(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_FETCH_AND_XOR(ptr, value, ...) __sync_fetch_and_xor(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_FETCH_AND_NAND(ptr, value, ...) __sync_fetch_and_nand(ptr, value, ##__VA_ARGS__)

#define GCC_SYNC_ADD_AND_FETCH(ptr, value, ...) __sync_add_and_fetch(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_SUB_AND_FETCH(ptr, value, ...) __sync_sub_and_fetch(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_OR_AND_FETCH(ptr, value, ...) __sync_or_and_fetch(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_AND_AND_FETCH(ptr, value, ...) __sync_and_and_fetch(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_XOR_AND_FETCH(ptr, value, ...) __sync_xor_and_fetch(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_NAND_AND_FETCH(ptr, value, ...) __sync_nand_and_fetch(ptr, value, ##__VA_ARGS__)

#define GCC_SYNC_BOOL_COMPARE_AND_SWAP(ptr, value, newval, ...) __sync_bool_compare_and_swap(ptr, value, newval, ##__VA_ARGS__)
#define GCC_SYNC_VAL_COMPARE_AND_SWAP(ptr, value, newval, ...) __sync_val_compare_and_swap(ptr, value, newval, ##__VA_ARGS__)

#define GCC_SYNC_SYNCHRONIZE(...) __sync_synchronize(##__VA_ARGS__)
#define GCC_SYNC_LOCK_TEST_AND_SET(ptr, value, ...) __sync_lock_test_and_set(ptr, value, ##__VA_ARGS__)
#define GCC_SYNC_LOCK_RELEASE(ptr, ...) __sync_lock_release(ptr, ##__VA_ARGS__)

//! Atomically add 'val' to an uint32_t
//! "mem": pointer to the object
//! "val": amount to add
//! Returns the old value pointed to by mem
inline uint32_t atomic_sync_add32(volatile uint32_t *mem, uint32_t val)
{
  return __sync_fetch_and_add(const_cast<uint32_t *>(mem), val);
}

//! Atomically increment an apr_uint32_t by 1
//! "mem": pointer to the object
//! Returns the old value pointed to by mem
inline uint32_t atomic_sync_inc32(volatile uint32_t *mem)
{
  return atomic_sync_add32(mem, 1);
}

//! Atomically decrement an uint32_t by 1
//! "mem": pointer to the atomic value
//! Returns the old value pointed to by mem
inline uint32_t atomic_sync_dec32(volatile uint32_t *mem)
{
  return atomic_sync_add32(mem, (uint32_t)-1);
}

//! Atomically read an uint32_t from memory
inline uint32_t atomic_sync_read32(volatile uint32_t *mem)
{
  uint32_t old_val = *mem;
  __sync_synchronize();
  return old_val;
}

//! Compare an uint32_t's value with "cmp".
//! If they are the same swap the value with "with"
//! "mem": pointer to the value
//! "with" what to swap it with
//! "cmp": the value to compare it to
//! Returns the old value of *mem
inline uint32_t atomic_sync_cas32(volatile uint32_t *mem, uint32_t with, uint32_t cmp)
{
  return __sync_val_compare_and_swap(const_cast<uint32_t *>(mem), cmp, with);
}

//! Atomically set an uint32_t in memory
//! "mem": pointer to the object
//! "param": val value that the object will assume
inline void atomic_sync_write32(volatile uint32_t *mem, uint32_t val)
{
  __sync_synchronize();
  *mem = val;
}

inline bool atomic_sync_add_unless32(volatile uint32_t *mem, uint32_t value, uint32_t unless_this)
{
  uint32_t old, c(atomic_sync_read32(mem));
  while (c != unless_this && (old = atomic_sync_cas32(mem, c + value, c)) != c)
  {
    c = old;
  }
  return c != unless_this;
}

inline void AtomicSyncMemoryBarrier()
{
  __sync_synchronize();
}

template <typename T>
inline T AtomicSyncGet(volatile T *ptr)
{
  T v = __sync_sub_and_fetch(ptr, 0);
  return v;
}

template <typename T>
inline void AtomicSyncSet(volatile T *ptr, T v)
{
  while (!__sync_bool_compare_and_swap(ptr, *ptr, v))
    ;
}

template <typename T>
inline T AtomicSyncLoad(volatile T *ptr)
{
  T v = *ptr;
  AtomicSyncMemoryBarrier();
  return v;
}

template <typename T>
inline T AtomicSyncAcquireLoad(volatile T *ptr)
{
  T v = *ptr;
  AtomicSyncMemoryBarrier();
  return v;
}

template <typename T>
inline T AtomicSyncReleaseLoad(volatile T *ptr)
{
  AtomicSyncMemoryBarrier();
  return *ptr;
}

template <typename T>
inline void AtomicSyncStore(volatile T *ptr, T val)
{
  AtomicSyncMemoryBarrier();
  *ptr = val;
}

template <typename T>
inline void AtomicSyncAcquireStore(volatile T *ptr, T val)
{
  *ptr = val;
  AtomicSyncMemoryBarrier();
}

template <typename T>
inline void AtomicSyncReleaseStore(volatile T *ptr, T val)
{
  AtomicSyncMemoryBarrier();
  *ptr = val;
}

template <typename T>
inline T AtomicSyncFetchAdd(volatile T *ptr, T value)
{
  return __sync_fetch_and_add(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchSub(volatile T *ptr, T value)
{
  return __sync_fetch_and_sub(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchOr(volatile T *ptr, T value)
{
  return __sync_fetch_and_or(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchAnd(volatile T *ptr, T value)
{
  return __sync_fetch_and_and(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchXor(volatile T *ptr, T value)
{
  return __sync_fetch_and_xor(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchNand(volatile T *ptr, T value)
{
  return __sync_fetch_and_nand(ptr, value);
}

template <typename T>
inline T AtomicSyncFetchInc(volatile T *ptr)
{
  return __sync_fetch_and_add(ptr, 1);
}

template <typename T>
inline T AtomicvFetchDec(volatile T *ptr)
{
  return __sync_fetch_and_sub(ptr, 1);
}

template <typename T>
inline T AtomicSyncAddFetch(volatile T *ptr, T value)
{
  return __sync_add_and_fetch(ptr, value);
}

template <typename T>
inline T AtomicSyncSubFetch(volatile T *ptr, T val)
{
  return __sync_sub_and_fetch(ptr, val);
}

template <typename T>
inline T AtomicSyncOrFetch(volatile T *ptr, T value)
{
  return __sync_or_and_fetch(ptr, value);
}

template <typename T>
inline T AtomicSyncAndFetch(volatile T *ptr, T val)
{
  return __sync_and_and_fetch(ptr, val);
}

template <typename T>
inline T AtomicSyncXorFetch(volatile T *ptr, T val)
{
  return __sync_xor_and_fetch(ptr, val);
}

template <typename T>
inline T AtomicSyncNandFetch(volatile T *ptr, T val)
{
  return __sync_nand_and_fetch(ptr, val);
}

template <typename T>
inline T AtomicSyncIncFetch(volatile T *ptr)
{
  return __sync_add_and_fetch(ptr, 1);
}

template <typename T>
inline T AtomicSyncDecFetch(volatile T *ptr)
{
  return __sync_sub_and_fetch(ptr, 1);
}

template <typename T>
inline T AtomicSyncValCompareAndSwap(volatile T *ptr,
                                     T old_value,
                                     T new_value)
{
  return __sync_val_compare_and_swap(ptr, old_value, new_value);
}

template <typename T>
inline T AtomicSyncCompareAndSwap(volatile T *ptr,
                                  T old_value,
                                  T new_value)
{
  // Since CompareAndSwap uses __sync_bool_compare_and_swap, which
  // is a full memory barrier, none is needed here or below in Release.
  T prev_value;
  do
  {
    if (__sync_bool_compare_and_swap(ptr, old_value, new_value))
      return old_value;
    prev_value = *ptr;
  } while (prev_value == old_value);
  return prev_value;
}

template <typename T>
inline T AtomicSyncExchange(volatile T *ptr,
                            T new_value)
{
  T old_value;
  do
  {
    old_value = *ptr;                                                 // atomic_read?
  } while (!__sync_bool_compare_and_swap(ptr, old_value, new_value)); // cmpxchg
  return old_value;
}

template <typename T>
inline T AtomicSyncCasInc(volatile T *ptr,
                          T increment)
{
  for (;;)
  {
    // Atomic exchange the old value with an incremented one.
    T old_value = *ptr;
    T new_value = old_value + increment;
    if (__sync_bool_compare_and_swap(ptr, old_value, new_value))
    {
      // The exchange took place as expected.
      return new_value;
    }
    // Otherwise, *ptr changed mid-loop and we need to retry.
  }
}

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

#define AtomicCompilerBarrier() std::atomic_signal_fence(std::memory_order_seq_cst)

// This built-in function implements an atomic load operation. It returns the contents of *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, and __ATOMIC_CONSUME.
template <typename T>
inline T AtomicLoadN(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_load_n(ptr, memorder);
}

// This is the generic version of an atomic load. It returns the contents of *ptr in *ret.
template <typename T>
inline void AtomicLoad(volatile T *ptr, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_load(ptr, ret, memorder);
}

// This built-in function implements an atomic store operation. It writes val into *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
inline void AtomicStoreN(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store_n(ptr, val, memorder);
}

// This is the generic version of an atomic store. It stores the value of *val into *ptr.
template <typename T>
inline void AtomicStore(volatile T *ptr, T *val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  __atomic_store(ptr, val, memorder);
}

// This built-in function implements an atomic exchange operation.
// It writes val into *ptr, and returns the previous contents of *ptr.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, __ATOMIC_ACQUIRE, __ATOMIC_RELEASE, and __ATOMIC_ACQ_REL.
template <typename T>
inline T AtomicExchangeN(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_exchange_n(ptr, val, memorder);
}

// This is the generic version of an atomic exchange. It stores the contents of *val into *ptr.
// The original value of *ptr is copied into *ret.
template <typename T>
inline void AtomicExchange(volatile T *ptr, T *val, T *ret, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
inline bool AtomicCompareExchangeN(volatile T *ptr, T *expected, T desired, bool weak,
                                   AtomicMemoryOrder success_memorder, AtomicMemoryOrder failure_memorder)
{
  return __atomic_compare_exchange_n(ptr, expected, desired, weak,
                                     success_memorder, failure_memorder);
}

// This built-in function implements the generic version of __atomic_compare_exchange.
// The function is virtually identical to __atomic_compare_exchange_n,
// except the desired value is also a pointer.
template <typename T>
inline bool AtomicCompareExchange(volatile T *ptr, T *expected, T *desired, bool weak,
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
inline T AtomicAddFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, val, memorder);
}

template <typename T>
inline T AtomicSubFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_sub_fetch(ptr, val, memorder);
}

template <typename T>
inline T AtomicAndFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_and_fetch(ptr, val, memorder);
}

template <typename T>
inline T AtomicXorFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_xor_fetch(ptr, val, memorder);
}

template <typename T>
inline T AtomicOrFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_or_fetch(ptr, val, memorder);
}

template <typename T>
inline T AtomicNandFetch(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
inline T AtomicFetchAdd(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_add(ptr, val, memorder);
}

template <typename T>
inline T AtomicFetchSub(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_sub(ptr, val, memorder);
}

template <typename T>
inline T AtomicFetchAnd(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_and(ptr, val, memorder);
}

template <typename T>
inline T AtomicFetchXor(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_xor(ptr, val, memorder);
}

template <typename T>
inline T AtomicFetchOr(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_or(ptr, val, memorder);
}

template <typename T>
inline T AtomicFetchNand(volatile T *ptr, T val, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_fetch_nand(ptr, val, memorder);
}

template <typename T>
T AtomicIncFetch(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_add_fetch(ptr, 1, memorder);
}

template <typename T>
inline T AtomicDecFetch(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
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
inline bool AtomicTestAndSet(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_test_and_set(ptr, memorder);
}

// This built-in function performs an atomic clear operation on *ptr.
// After the operation, *ptr contains 0. It should be only used for operands of type bool or char and in conjunction with __atomic_test_and_set.
// For other types it may only clear partially. If the type is not bool prefer using __atomic_store.
// The valid memory order variants are __ATOMIC_RELAXED, __ATOMIC_SEQ_CST, and __ATOMIC_RELEASE.
template <typename T>
inline bool AtomicClear(volatile T *ptr, AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_clear(ptr, memorder);
}

// This built-in function acts as a synchronization fence between threads based on the specified memory order.
// All memory orders are valid.
inline void AtomicThreadFence(AtomicMemoryOrder memorder = MEMORY_ORDER_ATOMIC_SEQ_CST)
{
  return __atomic_thread_fence(memorder);
}

//////////////////////// asm atomicops ////////////////////////////////

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

inline void Acquire_Store(volatile Atomic32 *ptr, Atomic32 value)
{
  *ptr = value;
  MemoryBarrier();
}

inline void Release_Store(volatile Atomic32 *ptr, Atomic32 value)
{
  CompilerBarrier();
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
  CompilerBarrier();
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
  CompilerBarrier();

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
  CompilerBarrier();
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

template <typename T>
class AtomicInteger
{
public:
  AtomicInteger()
      : value_(0)
  {
  }

  void store(T v)
  {
    return AtomicStore(&value_, v);
  }

  T load()
  {
    return AtomicLoad(&value_);
  }

  T fetch_add(T v)
  {
    return AtomicFetchAdd(&value_, v);
  }

  T fetch_sub(T v)
  {
    return AtomicFetchSub(&value_, v);
  }

  T fetch_and(T v)
  {
    return AtomicFetchAnd(&value_, v);
  }

  T fetch_or(T v)
  {
    return AtomicFetchOr(&value_, v);
  }

  T fetch_xor(T v)
  {
    return AtomicFetchXor(&value_, v);
  }

  T inc_fetch()
  {
    return AtomicIncFetch(&value_);
  }

  T dec_fetch()
  {
    return AtomicDecFetch(&value_);
  }

private:
  volatile T value_;

  DISALLOW_COPY_AND_ASSIGN(AtomicInteger);
};

typedef AtomicInteger<int32_t> AtomicInteger32;
typedef AtomicInteger<int64_t> AtomicInteger64;
typedef AtomicInteger<uint32_t> AtomicUinteger32;
typedef AtomicInteger<uint64_t> AtomicUinteger64;

// A type that holds a pointer that can be read or written atomically
// (i.e., without word-tearing.)
#if defined(BASE_CXX11_ENABLED)

class AtomicPointer
{
private:
  std::atomic<void *> rep_;

public:
  AtomicPointer() {}
  explicit AtomicPointer(void *v) : rep_(v) {}

  inline void *Acquire_Load() const
  {
    return rep_.load(std::memory_order_acquire);
  }

  inline void Release_Store(void *v)
  {
    rep_.store(v, std::memory_order_release);
  }

  inline void *NoBarrier_Load() const
  {
    return rep_.load(std::memory_order_relaxed);
  }

  inline void NoBarrier_Store(void *v)
  {
    rep_.store(v, std::memory_order_relaxed);
  }
};

#else

class AtomicPointer
{
private:
  void *rep_;

public:
  AtomicPointer() {}
  explicit AtomicPointer(void *p) : rep_(p) {}

  inline void *NoBarrier_Load() const { return rep_; }
  inline void NoBarrier_Store(void *v) { rep_ = v; }

  inline void *Acquire_Load() const
  {
    void *result = rep_;
    MemoryBarrier();
    return result;
  }

  inline void Release_Store(void *v)
  {
    MemoryBarrier();
    rep_ = v;
  }
};

#endif // AtomicPointer

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ATOMIC_UTIL_H_
