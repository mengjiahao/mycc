
#include "math_util.h"

namespace mycc
{
namespace util
{

/** Return the smallest number n such that (x >> n) == 0 (or 64 if the highest bit in x is set. */
uint64_t Count1Bits(uint64_t x)
{
  if (sizeof(unsigned long) >= sizeof(uint64_t))
  {
    return x ? 8 * sizeof(unsigned long) - __builtin_clzl(x) : 0;
  }
  if (sizeof(unsigned long long) >= sizeof(uint64_t))
  {
    return x ? 8 * sizeof(unsigned long long) - __builtin_clzll(x) : 0;
  }
  uint64_t ret = 0;
  while (x)
  {
    x >>= 1;
    ++ret;
  }
  return ret;
}

} // namespace util
} // namespace mycc