
#ifndef MYCC_UTIL_MATH_UTIL_H_
#define MYCC_UTIL_MATH_UTIL_H_

#include <assert.h>
#include <float.h>
#include <math.h>
#include <limits>
#include <utility>
#include "macros_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

#define MATH_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MATH_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define MATH_BEWTEEN(v, min_v, max_v) (((min_v) <= (v)) && ((v) <= (max_v))) // [min_v, max_v]

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

inline int32_t ToLog2(int32_t value)
{
  return static_cast<int32_t>(::floor(::log2(value)));
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

//===--------------------------------------------------------------------===//
// Find the next power of two higher than the provided value
//===--------------------------------------------------------------------===//
inline uint32_t NextPowerOf2Uint32(uint32_t n)
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
inline uint64_t NextPowerOf2Uint64(uint64_t n)
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
bool AlmostEquals(T a, T b)
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

inline uint8_t Count1BitsOfInt8(uint8_t i)
{
  // #number of 1 bits in 0x0 to 0xF.
  static const uint8_t kBitCountTable[] = {0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4};
  uint8_t count = 0;
  count += kBitCountTable[i & 0xf];
  count += kBitCountTable[(i >> 1) & 0xf];
  return count;
}

uint64_t Count1BitsOfInt64(uint64_t i);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MATH_UTIL_H_