
#ifndef MYCC_UTIL_ALLOCATOR_H_
#define MYCC_UTIL_ALLOCATOR_H_

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

  virtual char *Alloc(const size_t size) = 0;
  virtual char *AllocAligned(const size_t size, const size_t alignment = kDefaultAlignment) = 0;

  virtual size_t BlockSize() const = 0;

// This should be the worst-case alignment for any type.  This is
// good for IA-32, SPARC version 7 (the last one I know), and
// supposedly Alpha.  i386 would be more time-efficient with a
// default alignment of 8, but ::operator new() uses alignment of 4,
// and an assertion will fail below after the call to MakeNewBlock()
// if you try to use a larger alignment.
#ifdef __i386__
  static const int kDefaultAlignment = 4;
#else
  static const int kDefaultAlignment = 8;
#endif
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ALLOCATOR_H_