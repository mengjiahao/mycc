
#ifndef MYCC_UTIL_HASH_UTIL_H_
#define MYCC_UTIL_HASH_UTIL_H_

#include <stddef.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

extern uint32_t Hash(const char *data, uint64_t n, uint32_t seed);
extern uint32_t Hash32(const char *data, uint64_t n, uint32_t seed);
extern uint64_t Hash64(const char *data, uint64_t n, uint64_t seed);

inline uint64_t Hash64(const char *data, uint64_t n)
{
  return Hash64(data, n, 0xDECAFCAFFE);
}

inline uint64_t Hash64(const string &str)
{
  return Hash64(str.data(), str.size());
}

inline uint64_t Hash64Combine(uint64_t a, uint64_t b)
{
  return a ^ (b + 0x9e3779b97f4a7800ULL + (a << 10) + (a >> 4));
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_HASH_UTIL_H_