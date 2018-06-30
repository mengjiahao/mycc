
#ifndef MYCC_UTIL_BITMAP_H_
#define MYCC_UTIL_BITMAP_H_

#include "types_util.h"

namespace mycc
{
namespace util
{

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