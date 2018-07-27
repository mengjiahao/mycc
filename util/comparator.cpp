
#include <stdint.h>
#include <algorithm>
#include <memory>
#include "comparator.h"

namespace mycc
{
namespace util
{

Comparator::~Comparator() {}

namespace
{

class BytewiseComparatorImpl : public Comparator
{
public:
  BytewiseComparatorImpl() {}

  virtual const char *Name() const override
  {
    return "BytewiseComparator";
  }

  virtual int Compare(const StringPiece &a, const StringPiece &b) const override
  {
    return a.compare(b);
  }

  virtual bool Equal(const StringPiece &a, const StringPiece &b) const override
  {
    return a == b;
  }

  virtual void FindShortestSeparator(string *start,
                                     const StringPiece &limit) const override
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
      uint8_t start_byte = static_cast<uint8_t>((*start)[diff_index]);
      uint8_t limit_byte = static_cast<uint8_t>(limit[diff_index]);
      if (start_byte >= limit_byte)
      {
        // Cannot shorten since limit is smaller than start or start is
        // already the shortest possible.
        return;
      }
      assert(start_byte < limit_byte);

      if (diff_index < limit.size() - 1 || start_byte + 1 < limit_byte)
      {
        (*start)[diff_index]++;
        start->resize(diff_index + 1);
      }
      else
      {
        //     v
        // A A 1 A A A
        // A A 2
        //
        // Incrementing the current byte will make start bigger than limit, we
        // will skip this byte, and find the first non 0xFF byte in start and
        // increment it.
        diff_index++;

        while (diff_index < start->size())
        {
          // Keep moving until we find the first non 0xFF byte to
          // increment it
          if (static_cast<uint8_t>((*start)[diff_index]) <
              static_cast<uint8_t>(0xff))
          {
            (*start)[diff_index]++;
            start->resize(diff_index + 1);
            break;
          }
          diff_index++;
        }
      }
      assert(Compare(*start, limit) < 0);
    }
  }

  virtual void FindShortSuccessor(string *key) const override
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

class ReverseBytewiseComparatorImpl : public BytewiseComparatorImpl
{
public:
  ReverseBytewiseComparatorImpl() {}

  virtual const char *Name() const override
  {
    return "ReverseBytewiseComparator";
  }

  virtual int Compare(const StringPiece &a, const StringPiece &b) const override
  {
    return -a.compare(b);
  }
};

} // namespace

const Comparator *BytewiseComparator()
{
  static BytewiseComparatorImpl bytewise;
  return &bytewise;
}

const Comparator *ReverseBytewiseComparator()
{
  static ReverseBytewiseComparatorImpl rbytewise;
  return &rbytewise;
}

} // namespace util
} // namespace mycc