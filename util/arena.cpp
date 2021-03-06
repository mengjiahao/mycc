
#include "arena.h"
#include <assert.h>
#include <algorithm>
#include <vector>
#include "mem_util.h"

namespace mycc
{
namespace util
{

ArenaEasy::ArenaEasy() : memory_usage_(0)
{
  alloc_ptr_ = nullptr; // First allocation will allocate a block
  alloc_bytes_remaining_ = 0;
}

ArenaEasy::~ArenaEasy()
{
  for (uint64_t i = 0; i < blocks_.size(); i++)
  {
    delete[] blocks_[i];
  }
}

char *ArenaEasy::AllocateFallback(uint64_t bytes)
{
  if (bytes > kBlockSize / 4)
  {
    // Object is more than a quarter of our block size.  Allocate it separately
    // to avoid wasting too much space in leftover bytes.
    char *result = AllocateNewBlock(bytes);
    return result;
  }

  // We waste the remaining space in the current block.
  alloc_ptr_ = AllocateNewBlock(kBlockSize);
  alloc_bytes_remaining_ = kBlockSize;

  char *result = alloc_ptr_;
  alloc_ptr_ += bytes;
  alloc_bytes_remaining_ -= bytes;
  return result;
}

char *ArenaEasy::AllocateAligned(uint64_t bytes)
{
  const int align = (sizeof(void *) > 8) ? sizeof(void *) : 8;
  assert((align & (align - 1)) == 0); // Pointer size should be a power of 2
  uint64_t current_mod = reinterpret_cast<uint64_t>(alloc_ptr_) & (align - 1);
  uint64_t slop = (current_mod == 0 ? 0 : align - current_mod);
  uint64_t needed = bytes + slop;
  char *result;
  if (needed <= alloc_bytes_remaining_)
  {
    result = alloc_ptr_ + slop;
    alloc_ptr_ += needed;
    alloc_bytes_remaining_ -= needed;
  }
  else
  {
    // AllocateFallback always returned aligned memory
    result = AllocateFallback(bytes);
  }
  assert((reinterpret_cast<uintptr_t>(result) & (align - 1)) == 0);
  return result;
}

char *ArenaEasy::AllocateNewBlock(uint64_t block_bytes)
{
  char *result = new char[block_bytes];
  blocks_.push_back(result);
  memory_usage_.NoBarrier_Store(
      reinterpret_cast<void *>(MemoryUsage() + block_bytes + sizeof(char *)));
  return result;
}

// This approach to arenas overcomes many of the limitations described
// in the "Specialized allocators" section of
//     http://www.pdos.lcs.mit.edu/~dm/c++-new.html
//
// A somewhat similar approach to Gladiator, but for heap-detection, was
// suggested by Ron van der Wal and Scott Meyers at
//     http://www.aristeia.com/BookErrata/M27Comments_frames.html

const uint64_t Arena::kMinBlockSize = 1024;
const uint64_t Arena::kMaxBlockSize = 2u << 30;
static const int kAlignUnit = sizeof(void *);

uint64_t OptimizeBlockSize(uint64_t block_size)
{
  // Make sure block_size is in optimal range
  block_size = std::max(Arena::kMinBlockSize, block_size);
  block_size = std::min(Arena::kMaxBlockSize, block_size);

  // make sure block_size is the multiple of kAlignUnit
  if (block_size % kAlignUnit != 0)
  {
    block_size = (1 + block_size / kAlignUnit) * kAlignUnit;
  }

  return block_size;
}

// ----------------------------------------------------------------------
// Arena::Arena()
// Arena::~Arena()
//    Destroying the arena automatically calls Reset()
// ----------------------------------------------------------------------

Arena::Arena(const uint64_t block_size)
    : remaining_(0),
      block_size_(block_size),
      freestart_(nullptr), // set for real in Reset()
      blocks_alloced_(1),
      overflow_blocks_(nullptr)
{
  assert(block_size > kDefaultAlignment);

  first_blocks_[0].mem =
      reinterpret_cast<char *>(AlignedMalloc(block_size_, sizeof(void *)));

  first_blocks_[0].size = block_size_;

  Reset();
}

Arena::~Arena()
{
  FreeBlocks();
  assert(overflow_blocks_ == nullptr); // FreeBlocks() should do that
  // The first X blocks stay allocated always by default.  Delete them now.
  for (uint64_t i = 0; i < blocks_alloced_; ++i)
  {
    AlignedFree(first_blocks_[i].mem);
  }
}

// Returns true iff it advances freestart_ to the first position
// satisfying alignment without exhausting the current block.
bool Arena::SatisfyAlignment(uint64_t alignment)
{
  const uint64_t overage = reinterpret_cast<uint64_t>(freestart_) & (alignment - 1);
  if (overage > 0)
  {
    const uint64_t waste = alignment - overage;
    if (waste >= remaining_)
    {
      return false;
    }
    freestart_ += waste;
    remaining_ -= waste;
  }
  //DCHECK_EQ(uint64_t{0}, reinterpret_cast<uint64_t>(freestart_) & (alignment - 1));
  return true;
}

// ----------------------------------------------------------------------
// Arena::Reset()
//    Clears all the memory an arena is using.
// ----------------------------------------------------------------------

void Arena::Reset()
{
  FreeBlocks();
  freestart_ = first_blocks_[0].mem;
  remaining_ = first_blocks_[0].size;

  // There is no guarantee the first block is properly aligned, so
  // enforce that now.
  // CHECK(SatisfyAlignment(kDefaultAlignment));

  freestart_when_empty_ = freestart_;
}

// ----------------------------------------------------------------------
// Arena::MakeNewBlock()
//    Our sbrk() equivalent.  We always make blocks of the same size
//    (though GetMemory() can also make a new block for really big
//    data.
// ----------------------------------------------------------------------

void Arena::MakeNewBlock(const uint32_t alignment)
{
  AllocatedBlock *block = AllocNewBlock(block_size_, alignment);
  freestart_ = block->mem;
  remaining_ = block->size;
  //CHECK(SatisfyAlignment(alignment));
}

// The following simple numeric routines also exist in util/math/mathutil.h
// but we don't want to depend on that library.

// Euclid's algorithm for Greatest Common Denominator.
static uint32_t GCD(uint32_t x, uint32_t y)
{
  while (y != 0)
  {
    uint32_t r = x % y;
    x = y;
    y = r;
  }
  return x;
}

static uint32_t LeastCommonMultiple(uint32_t a, uint32_t b)
{
  if (a > b)
  {
    return (a / GCD(a, b)) * b;
  }
  else if (a < b)
  {
    return (b / GCD(b, a)) * a;
  }
  else
  {
    return a;
  }
}

// -------------------------------------------------------------
// Arena::AllocNewBlock()
//    Adds and returns an AllocatedBlock.
//    The returned AllocatedBlock* is valid until the next call
//    to AllocNewBlock or Reset.  (i.e. anything that might
//    affect overflow_blocks_).
// -------------------------------------------------------------

Arena::AllocatedBlock *Arena::AllocNewBlock(const uint64_t block_size,
                                            const uint32_t alignment)
{
  AllocatedBlock *block;
  // Find the next block.
  if (blocks_alloced_ < ARRAYSIZE_UNSAFE(first_blocks_))
  {
    // Use one of the pre-allocated blocks
    block = &first_blocks_[blocks_alloced_++];
  }
  else
  { // oops, out of space, move to the vector
    if (overflow_blocks_ == nullptr)
      overflow_blocks_ = new std::vector<AllocatedBlock>;
    // Adds another block to the vector.
    overflow_blocks_->resize(overflow_blocks_->size() + 1);
    // block points to the last block of the vector.
    block = &overflow_blocks_->back();
  }

  // NOTE(tucker): this utility is made slightly more complex by
  // not disallowing the case where alignment > block_size.
  // Can we, without breaking existing code?

  // Must be a multiple of kDefaultAlignment, unless requested
  // alignment is 1, in which case we don't care at all.
  uint32_t adjusted_alignment =
      (alignment > 1 ? LeastCommonMultiple(alignment, kDefaultAlignment) : 1);
  // Required minimum alignment for AlignedMalloc().
  adjusted_alignment =
      std::max(adjusted_alignment, static_cast<uint32_t>(sizeof(void *)));

  //CHECK_LE(adjusted_alignment, static_cast<uint32>(1 << 20))
  //    << "Alignment on boundaries greater than 1MB not supported.";

  // If block_size > alignment we force block_size to be a multiple
  // of alignment; if block_size < alignment we make no adjustment.
  uint64_t adjusted_block_size = block_size;
  if (adjusted_block_size > adjusted_alignment)
  {
    const uint32_t excess = adjusted_block_size % adjusted_alignment;
    adjusted_block_size += (excess > 0 ? adjusted_alignment - excess : 0);
  }
  block->mem = reinterpret_cast<char *>(
      AlignedMalloc(adjusted_block_size, adjusted_alignment));
  block->size = adjusted_block_size;
  //CHECK(nullptr != block->mem) << "block_size=" << block_size
  //                             << " adjusted_block_size=" << adjusted_block_size
  //                             << " alignment=" << alignment
  //                             << " adjusted_alignment=" << adjusted_alignment;

  return block;
}

// ----------------------------------------------------------------------
// Arena::GetMemoryFallback()
//    We take memory out of our pool, aligned on the byte boundary
//    requested.  If we don't have space in our current pool, we
//    allocate a new block (wasting the remaining space in the
//    current block) and give you that.  If your memory needs are
//    too big for a single block, we make a special your-memory-only
//    allocation -- this is equivalent to not using the arena at all.
// ----------------------------------------------------------------------

void *Arena::GetMemoryFallback(const uint64_t size, const int alignment)
{
  if (0 == size)
  {
    return nullptr; // stl/stl_alloc.h says this is okay
  }

  // alignment must be a positive power of 2.
  //CHECK(alignment > 0 && 0 == (alignment & (alignment - 1)));

  // If the object is more than a quarter of the block size, allocate
  // it separately to avoid wasting too much space in leftover bytes.
  if (block_size_ == 0 || size > block_size_ / 4)
  {
    return AllocNewBlock(size, alignment)->mem;
  }

  // Enforce alignment on freestart_ then check for adequate space,
  // which may require starting a new block.
  if (!SatisfyAlignment(alignment) || size > remaining_)
  {
    MakeNewBlock(alignment);
  }
  //CHECK_LE(size, remaining_);

  remaining_ -= size;
  void *result = freestart_;
  freestart_ += size;

  return result;
}

// ----------------------------------------------------------------------
// Arena::ReturnMemoryFallback()
// Arena::FreeBlocks()
//    Unlike GetMemory(), which does actual work, ReturnMemory() is a
//    no-op: we don't "free" memory until Reset() is called.  We do
//    update some stats, though.  Note we do no checking that the
//    pointer you pass in was actually allocated by us, or that it
//    was allocated for the size you say, so be careful here!
//       FreeBlocks() does the work for Reset(), actually freeing all
//    memory allocated in one fell swoop.
// ----------------------------------------------------------------------

void Arena::FreeBlocks()
{
  for (uint64_t i = 1; i < blocks_alloced_; ++i)
  { // keep first block alloced
    AlignedFree(first_blocks_[i].mem);
    first_blocks_[i].mem = nullptr;
    first_blocks_[i].size = 0;
  }
  blocks_alloced_ = 1;
  if (overflow_blocks_ != nullptr)
  {
    std::vector<AllocatedBlock>::iterator it;
    for (it = overflow_blocks_->begin(); it != overflow_blocks_->end(); ++it)
    {
      AlignedFree(it->mem);
    }
    delete overflow_blocks_; // These should be used very rarely
    overflow_blocks_ = nullptr;
  }
}

} // namespace util
} // namespace mycc