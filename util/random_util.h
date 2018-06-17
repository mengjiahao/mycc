
#ifndef MYCC_UTIL_RANDOM_UTIL_H_
#define MYCC_UTIL_RANDOM_UTIL_H_

#include <stdint.h>
#include <random>

namespace mycc
{
namespace util
{

// A very simple random number generator.  Not especially good at
// generating truly random bits, but good enough for our needs in this
// package.
class Random
{
private:
  static const uint32_t M = 2147483647L; // 2^31-1
  static const uint64_t A = 16807;       // bits 14, 8, 7, 5, 2, 1, 0

  uint32_t seed_;

  static uint32_t GoodSeed(uint32_t s) { return (s & M) != 0 ? (s & M) : 1; }

public:
  // This is the largest value that can be returned from Next()
  static const uint32_t kMaxNext = M;

  explicit Random(uint32_t s) : seed_(GoodSeed(s)) {}

  void reset(uint32_t s) { seed_ = GoodSeed(s); }

  uint32_t next()
  {
    // We are computing
    //       seed_ = (seed_ * A) % M,    where M = 2^31-1
    //
    // seed_ must not be zero or M, or else all subsequent computed values
    // will be zero or M respectively.  For all other values, seed_ will end
    // up cycling through every number in [1,M-1]
    uint64_t product = seed_ * A;

    // Compute (product % M) using the fact that ((x << 31) % M) == x.
    seed_ = static_cast<uint32_t>((product >> 31) + (product & M));
    // The first reduction may overflow by 1 bit, so we may need to
    // repeat.  mod == M is not possible; using > allows the faster
    // sign-bit-based test.
    if (seed_ > M)
    {
      seed_ -= M;
    }
    return seed_;
  }

  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint32_t uniform(int32_t n) { return next() % n; }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool oneIn(int32_t n) { return (next() % n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint32_t skewed(int32_t max_log)
  {
    return uniform(1 << uniform(max_log + 1));
  }
};

// A simple 64bit random number generator based on std::mt19937_64
class Random64
{
private:
  std::mt19937_64 generator_;

public:
  explicit Random64(uint64_t s) : generator_(s) {}

  // Generates the next random number
  uint64_t next() { return generator_(); }

  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint64_t uniform(uint64_t n)
  {
    return std::uniform_int_distribution<uint64_t>(0, n - 1)(generator_);
  }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool oneIn(uint64_t n) { return uniform(n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint64_t skewed(int32_t max_log)
  {
    return uniform(uint64_t(1) << uniform(max_log + 1));
  }
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_RANDOM_UTIL_H_
