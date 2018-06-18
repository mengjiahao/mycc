
#ifndef MYCC_UTIL_CODING_UTIL_H_
#define MYCC_UTIL_CODING_UTIL_H_

#include <endian.h>
#include <stdint.h>
#include <string.h>
#include <string>
#include "macros_util.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

inline bool IsBigEndian()
{
#if __linux__
  return __BYTE_ORDER == __BIG_ENDIAN;
#elif defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_IA64) || defined(_M_X64)
  // known little architectures
  return false;
#else // unknown
  int32_t x = 1;
  return reinterpret_cast<unsigned char &>(x) == 0;
#endif
}

inline bool IsLittleEndian()
{
  return !IsBigEndian();
}

#if defined(OS_LINUX)
#include <arpa/inet.h>
#include <byteswap.h>

inline uint16_t ByteSwap(uint16_t x)
{
  return bswap_16(x);
}
inline uint32_t ByteSwap(uint32_t x) { return bswap_32(x); }
inline uint64_t ByteSwap(uint64_t x) { return bswap_64(x); }

#else
// Returns a value with all bytes in |x| swapped, i.e. reverses the endianness.
inline uint16_t ByteSwap(uint16_t x)
{
  return (x << 8) | (x >> 8);
}

inline uint32_t ByteSwap(uint32_t x)
{
  x = ((x & 0xff00ff00UL) >> 8) | ((x & 0x00ff00ffUL) << 8);
  return (x >> 16) | (x << 16);
}

inline uint64_t ByteSwap(uint64_t x)
{
  x = ((x & 0xff00ff00ff00ff00ULL) >> 8) | ((x & 0x00ff00ff00ff00ffULL) << 8);
  x = ((x & 0xffff0000ffff0000ULL) >> 16) | ((x & 0x0000ffff0000ffffULL) << 16);
  return (x >> 32) | (x << 32);
}
#endif // ByteSwap

// Converts the bytes in |x| from host order (endianness) to little endian, and
// returns the result.
inline uint16_t ByteSwapToLE16(uint16_t x)
{
  if (port::kLittleEndian)
    return x;
  else
    return ByteSwap(x);
}
inline uint32_t ByteSwapToLE32(uint32_t x)
{
  if (port::kLittleEndian)
    return x;
  else
    return ByteSwap(x);
}
inline uint64_t ByteSwapToLE64(uint64_t x)
{
  if (port::kLittleEndian)
    return x;
  else
    return ByteSwap(x);
}

// Converts the bytes in |x| from network to host order (endianness), and
// returns the result.
inline uint16_t NetToHost16(uint16_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}
inline uint32_t NetToHost32(uint32_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}
inline uint64_t NetToHost64(uint64_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}

// Converts the bytes in |x| from host to network order (endianness), and
// returns the result.
inline uint16_t HostToNet16(uint16_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}
inline uint32_t HostToNet32(uint32_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}
inline uint64_t HostToNet64(uint64_t x)
{
  if (port::kLittleEndian)
    return ByteSwap(x);
  else
    return x;
}

// Read an integer (signed or unsigned) from |buf| in Big Endian order.
// Note: this loop is unrolled with -O1 and above.
// NOTE(szym): glibc dns-canon.c and SpdyFrameBuilder use
// ntohs(*(uint16_t*)ptr) which is potentially unaligned.
// This would cause SIGBUS on ARMv5 or earlier and ARMv6-M.
template <typename T>
inline void ReadBigEndian(const char buf[], T *out)
{
  *out = buf[0];
  for (size_t i = 1; i < sizeof(T); ++i)
  {
    *out <<= 8;
    // Must cast to uint8_t to avoid clobbering by sign extension.
    *out |= static_cast<uint8_t>(buf[i]);
  }
}

// Write an integer (signed or unsigned) |val| to |buf| in Big Endian order.
// Note: this loop is unrolled with -O1 and above.
template <typename T>
inline void WriteBigEndian(char buf[], T val)
{
  for (size_t i = 0; i < sizeof(T); ++i)
  {
    buf[sizeof(T) - i - 1] = static_cast<char>(val & 0xFF);
    val >>= 8;
  }
}

// Specializations to make clang happy about the (dead code) shifts above.
template <>
inline void ReadBigEndian<uint8_t>(const char buf[], uint8_t *out)
{
  *out = buf[0];
}

template <>
inline void WriteBigEndian<uint8_t>(char buf[], uint8_t val)
{
  buf[0] = static_cast<char>(val);
}

// The maximum length of a varint in bytes for 64-bit.
const unsigned int kMaxVarint64Length = 10;

// Lower-level versions of Get... that read directly from a character buffer
// without any bounds checking.

inline void EncodeBigEndian(char *buf, uint64_t value)
{
  buf[0] = (value >> 56) & 0xff;
  buf[1] = (value >> 48) & 0xff;
  buf[2] = (value >> 40) & 0xff;
  buf[3] = (value >> 32) & 0xff;
  buf[4] = (value >> 24) & 0xff;
  buf[5] = (value >> 16) & 0xff;
  buf[6] = (value >> 8) & 0xff;
  buf[7] = value & 0xff;
}

inline void EncodeBigEndian(char *buf, uint32_t value)
{
  buf[0] = (value >> 24) & 0xff;
  buf[1] = (value >> 16) & 0xff;
  buf[2] = (value >> 8) & 0xff;
  buf[3] = value & 0xff;
}

inline uint64_t DecodeBigEndian64(const char *buf)
{
  return ((static_cast<uint64_t>(static_cast<unsigned char>(buf[0]))) << 56 | (static_cast<uint64_t>(static_cast<unsigned char>(buf[1])) << 48) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[2])) << 40) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[3])) << 32) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[4])) << 24) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[5])) << 16) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[6])) << 8) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[7]))));
}

inline uint32_t DecodeBigEndian32(const char *buf)
{
  return ((static_cast<uint64_t>(static_cast<unsigned char>(buf[0])) << 24) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[1])) << 16) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[2])) << 8) | (static_cast<uint64_t>(static_cast<unsigned char>(buf[3]))));
}

inline uint16_t DecodeFixed16(const char *ptr)
{
  if (port::kLittleEndian)
  {
    // Load the raw bytes
    uint16_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  }
  else
  {
    return ((static_cast<uint16_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint16_t>(static_cast<unsigned char>(ptr[1])) << 8));
  }
}

inline uint32_t DecodeFixed32(const char *ptr)
{
  if (port::kLittleEndian)
  {
    // Load the raw bytes
    uint32_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  }
  else
  {
    return ((static_cast<uint32_t>(static_cast<unsigned char>(ptr[0]))) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[1])) << 8) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[2])) << 16) |
            (static_cast<uint32_t>(static_cast<unsigned char>(ptr[3])) << 24));
  }
}

inline uint64_t DecodeFixed64(const char *ptr)
{
  if (port::kLittleEndian)
  {
    // Load the raw bytes
    uint64_t result;
    memcpy(&result, ptr, sizeof(result)); // gcc optimizes this to a plain load
    return result;
  }
  else
  {
    uint64_t lo = DecodeFixed32(ptr);
    uint64_t hi = DecodeFixed32(ptr + 4);
    return (hi << 32) | lo;
  }
}

// Lower-level versions of Encode... that write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
extern void EncodeFixed16(char *dst, uint16_t value);
extern void EncodeFixed32(char *dst, uint32_t value);
extern void EncodeFixed64(char *dst, uint64_t value);

extern char *EncodeVarint32(char *dst, uint32_t v);
extern char *EncodeVarint64(char *dst, uint64_t v);

// Lower-level versions of Put... that write directly into a character buffer
// REQUIRES: dst has enough space for the value being written
extern void PutFixed16(string *dst, uint16_t value);
extern void PutFixed32(string *dst, uint32_t value);
extern void PutFixed64(string *dst, uint64_t value);

extern void PutVarint32(string *dst, uint32_t value);
extern void PutVarint64(string *dst, uint64_t value);

extern void PutVarint32Varint32(string *dst, uint32_t value1,
                                uint32_t value2);
extern void PutVarint32Varint32Varint32(string *dst, uint32_t value1,
                                        uint32_t value2, uint32_t value3);
extern void PutVarint64Varint64(string *dst, uint64_t value1,
                                uint64_t value2);
extern void PutVarint32Varint64(string *dst, uint32_t value1,
                                uint64_t value2);
extern void PutVarint32Varint32Varint64(string *dst, uint32_t value1,
                                        uint32_t value2, uint64_t value3);

extern void PutLengthPrefixedString(string *dst, const string &value);

// Standard Get... routines parse a value from the beginning of a StringPiece
// and advance the slice past the parsed value.
extern void GetFixed32(string *dst, uint32_t *value);
extern void GetFixed64(string *dst, uint64_t *value);
extern bool GetFixed32(StringPiece *input, uint32_t *value);
extern bool GetFixed64(StringPiece *input, uint64_t *value);

extern bool GetVarint32(string *input, uint32_t *value);
extern bool GetVarint64(string *input, uint64_t *value);
extern bool GetVarint32(StringPiece *input, uint32_t *value);
extern bool GetVarint64(StringPiece *input, uint64_t *value);

extern bool GetLengthPrefixedStringPiece(StringPiece *input, StringPiece *result);
// This function assumes data is well-formed.
extern StringPiece GetLengthPrefixedStringPiece(const char *data);
extern StringPiece GetStringPieceUntil(StringPiece *slice, char delimiter);
extern bool GetLengthPrefixedString(string *input, string *result);

// Pointer-based variants of GetVarint...  These either store a value
// in *v and return a pointer just past the parsed value, or return
// nullptr on error.  These routines only look at bytes in the range
// [p..limit-1]
// Internal routine for use by fallback path of GetVarint32Ptr
extern const char *GetVarint32PtrFallback(const char *p, const char *limit,
                                          uint32_t *value);
extern const char *GetVarint32Ptr(const char *p, const char *limit, uint32_t *value);
extern const char *GetVarint64Ptr(const char *p, const char *limit, uint64_t *v);

// Returns the length of the varint32 or varint64 encoding of "v"
extern int32_t VarintLength(uint64_t v);

// Provide an interface for platform independent endianness transformation
extern uint64_t EndianTransform(uint64_t input, uint64_t size);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_CODING_UTIL_H_
