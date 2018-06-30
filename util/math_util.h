
#ifndef MYCC_UTIL_MATH_UTIL_H_
#define MYCC_UTIL_MATH_UTIL_H_

#include <assert.h>
#include <float.h>
#include <math.h>
#include <limits>
#include <utility>
#include "types_util.h"

namespace mycc
{
namespace util
{

#define MATH_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MATH_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MATH_BEWTEEN(v, min_v, max_v) (((min_v) <= (v)) && ((v) <= (max_v))) // [min_v, max_v]

#ifndef FLT_EPSILON
#define FLT_EPSILON 1.192092896e-07F
#endif // FLT_EPSILON

#ifndef DBL_EPSILON
#define DBL_EPSILON 2.2204460492503131e-016
#endif

/**Util macro for conversion from degrees to radians.*/
#define MATH_DEG_TO_RAD(x) ((x)*0.0174532925f)
/**Util macro for conversion from radians to degrees.*/
#define MATH_RAD_TO_DEG(x) ((x)*57.29577951f)

//Util macro for const float such as epsilon, small float and float precision tolerance.
#define MATH_FLOAT_SMALL 1.0e-37f
#define MATH_TOLERANCE 2e-37f
#define MATH_PIOVER2 1.57079632679489661923f
#define MATH_PIOVER4 0.785398163397448309616f
#define MATH_EPSILON 0.000001f
#define MATH_PIX2 6.28318530717958647693f
#define MATH_E 2.71828182845904523536f
#define MATH_LOG10E 0.4342944819032518f
#define MATH_LOG2E 1.442695040888963387f
#define MATH_PI 3.14159265358979323846f
#define MATH_1_PI 0.31830988618379067154
#define MATH_RANDOM_MINUS1_1() ((2.0f * ((float)rand() / RAND_MAX)) - 1.0f) // Returns a random float between -1 and 1.
#define MATH_RANDOM_0_1() ((float)rand() / RAND_MAX)                        // Returns a random float between 0 and 1.
#define MATH_CLAMP(x, lo, hi) ((x < lo) ? lo : ((x > hi) ? hi : x))

//------------------------------------------------------------------------------
// Fast log()
//------------------------------------------------------------------------------

inline float fastlog2(float x)
{
  union {
    float f;
    uint32_t i;
  } vx = {x};
  union {
    uint32_t i;
    float f;
  } mx = {(vx.i & 0x007FFFFF) | 0x3f000000};
  float y = vx.i;
  y *= 1.1920928955078125e-7f;

  return y - 124.22551499f - 1.498030302f * mx.f - 1.72587999f / (0.3520887068f + mx.f);
}

inline float fastlog(float x)
{
  return 0.69314718f * fastlog2(x);
}

inline float fasterlog2(float x)
{
  union {
    float f;
    uint32_t i;
  } vx = {x};
  float y = vx.i;
  y *= 1.1920928955078125e-7f;
  return y - 126.94269504f;
}

inline float fasterlog(float x)
{
  union {
    float f;
    uint32_t i;
  } vx = {x};
  float y = vx.i;
  y *= 8.2629582881927490e-8f;
  return y - 87.989971088f;
}

//------------------------------------------------------------------------------
// Fast exp()
//------------------------------------------------------------------------------

inline float fastpow2(float p)
{
  float offset = (p < 0) ? 1.0f : 0.0f;
  float clipp = (p < -126) ? -126.0f : p;
  int w = clipp;
  float z = clipp - w + offset;
  union {
    uint32_t i;
    float f;
  } v = {(uint32_t)((1 << 23) * (clipp + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z))};

  return v.f;
}

inline float fastexp(float p)
{
  return fastpow2(1.442695040f * p);
}

inline float fasterpow2(float p)
{
  float clipp = (p < -126) ? -126.0f : p;
  union {
    uint32_t i;
    float f;
  } v = {(uint32_t)((1 << 23) * (clipp + 126.94269504f))};

  return v.f;
}

inline float fasterexp(float p)
{
  return fasterpow2(1.442695040f * p);
}

//------------------------------------------------------------------------------
// Fast pow()
//------------------------------------------------------------------------------

inline float fastpow(float x, float p)
{
  return fastpow2(p * fastlog2(x));
}

inline float fasterpow(float x, float p)
{
  return fasterpow2(p * fasterlog2(x));
}

//------------------------------------------------------------------------------
// Fast sigmoid()
//------------------------------------------------------------------------------

inline float fastsigmoid(float x)
{
  return 1.0f / (1.0f + fastexp(-x));
}

inline float fastersigmoid(float x)
{
  return 1.0f / (1.0f + fasterexp(-x));
}

//------------------------------------------------------------------------------
// 1 / sqrt() Magic function !!
//------------------------------------------------------------------------------
inline float InvSqrt(float x)
{
  float xhalf = 0.5f * x;
  int i = *reinterpret_cast<int *>(&x); // get bits for floating VALUE
  i = 0x5f375a86 - (i >> 1);            // gives initial guess y0
  x = *reinterpret_cast<float *>(&i);   // convert bits BACK to float
  x = x * (1.5f - xhalf * x * x);       // Newton step, repeating increases accuracy
  return x;
}

inline int32_t ToLog2(int32_t value)
{
  return static_cast<int32_t>(::floor(::log2(value)));
}

// ------------------------------------------------------------------------
// Implementation details follow
// ------------------------------------------------------------------------

#if defined(__GNUC__)

// Return floor(log2(n)) for positive integer n.  Returns -1 iff n == 0.
inline int32_t Log2Floor(uint32_t n) { return n == 0 ? -1 : 31 ^ __builtin_clz(n); }

// Return floor(log2(n)) for positive integer n.  Returns -1 iff n == 0.
inline int32_t Log2Floor64(uint64_t n)
{
  return n == 0 ? -1 : 63 ^ __builtin_clzll(n);
}

#else

// Return floor(log2(n)) for positive integer n.  Returns -1 iff n == 0.
inline int32_t Log2Floor(uint32_t n)
{
  if (n == 0)
    return -1;
  int32_t log = 0;
  uint32_t value = n;
  for (int32_t i = 4; i >= 0; --i)
  {
    int32_t shift = (1 << i);
    uint32_t x = value >> shift;
    if (x != 0)
    {
      value = x;
      log += shift;
    }
  }
  assert(value == 1);
  return log;
}

// Return floor(log2(n)) for positive integer n.  Returns -1 iff n == 0.
// Log2Floor64() is defined in terms of Log2Floor32()
inline int32_t Log2Floor64(uint64_t n)
{
  const uint32_t topbits = static_cast<uint32_t>(n >> 32);
  if (topbits == 0)
  {
    // Top bits are zero, so scan in bottom bits
    return Log2Floor(static_cast<uint32_t>(n));
  }
  else
  {
    return 32 + Log2Floor(topbits);
  }
}

#endif

inline int32_t Log2Ceiling(uint32_t n)
{
  int32_t floor = Log2Floor(n);
  if (n == (n & ~(n - 1))) // zero or a power of two
    return floor;
  else
    return floor + 1;
}

inline int32_t Log2Ceiling64(uint64_t n)
{
  int32_t floor = Log2Floor64(n);
  if (n == (n & ~(n - 1))) // zero or a power of two
    return floor;
  else
    return floor + 1;
}

inline uint32_t NextPowerOfTwo(uint32_t value)
{
  int32_t exponent = Log2Ceiling(value);
  //DCHECK_LT(exponent, std::numeric_limits<uint32_t>::digits);
  return 1 << exponent;
}

inline uint64_t NextPowerOfTwo64(uint64_t value)
{
  int32_t exponent = Log2Ceiling(value);
  //DCHECK_LT(exponent, std::numeric_limits<uint64_t>::digits);
  return 1LL << exponent;
}

// The number of bits necessary to hold the given index.
//
// ------------------------
//   sample input/output
// ------------------------
//   0           -->  0
//   1           -->  1
//   2,3         -->  2
//   4,5,6,7     -->  3
//   128,129,255 -->  8
// ------------------------
inline int32_t ToRadix(int32_t index)
{
  assert(index >= 0);
  return index == 0 ? 0 : 1 + ToLog2(index);
}

//===--------------------------------------------------------------------===//
// Count the number of leading zeroes in a given 64-bit unsigned number
//===--------------------------------------------------------------------===//
inline uint64_t CountLeadingZeroes(uint64_t i)
{
#if defined __GNUC__ || defined __clang__
  return __builtin_clzl(i);
#else
#error get a better compiler to CountLeadingZeroes
#endif
}

inline bool IsPowerOf2(uint64_t i)
{
  if (i < 2)
    return false;
  return (i & (i - 1)) == 0;
}

inline uint64_t UpperUint(const uint64_t i, const uint64_t fac)
{
  if (i % fac == 0)
  {
    return i;
  }
  return i + (fac - i % fac);
}

inline uint64_t LowerUint(const uint64_t i, const uint64_t fac)
{
  if (i % fac == 0)
  {
    return i;
  }
  return i - (i % fac);
}

inline uint64_t RoundupPow2(uint64_t i, uint64_t base2)
{
  return (i + base2 - 1) & (~(base2 - 1));
}

inline uint64_t Roundup(uint64_t x, uint64_t y)
{
  return ((x + y - 1) / y) * y;
}

inline uint64_t Rounddown(uint64_t x, uint64_t y)
{
  return (x / y) * y;
}

/// @brief round up pointer to next nearest aligned address
/// @param p the pointer
/// @param align alignment, must be power if 2
template <typename T>
T *RoundupPtr(T *p, size_t align)
{
  size_t address = reinterpret_cast<size_t>(p);
  return reinterpret_cast<T *>((address + align - 1) & ~(align - 1U));
}

/// @brief round down pointer to previous nearest aligned address
/// @param p the pointer
/// @param align alignment, must be power if 2
template <typename T>
T *RounddownPtr(T *p, size_t align)
{
  size_t address = reinterpret_cast<size_t>(p);
  return reinterpret_cast<T *>(address & ~(align - 1U));
}

//===--------------------------------------------------------------------===//
// Find the next power of two higher than the provided value
//===--------------------------------------------------------------------===//
inline uint32_t NextPowerOf2uint32_t(uint32_t n)
{
#if defined __GNUC__ || defined __clang__
  assert(n > 0);
  return 1ul << (64 - CountLeadingZeroes(n - 1));
#else
  // If input is a power of two, shift its high-order bit right.
  --n;
  // "Smear" the high-order bit all the way to the right.
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  return ++n;
#endif
}

//===--------------------------------------------------------------------===//
// Find the next power of two higher than the provided value
//===--------------------------------------------------------------------===//
inline uint64_t NextPowerOf2uint64_t(uint64_t n)
{
#if defined __GNUC__ || defined __clang__
  assert(n > 0);
  return 1ul << (64 - CountLeadingZeroes(n - 1));
#else
  // If input is a power of two, shift its high-order bit right.
  --n;
  // "Smear" the high-order bit all the way to the right.
  n |= n >> 1;
  n |= n >> 2;
  n |= n >> 4;
  n |= n >> 8;
  n |= n >> 16;
  n |= n >> 32;
  return ++n;
#endif
}

template <typename T>
inline bool AlmostEquals(T a, T b)
{
  return a == b;
}

template <>
inline bool AlmostEquals(float a, float b)
{
  return fabs(a - b) < 32 * FLT_EPSILON;
}

template <>
inline bool AlmostEquals(double a, double b)
{
  return fabs(a - b) < 32 * DBL_EPSILON;
}

inline uint64_t Div(uint64_t dividend, uint64_t divisor, uint64_t *remainder)
{

  *remainder = dividend % divisor;
  return (dividend / divisor);
}

/**
 * calculate the non-negative remainder of a/b
 * @param[in] a
 * @param[in] b, should be positive
 * @return the non-negative remainder of a / b
 */
inline int32_t Mod(int32_t a, int32_t b)
{
  int32_t r = a % b;
  return r >= 0 ? r : r + b;
}

inline uint8_t Count1Bits(uint8_t i)
{
  // #number of 1 bits in 0x0 to 0xF.
  static const uint8_t kBitCountTable[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  uint8_t count = 0;
  count += kBitCountTable[i & 0xf];
  count += kBitCountTable[(i >> 1) & 0xf];
  return count;
}

// Provides efficient bit operations.
// More details can be found at:
// http://graphics.stanford.edu/~seander/bithacks.html
// Counts set bits from a 32 bit unsigned integer using Hamming weight.
inline int32_t count1Bits(uint32_t value)
{
  int32_t count = 0;
  value = value - ((value >> 1) & 0x55555555);
  value = (value & 0x33333333) + ((value >> 2) & 0x33333333);
  count = (((value + (value >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24;

  return count;
}

// Return the smallest number n such that (x >> n) == 0
// (or 64 if the highest bit in x is set.
uint64_t Count1Bits(uint64_t x);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MATH_UTIL_H_