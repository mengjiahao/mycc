
#ifndef MYCC_UTIL_ALLOCATOR_H_
#define MYCC_UTIL_ALLOCATOR_H_

#include <stdlib.h>
#include <memory>
#include "singleton.h"
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

// MemoryPieceStore is used to cache small objects, so that we can save time for new/delete
// it is not thread-safe

class MemoryPieceStore : public SingletonStaticBase<MemoryPieceStore>
{
public:
  friend class SingletonStaticBase<MemoryPieceStore>;
  static const int32_t kMaxCacheByte = 128; // most objects should be less than 128B
  static const int32_t kMaxCacheCount = 16; // only cache 16 of them

  void *Allocate(uint64_t sz)
  {
    m_allocate_count++;
    if (sz <= static_cast<uint64_t>(kMaxCacheByte) && m_cached_count[sz] > 0)
    {
      int32_t count = m_cached_count[sz];
      char *ptr = m_cached_objects[sz][0];
      m_cached_objects[sz][0] = m_cached_objects[sz][count - 1];
      m_cached_count[sz]--;
      return ptr;
    }
    return malloc(sz);
  }

  void Release(void *ptr, uint64_t sz)
  {
    if (ptr == NULL)
    {
      return;
    }
    m_release_count++;
    if (static_cast<char *>(ptr) < m_buffer ||
        static_cast<char *>(ptr) >= (m_buffer + m_length))
    {
      free(ptr);
      return;
    }

    assert(sz > 0U && sz <= static_cast<uint64_t>(kMaxCacheByte) && m_cached_count[sz] < kMaxCacheCount);
    int32_t count = m_cached_count[sz];
    m_cached_objects[sz][count] = static_cast<char *>(ptr);
    m_cached_count[sz]++;
  }

  MemoryPieceStore() : m_allocate_count(0), m_release_count(0), m_length(0), m_buffer(NULL)
  {
    for (int32_t i = 1; i <= kMaxCacheByte; ++i)
    {
      m_length += i * kMaxCacheCount;
    }
    //PRINT_INFO("total allocate cache size: %d\n", m_length);

    m_buffer = new char[m_length];
    int32_t use_len = 0;
    for (int32_t i = 1; i <= kMaxCacheByte; ++i)
    {
      for (int32_t j = 0; j < kMaxCacheCount; ++j)
      {
        m_cached_objects[i][j] = m_buffer + use_len;
        use_len += i;
      }
      m_cached_count[i] = kMaxCacheCount;
    }
    assert(use_len == m_length);
  }

  ~MemoryPieceStore()
  {
    delete[] m_buffer;
  }

  int64_t allocate_count() const { return m_allocate_count; }
  int64_t release_count() const { return m_release_count; }

private:
  int64_t m_allocate_count;
  int64_t m_release_count;
  int m_cached_count[kMaxCacheByte + 1];
  char *m_cached_objects[kMaxCacheByte + 1][kMaxCacheCount];
  int m_length;
  char *m_buffer;
};

template <class T>
class MemoryPieceAllocator
{
public:
  void *operator new(size_t sz);
  void operator delete(void *p, size_t sz);
  virtual ~MemoryPieceAllocator() {}
};

template <class T>
void *MemoryPieceAllocator<T>::operator new(size_t sz)
{
  assert(sizeof(T) == sz);
  return MemoryPieceStore::Instance()->Allocate(sz);
}

template <class T>
void MemoryPieceAllocator<T>::operator delete(void *p, size_t sz)
{
  assert(sizeof(T) == sz);
  if (p == NULL)
    return;
  return MemoryPieceStore::Instance()->Release(p, sz);
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ALLOCATOR_H_