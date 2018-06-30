
#include "stringpiece.h"
#include <stdio.h>
#include <algorithm>
#include <string>
#include "hash_util.h"
#include "locks_util.h"
#include "math_util.h"

namespace mycc
{
namespace util
{

// 2 small internal utility functions, for efficient hex conversions
// and no need for snprintf, toupper etc...
// Originally from wdt/util/EncryptionUtils.cpp - for ToString(true)/DecodeHex:
static char toHex(unsigned char v)
{
  if (v <= 9)
  {
    return '0' + v;
  }
  return 'A' + v - 10;
}
// most of the code is for validation/error check
static int32_t fromHex(char c)
{
  // toupper:
  if (c >= 'a' && c <= 'f')
  {
    c -= ('a' - 'A'); // aka 0x20
  }
  // validation
  if (c < '0' || (c > '9' && (c < 'A' || c > 'F')))
  {
    return -1; // invalid not 0-9A-F hex char
  }
  if (c <= '9')
  {
    return c - '0';
  }
  return c - 'A' + 10;
}

const uint64_t StringPiece::npos = static_cast<uint64_t>(-1);

uint64_t StringPiece::find(char c, uint64_t pos) const
{
  if (pos >= size_)
  {
    return npos;
  }
  const char *result =
      reinterpret_cast<const char *>(memchr(data_ + pos, c, size_ - pos));
  return result != nullptr ? result - data_ : npos;
}

// Search range is [0..pos] inclusive.  If pos == npos, search everything.
uint64_t StringPiece::rfind(char c, uint64_t pos) const
{
  if (size_ == 0)
    return npos;
  for (const char *p = data_ + MATH_MIN(pos, size_ - 1); p >= data_; p--)
  {
    if (*p == c)
    {
      return p - data_;
    }
  }
  return npos;
}

bool StringPiece::contains(StringPiece s) const
{
  return std::search(begin(), end(), s.begin(), s.end()) != end();
}

StringPiece StringPiece::substr(uint64_t pos, uint64_t n) const
{
  if (pos > size_)
    pos = size_;
  if (n > size_ - pos)
    n = size_ - pos;
  return StringPiece(data_ + pos, n);
}

int32_t StringPiece::compare(StringPiece b) const
{
  const uint64_t min_len = (size_ < b.size_) ? size_ : b.size_;
  int32_t r = memcmp(data_, b.data_, min_len);
  if (r == 0)
  {
    if (size_ < b.size_)
      r = -1;
    else if (size_ > b.size_)
      r = +1;
  }
  return r;
}

// Return a string that contains the copy of the referenced data.
string StringPiece::toString(bool hex) const
{
  string result;
  if (hex)
  {
    result.reserve(2 * size_);
    for (uint64_t i = 0; i < size_; ++i)
    {
      unsigned char c = data_[i];
      result.push_back(toHex(c >> 4));
      result.push_back(toHex(c & 0xf));
    }
    return result;
  }
  else
  {
    result.assign(data_, size_);
    return result;
  }
}

bool StringPiece::decodeHex(string *result) const
{
  uint64_t len = size_;
  if (len % 2)
  {
    // Hex string must be even number of hex digits to get complete bytes back
    return false;
  }
  if (!result)
  {
    return false;
  }
  result->clear();
  result->reserve(len / 2);

  for (uint64_t i = 0; i < len;)
  {
    int32_t h1 = fromHex(data_[i++]);
    if (h1 < 0)
    {
      return false;
    }
    int32_t h2 = fromHex(data_[i++]);
    if (h2 < 0)
    {
      return false;
    }
    result->push_back((h1 << 4) | h2);
  }
  return true;
}

// Compare two StringPieces and returns the first byte where they differ
uint64_t StringPiece::difference_offset(const StringPiece b) const
{
  uint64_t off = 0;
  const uint64_t len = (size_ < b.size_) ? size_ : b.size_;
  for (; off < len; off++)
  {
    if (data_[off] != b.data_[off])
      break;
  }
  return off;
}

std::uint64_t StringPiece::Hasher::operator()(StringPiece s) const
{
  return (std::uint64_t)Hash64(s.data(), s.size(), 0xDECAFCAFFE);
}

namespace
{ // namespace anonymous

class BytewiseStringPieceComparatorImpl : public StringPieceComparator
{
public:
  BytewiseStringPieceComparatorImpl() {}

  virtual const char *Name() const
  {
    return "leveldb.BytewiseStringPieceComparator";
  }

  virtual int32_t Compare(const StringPiece &a, const StringPiece &b) const
  {
    return a.compare(b);
  }

  virtual void FindShortestSeparator(
      string *start,
      const StringPiece &limit) const
  {
    // Find length of common prefix
    uint64_t min_length = std::min(start->size(), limit.size());
    uint64_t diff_index = 0;
    while ((diff_index < min_length) &&
           ((*start)[diff_index] == limit[diff_index]))
    {
      diff_index++;
    }

    if (diff_index >= min_length)
    {
      // Do not shorten if one string is a prefix of the other
    }
    else
    {
      uint8_t diff_byte = static_cast<uint8_t>((*start)[diff_index]);
      if (diff_byte < static_cast<uint8_t>(0xff) &&
          diff_byte + 1 < static_cast<uint8_t>(limit[diff_index]))
      {
        (*start)[diff_index]++;
        start->resize(diff_index + 1);
        assert(Compare(*start, limit) < 0);
      }
    }
  }

  virtual void FindShortSuccessor(string *key) const
  {
    // Find first character that can be incremented
    uint64_t n = key->size();
    for (uint64_t i = 0; i < n; i++)
    {
      const uint8_t byte = (*key)[i];
      if (byte != static_cast<uint8_t>(0xff))
      {
        (*key)[i] = byte + 1;
        key->resize(i + 1);
        return;
      }
    }
    // *key is a run of 0xffs.  Leave it alone.
  }
};

} // namespace

static OnceType once = {};
static const StringPieceComparator *bytewise;

static void InitModule()
{
  bytewise = new BytewiseStringPieceComparatorImpl;
}

const StringPieceComparator *BytewiseStringPieceComparator()
{
  InitOnce(&once, InitModule);
  return bytewise;
}

} // namespace util
} // namespace mycc