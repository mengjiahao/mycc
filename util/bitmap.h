
#ifndef MYCC_UTIL_BITMAP_H_
#define MYCC_UTIL_BITMAP_H_

#include <malloc.h>
#include <string.h>
#include "math_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

static const int32_t NBITS_IN_BYTE = 8; /* number of bits in a byte */
/* Bit map related macros. */
#define easybit_setbit(a, i) (((unsigned char *)(a))[(i) / NBITS_IN_BYTE] |= 1 << ((i) % NBITS_IN_BYTE))
#define easybit_clrbit(a, i) (((unsigned char *)(a))[(i) / NBITS_IN_BYTE] &= ~(1 << ((i) % NBITS_IN_BYTE)))
#define easybit_isset(a, i) \
  (((const unsigned char *)(a))[(i) / NBITS_IN_BYTE] & (1 << ((i) % NBITS_IN_BYTE)))
#define easybit_isclr(a, i) \
  ((((const unsigned char *)(a))[(i) / NBITS_IN_BYTE] & (1 << ((i) % NBITS_IN_BYTE))) == 0)

// Note: use gcc

// Create an array with at least |nbit| bits. The array is not cleared.
inline uint64_t *bit_array_malloc(uint64_t nbit)
{
  if (!nbit)
  {
    return NULL;
  }
  return (uint64_t *)malloc((nbit + 63) / 64 * 8 /*different from /8*/);
}

// Set bit 0 ~ nbit-1 of |array| to be 0
inline void bit_array_clear(uint64_t *array, uint64_t nbit)
{
  const uint64_t off = (nbit >> 6);
  memset(array, 0, off * 8);
  const uint64_t last = (off << 6);
  if (last != nbit)
  {
    array[off] &= ~((((uint64_t)1) << (nbit - last)) - 1);
  }
}

// Set i-th bit (from left, counting from 0) of |array| to be 1
inline void bit_array_set(uint64_t *array, uint64_t i)
{
  const uint64_t off = (i >> 6);
  array[off] |= (((uint64_t)1) << (i - (off << 6)));
}

// Set i-th bit (from left, counting from 0) of |array| to be 0
inline void bit_array_unset(uint64_t *array, uint64_t i)
{
  const uint64_t off = (i >> 6);
  array[off] &= ~(((uint64_t)1) << (i - (off << 6)));
}

// Get i-th bit (from left, counting from 0) of |array|
inline uint64_t bit_array_get(const uint64_t *array, uint64_t i)
{
  const uint64_t off = (i >> 6);
  return (array[off] & (((uint64_t)1) << (i - (off << 6))));
}

// Find index of first 1-bit from bit |begin| to |end| in |array|.
// Returns |end| if all bits are 0.
// This function is of O(nbit) complexity.
inline uint64_t bit_array_first1(const uint64_t *array, uint64_t begin, uint64_t end)
{
  uint64_t off1 = (begin >> 6);
  const uint64_t first = (off1 << 6);
  if (first != begin)
  {
    const uint64_t v =
        array[off1] & ~((((uint64_t)1) << (begin - first)) - 1);
    if (v)
    {
      return MATH_MIN(first + __builtin_ctzl(v), end);
    }
    ++off1;
  }

  const uint64_t off2 = (end >> 6);
  for (uint64_t i = off1; i < off2; ++i)
  {
    if (array[i])
    {
      return i * 64 + __builtin_ctzl(array[i]);
    }
  }
  const uint64_t last = (off2 << 6);
  if (last != end && array[off2])
  {
    return MATH_MIN(last + __builtin_ctzl(array[off2]), end);
  }
  return end;
}

class Bitmap
{
public:
  // Create a bitmap that holds 0 bits.
  Bitmap();

  // Create a bitmap that holds n bits, all initially zero.
  explicit Bitmap(uint64_t n);

  ~Bitmap();

  // Return the number of bits that the bitmap can hold.
  uint64_t bits() const;

  // Replace contents of *this with a bitmap of n bits, all set to zero.
  void Reset(uint64_t n);

  // Return the contents of the ith bit.
  // REQUIRES: i < bits()
  bool get(uint64_t i) const;

  // Set the contents of the ith bit to true.
  // REQUIRES: i < bits()
  void set(uint64_t i);

  // Set the contents of the ith bit to false.
  // REQUIRES: i < bits()
  void clear(uint64_t i);

  // Return the smallest i such that i >= start and !get(i).
  // Returns bits() if no such i exists.
  uint64_t FirstUnset(uint64_t start) const;

  // Returns the bitmap as an ascii string of '0' and '1' characters, bits()
  // characters in length.
  string ToString() const;

private:
  typedef uint32_t Word;
  static const uint64_t kBits = 32;

  // Return the number of words needed to store n bits.
  static uint64_t NumWords(uint64_t n) { return (n + kBits - 1) / kBits; }

  // Return the mask to use for the ith bit in a word.
  static Word Mask(uint64_t i) { return 1ull << i; }

  uint64_t nbits_; // Length of bitmap in bits.
  Word *word_;

  DISALLOW_COPY_AND_ASSIGN(Bitmap);
};

// Implementation details follow.  Clients should ignore.

inline Bitmap::Bitmap() : nbits_(0), word_(nullptr) {}

inline Bitmap::Bitmap(uint64_t n) : Bitmap() { Reset(n); }

inline Bitmap::~Bitmap() { delete[] word_; }

inline uint64_t Bitmap::bits() const { return nbits_; }

inline bool Bitmap::get(uint64_t i) const
{
  //DCHECK_LT(i, nbits_);
  return word_[i / kBits] & Mask(i % kBits);
}

inline void Bitmap::set(uint64_t i)
{
  //DCHECK_LT(i, nbits_);
  word_[i / kBits] |= Mask(i % kBits);
}

inline void Bitmap::clear(uint64_t i)
{
  //DCHECK_LT(i, nbits_);
  word_[i / kBits] &= ~Mask(i % kBits);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BITMAP_H_