
#ifndef MYCC_UTIL_ARENA_H_
#define MYCC_UTIL_ARENA_H_

#include <assert.h>
#include <vector>
#include "allocator.h"
#include "locks_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class ArenaEasy
{
public:
  static const int32_t kBlockSize = 4096;

  ArenaEasy();
  ~ArenaEasy();

  // Return a pointer to a newly allocated memory block of "bytes" bytes.
  char *Allocate(uint64_t bytes);

  // Allocate memory with the normal alignment guarantees provided by malloc
  char *AllocateAligned(uint64_t bytes);

  // Returns an estimate of the total memory usage of data allocated
  // by the arena.
  uint64_t MemoryUsage() const
  {
    return reinterpret_cast<uint64_t>(memory_usage_.NoBarrier_Load());
  }

private:
  char *AllocateFallback(uint64_t bytes);
  char *AllocateNewBlock(uint64_t block_bytes);

  // Allocation state
  char *alloc_ptr_;
  uint64_t alloc_bytes_remaining_;

  // Array of new[] allocated memory blocks
  std::vector<char *> blocks_;

  // Total memory usage of the arena.
  AtomicPointer memory_usage_;

  // No copying allowed
  DISALLOW_COPY_AND_ASSIGN(ArenaEasy);
};

inline char *ArenaEasy::Allocate(uint64_t bytes)
{
  // The semantics of what to return are a bit messy if we allow
  // 0-byte allocations, so we disallow them here (we don't need
  // them for our internal use).
  assert(bytes > 0);
  if (bytes <= alloc_bytes_remaining_)
  {
    char *result = alloc_ptr_;
    alloc_ptr_ += bytes;
    alloc_bytes_remaining_ -= bytes;
    return result;
  }
  return AllocateFallback(bytes);
}

// This class is "thread-compatible": different threads can access the
// arena at the same time without locking, as long as they use only
// const methods.
class Arena : public Allocator
{
public:
  // Allocates a thread-compatible arena with the specified block size.
  explicit Arena(const uint64_t block_size = kMinBlockSize);
  virtual ~Arena();

  static const uint64_t kInlineSize = 2048;
  static const uint64_t kMinBlockSize;
  static const uint64_t kMaxBlockSize;

  virtual char *Allocate(const uint64_t size) override
  {
    return reinterpret_cast<char *>(GetMemory(size, 1));
  }

  virtual char *AllocateAligned(const uint64_t size, const uint64_t alignment) override
  {
    return reinterpret_cast<char *>(GetMemory(size, alignment));
  }

  virtual uint64_t BlockSize() const override { return block_size_; }

  void Reset();

protected:
  bool SatisfyAlignment(const uint64_t alignment);
  void MakeNewBlock(const uint32_t alignment);
  void *GetMemoryFallback(const uint64_t size, const int align);
  void *GetMemory(const uint64_t size, const int align)
  {
    assert(remaining_ <= block_size_); // an invariant
    if (size > 0 && size < remaining_ && align == 1)
    { // common case
      void *result = freestart_;
      freestart_ += size;
      remaining_ -= size;
      return result;
    }
    return GetMemoryFallback(size, align);
  }

  uint64_t remaining_;

private:
  struct AllocatedBlock
  {
    char *mem;
    uint64_t size;
  };

  // Allocate new block of at least block_size, with the specified
  // alignment.
  // The returned AllocatedBlock* is valid until the next call to AllocNewBlock
  // or Reset (i.e. anything that might affect overflow_blocks_).
  AllocatedBlock *AllocNewBlock(const uint64_t block_size,
                                const uint32_t alignment);

  const uint64_t block_size_;
  char *freestart_;            // beginning of the free space in most recent block
  char *freestart_when_empty_; // beginning of the free space when we're empty
  // STL vector isn't as efficient as it could be, so we use an array at first
  uint64_t blocks_alloced_;         // how many of the first_blocks_ have been alloced
  AllocatedBlock first_blocks_[16]; // the length of this array is arbitrary
  // if the first_blocks_ aren't enough, expand into overflow_blocks_.
  std::vector<AllocatedBlock> *overflow_blocks_;

  void FreeBlocks(); // Frees all except first block

  DISALLOW_COPY_AND_ASSIGN(Arena);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ARENA_H_