
#ifndef MYCC_UTIL_ALLOCATOR_H_
#define MYCC_UTIL_ALLOCATOR_H_

#include <stdlib.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

// Abstract interface for allocating memory in blocks. This memory is freed
// when the allocator object is destroyed. See the Arena class for more info.

class Allocator
{
public:
  virtual ~Allocator() {}

  virtual char *Allocate(const uint64_t size) = 0;
  virtual char *AllocateAligned(const uint64_t size, const uint64_t alignment = kDefaultAlignment) = 0;

  virtual uint64_t BlockSize() const = 0;

// This should be the worst-case alignment for any type.  This is
// good for IA-32, SPARC version 7 (the last one I know), and
// supposedly Alpha.  i386 would be more time-efficient with a
// default alignment of 8, but ::operator new() uses alignment of 4,
// and an assertion will fail below after the call to MakeNewBlock()
// if you try to use a larger alignment.
#ifdef __i386__
  static const int32_t kDefaultAlignment = 4;
#else
  static const int32_t kDefaultAlignment = 8;
#endif
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ALLOCATOR_H_