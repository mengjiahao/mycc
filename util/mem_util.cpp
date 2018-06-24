
#include "mem_util.h"
#include <malloc.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "mem_util.h"

#ifdef USE_JEMALLOC
#include "jemalloc/jemalloc.h"
#endif

namespace mycc
{
namespace util
{

void *AlignedMalloc(uint64_t size, int32_t minimum_alignment)
{
  void *ptr = nullptr;
  // posix_memalign requires that the requested alignment be at least
  // sizeof(void*). In this case, fall back on malloc which should return
  // memory aligned to at least the size of a pointer.
  const int required_alignment = sizeof(void *);
  if (minimum_alignment < required_alignment)
    return Malloc(size);

#if USE_JEMALLOC
  int err = je_posix_memalign(&ptr, minimum_alignment, size);
#else
  int err = ::posix_memalign(&ptr, minimum_alignment, size);
#endif

  if (err != 0)
  {
    return nullptr;
  }
  else
  {
    return ptr;
  }
}

void AlignedFree(void *aligned_memory) { Free(aligned_memory); }

void *Malloc(uint64_t size)
{
#if USE_JEMALLOC
  return je_malloc(size);
#else
  return ::malloc(size);
#endif
}

void *Realloc(void *ptr, uint64_t size)
{
#if USE_JEMALLOC
  return je_realloc(ptr, size);
#else
  return ::realloc(ptr, size);
#endif
}

void Free(void *ptr)
{
#if USE_JEMALLOC
  je_free(ptr);
#else
  ::free(ptr);
#endif
}

} // namespace util
} // namespace mycc