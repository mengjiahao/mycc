
#ifndef MYCC_UTIL_CRC32_UTIL_H_
#define MYCC_UTIL_CRC32_UTIL_H_

#include "types_util.h"

namespace mycc {
namespace util {

// Return the crc32c of concat(A, data[0,n-1]) where init_crc is the
// crc32c of some string A.  Extend() is often used to maintain the
// crc32c of a stream of data.
extern uint32_t Crc32Extend(uint32_t init_crc, const char* data, uint64_t n);

// Return the crc32c of data[0,n-1]
inline uint32_t Crc32Value(const char* data, uint64_t n) { return Crc32Extend(0, data, n); }

static const uint32_t kCrc32MaskDelta = 0xa282ead8ul;

// Return a masked representation of crc.
//
// Motivation: it is problematic to compute the CRC of a string that
// contains embedded CRCs.  Therefore we recommend that CRCs stored
// somewhere (e.g., in files) should be masked before being stored.
inline uint32_t Crc32Mask(uint32_t crc) {
  // Rotate right by 15 bits and add a constant.
  return ((crc >> 15) | (crc << 17)) + kCrc32MaskDelta;
}

// Return the crc whose masked representation is masked_crc.
inline uint32_t Crc32Unmask(uint32_t masked_crc) {
  uint32_t rot = masked_crc - kCrc32MaskDelta;
  return ((rot >> 17) | (rot << 15));
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CRC32_UTIL_H_