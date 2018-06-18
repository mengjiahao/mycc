
#ifndef MYCC_UTIL_TYPES_UTIL_H_
#define MYCC_UTIL_TYPES_UTIL_H_

#include <endian.h>
#include <inttypes.h>
#include <limits.h> // So we can set the bounds of our types.
#include <stddef.h> // For size_t.
#include <stdint.h> // For intptr_t.
#include <limits>
#include <string>
#include "macros_util.h"

namespace mycc
{

#ifndef INT8_MAX
#define INT8_MAX 0x7f // (127)
#endif
#ifndef INT8_MIN
#define INT8_MIN (-INT8_MAX - 1) // (-128)
#endif
#ifndef UINT8_MAX
#define UINT8_MAX (INT8_MAX * 2 + 1) // (255)
#endif
#ifndef INT16_MAX
#define INT16_MAX 0x7fff // (32767)
#endif
#ifndef INT16_MIN
#define INT16_MIN (-INT16_MAX - 1) // (-32767-1)
#endif
#ifndef UINT16_MAX
#define UINT16_MAX 0xffff // (65535)
#endif
#ifndef INT32_MAX
#define INT32_MAX 0x7fffffffL // (2147483647)
#endif
#ifndef INT32_MIN
#define INT32_MIN (-INT32_MAX - 1L) // (-2147483647-1)
#endif
#ifndef UINT32_MAX
#define UINT32_MAX 0xffffffffUL // (4294967295U)
#endif
#ifndef INT64_MAX
#define INT64_MAX 0x7fffffffffffffffLL //(9223372036854775807)
#endif
#ifndef INT64_MIN
#define INT64_MIN (-INT64_MAX - 1LL) // (-9223372036854775807-1)
#endif
#ifndef UINT64_MAX
#define UINT64_MAX 0xffffffffffffffffULL // (18446744073709551615)
#endif

// KB, MB, GB to bytes
#define KBYTES (1024L)
#define MBYTES (1024L * 1024L)
#define GBYTES (1024L * 1024L * 1024)

typedef float float32;
typedef double float64;

static const uint8_t kuint8max = ((uint8_t)0xFF);
static const uint16_t kuint16max = ((uint16_t)0xFFFF);
static const uint32_t kuint32max = ((uint32_t)0xFFFFFFFF);
static const uint64_t kuint64max = ((uint64_t)0xFFFFFFFFFFFFFFFFull);
static const int8_t kint8min = ((int8_t)~0x7F);
static const int8_t kint8max = ((int8_t)0x7F);
static const int16_t kint16min = ((int16_t)~0x7FFF);
static const int16_t kint16max = ((int16_t)0x7FFF);
static const int32_t kint32min = ((int32_t)~0x7FFFFFFF);
static const int32_t kint32max = ((int32_t)0x7FFFFFFF);
static const int64_t kint64min = ((int64_t)~0x7FFFFFFFFFFFFFFFll);
static const int64_t kint64max = ((int64_t)0x7FFFFFFFFFFFFFFFll);

static const int32_t kMaxInt32 = std::numeric_limits<int32_t>::max();
static const uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();
static const int64_t kMaxInt64 = std::numeric_limits<int64_t>::max();
static const size_t kMaxSizet = std::numeric_limits<size_t>::max();
static const float kFloatMax = std::numeric_limits<float>::max();
static const float kFloatMin = std::numeric_limits<float>::min();

/* To avoid dividing by zero */
static const float kVerySmallNumber = 1e-15;
static const double kVerySmallNumberDouble = 1e-15;

using ::std::string;

/// platform portable

namespace port
{

constexpr bool kLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

} // namespace port

} // namespace mycc

#endif // MYCC_UTIL_TYPES_UTIL_H_