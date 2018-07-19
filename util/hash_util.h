
#ifndef MYCC_UTIL_HASH_UTIL_H_
#define MYCC_UTIL_HASH_UTIL_H_

#include <stddef.h>
#include <utility>
#include "types_util.h"

namespace mycc
{
namespace util
{

// Implement hashing for pairs of at-most 32 bit integer values.
// When size_t is 32 bits, we turn the 64-bit hash code into 32 bits by using
// multiply-add hashing. This algorithm, as described in
// Theorem 4.3.3 of the thesis "Über die Komplexität der Multiplikation in
// eingeschränkten Branchingprogrammmodellen" by Woelfel, is:
//
//   h32(x32, y32) = (h64(x32, y32) * rand_odd64 + rand16 * 2^16) % 2^64 / 2^32
//
// Contact danakj@chromium.org for any questions.
inline size_t HashInts32(uint32_t value1, uint32_t value2)
{
  uint64_t value1_64 = value1;
  uint64_t hash64 = (value1_64 << 32) | value2;

  if (sizeof(size_t) >= sizeof(uint64_t))
    return static_cast<size_t>(hash64);

  uint64_t odd_random = 481046412LL << 32 | 1025306955LL;
  uint32_t shift_random = 10121U << 16;

  hash64 = hash64 * odd_random + shift_random;
  size_t high_bits =
      static_cast<size_t>(hash64 >> (8 * (sizeof(uint64_t) - sizeof(size_t))));
  return high_bits;
}

// Implement hashing for pairs of up-to 64-bit integer values.
// We use the compound integer hash method to produce a 64-bit hash code, by
// breaking the two 64-bit inputs into 4 32-bit values:
// http://opendatastructures.org/versions/edition-0.1d/ods-java/node33.html#SECTION00832000000000000000
// Then we reduce our result to 32 bits if required, similar to above.
inline size_t HashInts64(uint64_t value1, uint64_t value2)
{
  uint32_t short_random1 = 842304669U;
  uint32_t short_random2 = 619063811U;
  uint32_t short_random3 = 937041849U;
  uint32_t short_random4 = 3309708029U;

  uint32_t value1a = static_cast<uint32_t>(value1 & 0xffffffff);
  uint32_t value1b = static_cast<uint32_t>((value1 >> 32) & 0xffffffff);
  uint32_t value2a = static_cast<uint32_t>(value2 & 0xffffffff);
  uint32_t value2b = static_cast<uint32_t>((value2 >> 32) & 0xffffffff);

  uint64_t product1 = static_cast<uint64_t>(value1a) * short_random1;
  uint64_t product2 = static_cast<uint64_t>(value1b) * short_random2;
  uint64_t product3 = static_cast<uint64_t>(value2a) * short_random3;
  uint64_t product4 = static_cast<uint64_t>(value2b) * short_random4;

  uint64_t hash64 = product1 + product2 + product3 + product4;

  if (sizeof(size_t) >= sizeof(uint64_t))
    return static_cast<size_t>(hash64);

  uint64_t odd_random = 1578233944LL << 32 | 194370989LL;
  uint32_t shift_random = 20591U << 16;

  hash64 = hash64 * odd_random + shift_random;
  size_t high_bits =
      static_cast<size_t>(hash64 >> (8 * (sizeof(uint64_t) - sizeof(size_t))));
  return high_bits;
}

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

/*!
 * \brief hash an object and combines the key with previous keys
 */
template <typename T>
inline uint64_t StdHashCombine(uint64_t key, const T &value)
{
  std::hash<T> hash_func;
  return key ^ (hash_func(value) + 0x9e3779b9 + (key << 6) + (key >> 2));
}

// Hash functor suitable for use with power-of-two sized hashtables.  Use
// instead of std::hash<T>.
//
// In particular, tensorflow::hash is not the identity function for pointers.
// This is important for power-of-two sized hashtables like FlatMap and FlatSet,
// because otherwise they waste the majority of their hash buckets.
template <typename T>
struct hash
{
  uint64_t operator()(const T &t) const { return std::hash<T>()(t); }
};

template <typename T>
struct hash<T *>
{
  uint64_t operator()(const T *t) const
  {
    // Hash pointers as integers, but bring more entropy to the lower bits.
    uint64_t k = static_cast<uint64_t>(reinterpret_cast<uintptr_t>(t));
    return k + (k >> 6);
  }
};

// Implement string hash functions so that strings of various flavors can
// be used as keys in STL maps and sets.  The hash algorithm comes from the
// GNU C++ library, in <tr1/functional>.  It is duplicated here because GCC
// versions prior to 4.3.2 are unable to compile <tr1/functional> when RTTI
// is disabled, as it is in our build.

// template <>
// struct hash<string_type>
// {
//   std::size_t operator()(const string_type &s) const
//   {
//     std::size_t result = 0;
//     for (string_type::const_iterator i = s.begin(); i != s.end(); ++i)
//       result = (result * 131) + *i;
//     return result;
//   }
// }

template <>
struct hash<string>
{
  uint64_t operator()(const string &s) const
  {
    return static_cast<uint64_t>(Hash64(s));
  }
};

template <typename T, typename U>
struct hash<std::pair<T, U>>
{
  uint64_t operator()(const std::pair<T, U> &p) const
  {
    return Hash64Combine(hash<T>()(p.first), hash<U>()(p.second));
  }
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_HASH_UTIL_H_