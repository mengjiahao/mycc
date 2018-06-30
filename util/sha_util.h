
#ifndef MYCC_UTIL_SHA_UTIL_H_
#define MYCC_UTIL_SHA_UTIL_H_

#include <stdlib.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

/** A hasher class for SHA1. */
class CSHA1
{
private:
  uint32_t s[5];
  unsigned char buf[64];
  uint64_t bytes;

public:
  static const uint64_t OUTPUT_SIZE = 20;

  CSHA1();
  CSHA1 &Write(const unsigned char *data, uint64_t len);
  void Finalize(unsigned char hash[OUTPUT_SIZE]);
  CSHA1 &Reset();
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_SHA_UTIL_H_