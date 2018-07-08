
#ifndef MYCC_UTIL_TYPES_UTIL_H_
#define MYCC_UTIL_TYPES_UTIL_H_

#include <endian.h>
#include <inttypes.h>
#include <limits.h> // So we can set the bounds of our types.
#include <stddef.h> // For size_t.
#include <stdint.h> // For intptr_t.
#include <string.h>
#include <sys/types.h>
#include <limits>
#include <string>
#include "macros_util.h"

namespace mycc
{

#ifndef INT8_MAX
#define INT8_MAX 0x7f // (127)
#endif
#ifndef INT8_MIN
#define INT8_MIN (-INT8_MAX - 1) // (-128)
#endif
#ifndef UINT8_MAX
#define UINT8_MAX (INT8_MAX * 2 + 1) // (255)
#endif
#ifndef INT16_MAX
#define INT16_MAX 0x7fff // (32767)
#endif
#ifndef INT16_MIN
#define INT16_MIN (-INT16_MAX - 1) // (-32767-1)
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 0xffff // (65535)
#endif
#ifndef INT32_MAX
#define INT32_MAX 0x7fffffffL // (2147483647)
#endif
#ifndef INT32_MIN
#define INT32_MIN (-INT32_MAX - 1L) // (-2147483647-1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffUL // (4294967295U)
#endif
#ifndef INT64_MAX
#define INT64_MAX 0x7fffffffffffffffLL //(9223372036854775807)
#endif
#ifndef INT64_MIN
#define INT64_MIN (-INT64_MAX - 1LL) // (-9223372036854775807-1)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffULL // (18446744073709551615)
#endif

// KB, MB, GB to bytes
#define KBYTES (1024L)
#define MBYTES (1024L * 1024L)
#define GBYTES (1024L * 1024L * 1024)

typedef float float32;
typedef double float64;

static const uint8_t kuint8max = ((uint8_t)0xFF);
static const uint16_t kuint16max = ((uint16_t)0xFFFF);
static const uint32_t kuint32max = ((uint32_t)0xFFFFFFFF);
static const uint64_t kuint64max = ((uint64_t)0xFFFFFFFFFFFFFFFFull);
static const int8_t kint8min = ((int8_t)~0x7F);
static const int8_t kint8max = ((int8_t)0x7F);
static const int16_t kint16min = ((int16_t)~0x7FFF);
static const int16_t kint16max = ((int16_t)0x7FFF);
static const int32_t kint32min = ((int32_t)~0x7FFFFFFF);
static const int32_t kint32max = ((int32_t)0x7FFFFFFF);
static const int64_t kint64min = ((int64_t)~0x7FFFFFFFFFFFFFFFll);
static const int64_t kint64max = ((int64_t)0x7FFFFFFFFFFFFFFFll);

static const int32_t kMaxInt32 = std::numeric_limits<int32_t>::max();
static const uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();
static const int64_t kMaxInt64 = std::numeric_limits<int64_t>::max();
static const size_t kMaxSizet = std::numeric_limits<size_t>::max();
static const float kFloatMax = std::numeric_limits<float>::max();
static const float kFloatMin = std::numeric_limits<float>::min();

/* To avoid dividing by zero */
static const float kVerySmallNumber = 1e-15;
static const double kVerySmallNumberDouble = 1e-15;

using ::std::string;

// Cacheline related --------------------------------------

#ifndef CACHE_LINE_SIZE
#if defined(__s390__)
#define CACHE_LINE_SIZE 256U
#elif defined(__powerpc__) || defined(__aarch64__)
#define CACHE_LINE_SIZE 128U
#else
#define CACHE_LINE_SIZE 64U
#endif
#endif // CACHE_LINE_SIZE

#ifndef CACHE_LINE_ALIGNMENT
#if defined(COMPILER_MSVC)
#define CACHE_LINE_ALIGNMENT __declspec(align(CACHE_LINE_SIZE))
#elif defined(COMPILER_GCC)
#define CACHE_LINE_ALIGNMENT __attribute__((aligned(CACHE_LINE_SIZE)))
#else
#define CACHE_LINE_ALIGNMENT
#endif
#endif // CACHE_LINE_ALIGNMENT

/// platform portable

namespace port
{

constexpr bool kLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

//  Memory locations within the same cache line are subject to destructive
  //  interference, also known as false sharing, which is when concurrent
  //  accesses to these different memory locations from different cores, where at
  //  least one of the concurrent accesses is or involves a store operation,
  //  induce contention and harm performance.
  //
  //  Microbenchmarks indicate that pairs of cache lines also see destructive
  //  interference under heavy use of atomic operations, as observed for atomic
  //  increment on Sandy Bridge.
  //
  //  We assume a cache line size of 64, so we use a cache line pair size of 128
  //  to avoid destructive interference.
  //
  //  mimic: std::hardware_destructive_interference_size, C++17
constexpr uint64_t k_hardware_destructive_interference_size = 128;

// Prefetching support
//
// Defined behavior on some of the uarchs:
// PREFETCH_HINT_T0:
//   prefetch to all levels of the hierarchy (except on p4: prefetch to L2)
// PREFETCH_HINT_NTA:
//   p4: fetch to L2, but limit to 1 way (out of the 8 ways)
//   core: skip L2, go directly to L1
//   k8 rev E and later: skip L2, can go to either of the 2-ways in L1
enum PrefetchHint
{
  PREFETCH_HINT_T0 = 3, // More temporal locality
  PREFETCH_HINT_T1 = 2,
  PREFETCH_HINT_T2 = 1, // Less temporal locality
  PREFETCH_HINT_NTA = 0 // No temporal locality
};
template <PrefetchHint hint>
void prefetch(const void *x);

// ---------------------------------------------------------------------------
// Inline implementation
// ---------------------------------------------------------------------------
template <PrefetchHint hint>
inline void prefetch(const void *x)
{
// Check of COMPILER_GCC macro below is kept only for backward-compatibility
// reasons. COMPILER_GCC3 is the macro that actually enables prefetch.
#if defined(__llvm__) || defined(COMPILER_GCC) || defined(COMPILER_GCC3)
  __builtin_prefetch(x, 0, hint);
#else
  // You get no effect.  Feel free to add more sections above.
  static_assert(false, "prefetch unimplemented");
#endif
}

} // namespace port

// Delete the typed-T object whose address is `arg'. This is a common function
// to thread_atexit.
template <typename T>
void delete_object(void *arg)
{
  delete static_cast<T *>(arg);
}

template <typename T>
inline void CheckedDelete(T *p)
{
  typedef char type_must_be_complete[sizeof(T) ? 1 : -1];
  (void)sizeof(type_must_be_complete);
  delete p;
}

template <typename T>
inline void CheckedArrayDelete(T *p)
{
  typedef char type_must_be_complete[sizeof(T) ? 1 : -1];
  (void)sizeof(type_must_be_complete);
  delete[] p;
}

// The type-based aliasing rule allows the compiler to assume that pointers of
// different types (for some definition of different) never alias each other.
// Thus the following code does not work:
//
// float f = foo();
// int fbits = *(int*)(&f);
//
// The compiler 'knows' that the int pointer can't refer to f since the types
// don't match, so the compiler may cache f in a register, leaving random data
// in fbits.  Using C++ style casts makes no difference, however a pointer to
// char data is assumed to alias any other pointer.  This is the 'memcpy
// exception'.
//
// Bit_cast uses the memcpy exception to move the bits from a variable of one
// type of a variable of another type.  Of course the end result is likely to
// be implementation dependent.  Most compilers (gcc-4.2 and MSVC 2005)
// will completely optimize BitCast away.
//
// There is an additional use for BitCast.
// Recent gccs will warn when they see casts that may result in breakage due to
// the type-based aliasing rule.  If you have checked that there is no breakage
// you can use BitCast to cast one pointer type to another.  This confuses gcc
// enough that it can no longer see that you have cast one pointer type to
// another thus avoiding the warning.
template <class Dest, class Source>
inline Dest BitCast(const Source &source)
{
  static_assert(sizeof(Dest) == sizeof(Source),
                "BitCast's source and destination types must be the same size");

  Dest dest;
  ::memmove(&dest, &source, sizeof(dest));
  return dest;
}

template <class Dest, class Source>
inline Dest BitCast(Source *source)
{
  return BitCast<Dest>(reinterpret_cast<uintptr_t>(source));
}

} // namespace mycc

#endif // MYCC_UTIL_TYPES_UTIL_H_