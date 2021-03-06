
#ifndef MYCC_UTIL_RANDOM_UTIL_H_
#define MYCC_UTIL_RANDOM_UTIL_H_

#include <random>
#include "stringpiece.h"
#include "types_util.h"

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

  void Reset(uint32_t s) { seed_ = GoodSeed(s); }

  uint32_t Next()
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
  uint32_t Uniform(int32_t n) { return Next() % n; }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool OneIn(int32_t n) { return (Next() % n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint32_t Skewed(int32_t max_log)
  {
    return Uniform(1 << Uniform(max_log + 1));
  }

  // Returns a Random instance for use by the current thread without
  // additional locking
  static Random *GetTLSInstance();
};

// A simple 64bit random number generator based on std::mt19937_64
class Random64
{
private:
  std::mt19937_64 generator_;

public:
  explicit Random64(uint64_t s) : generator_(s) {}

  // Generates the next random number
  uint64_t Next() { return generator_(); }

  // Returns a uniformly distributed value in the range [0..n-1]
  // REQUIRES: n > 0
  uint64_t Uniform(uint64_t n)
  {
    return std::uniform_int_distribution<uint64_t>(0, n - 1)(generator_);
  }

  // Randomly returns true ~"1/n" of the time, and false otherwise.
  // REQUIRES: n > 0
  bool OneIn(uint64_t n) { return Uniform(n) == 0; }

  // Skewed: pick "base" uniformly from range [0,max_log] and then
  // return "base" random bits.  The effect is to pick a number in the
  // range [0,2^max_log-1] with exponential bias towards smaller numbers.
  uint64_t Skewed(int32_t max_log)
  {
    return Uniform(uint64_t(1) << Uniform(max_log + 1));
  }
};

/**
 * @class RandomHelper
 * @brief A helper class for creating random number.
 */
class RandomHelper
{
public:
  template <typename T>
  static T random_real(T min, T max)
  {
    std::uniform_real_distribution<T> dist(min, max);
    auto &mt = RandomHelper::getEngine();
    return dist(mt);
  }

  template <typename T>
  static T random_int(T min, T max)
  {
    std::uniform_int_distribution<T> dist(min, max);
    auto &mt = RandomHelper::getEngine();
    return dist(mt);
  }

private:
  static std::mt19937 &getEngine();
};

/**
 * Returns a random float between -1 and 1.
 * It can be seeded using std::srand(seed);
 */
inline float rand_minus1_1()
{
  // FIXME: using the new c++11 random engine generator
  // without a proper way to set a seed is not useful.
  // Resorting to the old random method since it can
  // be seeded using std::srand()
  return ((rand() / (float)RAND_MAX) * 2) - 1;

  //    return cocos2d::random(-1.f, 1.f);
};

/**
 * Returns a random float between 0 and 1.
 * It can be seeded using std::srand(seed);
 */
inline float rand_0_1()
{
  // FIXME: using the new c++11 random engine generator
  // without a proper way to set a seed is not useful.
  // Resorting to the old random method since it can
  // be seeded using std::srand()
  return rand() / (float)RAND_MAX;

  //    return cocos2d::random(0.f, 1.f);
};

class TrueRandom
{
public:
  TrueRandom();
  ~TrueRandom();

  uint32_t NextUInt32();
  uint32_t NextUInt32(uint32_t max_random);
  bool NextBytes(void *buffer, uint64_t size);

private:
  int m_fd;
  DISALLOW_COPY_AND_ASSIGN(TrueRandom);
};

// --------------------------------------------------------------------------
// Functions in this header read from /dev/urandom in posix
// systems and are not proper for performance critical situations.
// For fast random numbers, check fast_rand.h
// --------------------------------------------------------------------------
void RandBytes(void *output, uint64_t output_length);

// Returns a random number in range [0, kuint64max]. Thread-safe.
uint64_t RandUint64();

// Generate a 128-bit random GUID of the form: "%08X-%04X-%04X-%04X-%012llX".
// If GUID generation fails an empty string is returned.
// The POSIX implementation uses psuedo random number generation to create
// the GUID.  The Windows implementation uses system services.
string GenerateGUID();

// Returns true if the input string conforms to the GUID format.
bool IsValidGUID(const string &guid);

string RandomDataToGUIDString(const uint64_t bytes[2]);

// Store in *dst a random string of length "len" and return a Slice that
// references the generated data.
extern StringPiece RandomString(Random *rnd, int32_t len, string *dst);

// Return a random key with the specified length that may contain interesting
// characters (e.g. \x00, \xff, etc.).
extern string RandomKey(Random *rnd, int len);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_RANDOM_UTIL_H_
