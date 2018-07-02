
#ifndef MYCC_UTIL_ALLOCATOR_H_
#define MYCC_UTIL_ALLOCATOR_H_

#include <stdlib.h>
#include <algorithm>
#include <memory>
#include "singleton.h"
#include "threadlocal_util.h"
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

/*!
 * \brief A memory pool that allocate memory of fixed size and alignment.
 * \tparam size The size of each piece.
 * \tparam align The alignment requirement of the memory.
 */
template <uint64_t size, uint64_t align>
class MemoryPoolEasy
{
public:
  /*! \brief constructor */
  MemoryPoolEasy()
  {
    static_assert(align % alignof(LinkedList) == 0,
                  "alignment requirement failed.");
    curr_page_.reset(new Page());
  }
  /*! \brief allocate a new memory of size */
  inline void *allocate()
  {
    if (head_ != nullptr)
    {
      LinkedList *ret = head_;
      head_ = head_->next;
      return ret;
    }
    else
    {
      if (page_ptr_ < kPageSize)
      {
        return &(curr_page_->data[page_ptr_++]);
      }
      else
      {
        allocated_.push_back(std::move(curr_page_));
        curr_page_.reset(new Page());
        page_ptr_ = 1;
        return &(curr_page_->data[0]);
      }
    }
  }
  /*!
   * \brief deallocate a piece of memory
   * \param p The pointer to the memory to be de-allocated.
   */
  inline void deallocate(void *p)
  {
    LinkedList *ptr = static_cast<LinkedList *>(p);
    ptr->next = head_;
    head_ = ptr;
  }

private:
  // page size of each member
  static const int kPageSize = ((1 << 22) / size);
  // page to be requested.
  struct Page
  {
    typename std::aligned_storage<size, align>::type data[kPageSize];
  };
  // internal linked list structure.
  struct LinkedList
  {
    LinkedList *next{nullptr};
  };
  // head of free list
  LinkedList *head_{nullptr};
  // current free page
  std::unique_ptr<Page> curr_page_;
  // pointer to the current free page position.
  uint64_t page_ptr_{0};
  // allocated pages.
  std::vector<std::unique_ptr<Page>> allocated_;
};

// A single-threaded pool for very efficient allocations of same-sized items.
// Example:
//   SingleMemoryPool<16, 512> pool;
//   void* mem = pool.get();
//   pool.back(mem);

template <uint64_t ITEM_SIZE_IN,  // size of an item
          uint64_t BLOCK_SIZE_IN, // suggested size of a block
          uint64_t MIN_NITEM = 1> // minimum number of items in one block
class SingleMemoryPool
{
public:
  // Note: this is a union. The next pointer is set iff when spaces is free,
  // ok to be overlapped.
  union Node {
    Node *next;
    char spaces[ITEM_SIZE_IN];
  };
  struct Block
  {
    static const uint64_t INUSE_SIZE =
        BLOCK_SIZE_IN - sizeof(void *) - sizeof(uint64_t);
    static const uint64_t NITEM = (sizeof(Node) <= INUSE_SIZE ? (INUSE_SIZE / sizeof(Node)) : MIN_NITEM);
    uint64_t nalloc;
    Block *next;
    Node nodes[NITEM];
  };
  static const uint64_t BLOCK_SIZE = sizeof(Block);
  static const uint64_t NITEM = Block::NITEM;
  static const uint64_t ITEM_SIZE = ITEM_SIZE_IN;

  SingleMemoryPool() : _free_nodes(nullptr), _blocks(nullptr) {}
  ~SingleMemoryPool() { reset(); }

  void swap(SingleMemoryPool &other)
  {
    std::swap(_free_nodes, other._free_nodes);
    std::swap(_blocks, other._blocks);
  }

  // Get space of an item. The space is as long as ITEM_SIZE.
  // Returns nullptr on out of memory
  void *get()
  {
    if (_free_nodes)
    {
      void *spaces = _free_nodes->spaces;
      _free_nodes = _free_nodes->next;
      return spaces;
    }
    if (_blocks == nullptr || _blocks->nalloc >= Block::NITEM)
    {
      Block *new_block = (Block *)malloc(sizeof(Block));
      if (new_block == nullptr)
      {
        return nullptr;
      }
      new_block->nalloc = 0;
      new_block->next = _blocks;
      _blocks = new_block;
    }
    return _blocks->nodes[_blocks->nalloc++].spaces;
  }

  // Return a space allocated by get() before.
  // Do nothing for nullptr.
  void back(void *p)
  {
    if (nullptr != p)
    {
      Node *node = (Node *)((char *)p - offsetof(Node, spaces));
      node->next = _free_nodes;
      _free_nodes = node;
    }
  }

  // Remove all allocated spaces. Spaces that are not back()-ed yet become
  // invalid as well.
  void reset()
  {
    _free_nodes = nullptr;
    while (_blocks)
    {
      Block *next = _blocks->next;
      free(_blocks);
      _blocks = next;
    }
  }

  // Count number of allocated/free/actively-used items.
  // Notice that these functions walk through all free nodes or blocks and
  // are not O(1).
  uint64_t count_allocated() const
  {
    uint64_t n = 0;
    for (Block *p = _blocks; p; p = p->next)
    {
      n += p->nalloc;
    }
    return n;
  }
  uint64_t count_free() const
  {
    uint64_t n = 0;
    for (Node *p = _free_nodes; p; p = p->next, ++n)
    {
    }
    return n;
  }
  uint64_t count_active() const
  {
    return count_allocated() - count_free();
  }

private:
  Node *_free_nodes;
  Block *_blocks;

  DISALLOW_COPY_AND_ASSIGN(SingleMemoryPool);
};

// MemoryPieceStore is used to cache small objects, so that we can save time for new/delete
// it is not thread-safe

class MemoryPieceStore : public SingletonStaticBase<MemoryPieceStore>
{
public:
  friend class SingletonStaticT<MemoryPieceStore>;
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
  void *operator new(uint64_t sz);
  void operator delete(void *p, uint64_t sz);
  virtual ~MemoryPieceAllocator() {}
};

template <class T>
void *MemoryPieceAllocator<T>::operator new(uint64_t sz)
{
  assert(sizeof(T) == sz);
  return MemoryPieceStore::Instance()->Allocate(sz);
}

template <class T>
void MemoryPieceAllocator<T>::operator delete(void *p, uint64_t sz)
{
  assert(sizeof(T) == sz);
  if (p == NULL)
    return;
  return MemoryPieceStore::Instance()->Release(p, sz);
}

/*!
 * \brief A thread local allocator that get memory from a threadlocal memory pool.
 * This is suitable to allocate objects that do not cross thread.
 * \tparam T the type of the data to be allocated.
 */
template <typename T>
class ThreadlocalAllocator
{
public:
  /*! \brief pointer type */
  typedef T *pointer;
  /*! \brief const pointer type */
  typedef const T *const_ptr;
  /*! \brief value type */
  typedef T value_type;
  /*! \brief default constructor */
  ThreadlocalAllocator() {}
  /*!
   * \brief constructor from another allocator
   * \param other another allocator
   * \tparam U another type
   */
  template <typename U>
  ThreadlocalAllocator(const ThreadlocalAllocator<U> &other) {}
  /*!
   * \brief allocate memory
   * \param n number of blocks
   * \return an uninitialized memory of type T.
   */
  inline T *allocate(uint64_t n)
  {
    assert(n == 1);
    typedef ThreadLocalStore<MemoryPoolEasy<sizeof(T), alignof(T)>> Store;
    return static_cast<T *>(Store::Get()->allocate());
  }
  /*!
   * \brief deallocate memory
   * \param p a memory to be returned.
   * \param n number of blocks
   */
  inline void deallocate(T *p, uint64_t n)
  {
    assert(n == 1);
    typedef ThreadLocalStore<MemoryPoolEasy<sizeof(T), alignof(T)>> Store;
    Store::Get()->deallocate(p);
  }
};

/*!
 * \brief a shared pointer like type that allocate object
 *   from a threadlocal object pool. This object is not thread-safe
 *   but can be faster than shared_ptr in certain usecases.
 * \tparam T the data type.
 */
template <typename T>
struct ThreadlocalAllocSharedPtr
{
public:
  /*! \brief default constructor */
  ThreadlocalAllocSharedPtr() : block_(nullptr) {}
  /*!
   * \brief constructor from nullptr
   * \param other the nullptr type
   */
  ThreadlocalAllocSharedPtr(std::nullptr_t other) : block_(nullptr) {} // NOLINT(*)
  /*!
   * \brief copy constructor
   * \param other another pointer.
   */
  ThreadlocalAllocSharedPtr(const ThreadlocalAllocSharedPtr<T> &other)
      : block_(other.block_)
  {
    IncRef(block_);
  }
  /*!
   * \brief move constructor
   * \param other another pointer.
   */
  ThreadlocalAllocSharedPtr(ThreadlocalAllocSharedPtr<T> &&other)
      : block_(other.block_)
  {
    other.block_ = nullptr;
  }
  /*!
   * \brief destructor
   */
  ~ThreadlocalAllocSharedPtr()
  {
    DecRef(block_);
  }
  /*!
   * \brief move assignment
   * \param other another object to be assigned.
   * \return self.
   */
  inline ThreadlocalAllocSharedPtr<T> &operator=(ThreadlocalAllocSharedPtr<T> &&other)
  {
    DecRef(block_);
    block_ = other.block_;
    other.block_ = nullptr;
    return *this;
  }
  /*!
   * \brief copy assignment
   * \param other another object to be assigned.
   * \return self.
   */
  inline ThreadlocalAllocSharedPtr<T> &operator=(const ThreadlocalAllocSharedPtr<T> &other)
  {
    DecRef(block_);
    block_ = other.block_;
    IncRef(block_);
    return *this;
  }
  /*! \brief check if nullptr */
  inline bool operator==(std::nullptr_t other) const
  {
    return block_ == nullptr;
  }
  /*!
   * \return get the pointer content.
   */
  inline T *get() const
  {
    if (block_ == nullptr)
      return nullptr;
    return reinterpret_cast<T *>(&(block_->data));
  }
  /*!
   * \brief reset the pointer to nullptr.
   */
  inline void reset()
  {
    DecRef(block_);
    block_ = nullptr;
  }
  /*! \return if use_count == 1*/
  inline bool unique() const
  {
    if (block_ == nullptr)
      return false;
    return block_->use_count_ == 1;
  }
  /*! \return dereference pointer */
  inline T *operator*() const
  {
    return reinterpret_cast<T *>(&(block_->data));
  }
  /*! \return dereference pointer */
  inline T *operator->() const
  {
    return reinterpret_cast<T *>(&(block_->data));
  }
  /*!
   * \brief create a new space from threadlocal storage and return it.
   * \tparam Args the arguments.
   * \param args The input argument
   * \return the allocated pointer.
   */
  template <typename... Args>
  inline static ThreadlocalAllocSharedPtr<T> Create(Args &&... args)
  {
    ThreadlocalAllocator<RefBlock> arena;
    ThreadlocalAllocSharedPtr<T> p;
    p.block_ = arena.allocate(1);
    p.block_->use_count_ = 1;
    new (&(p.block_->data)) T(std::forward<Args>(args)...);
    return p;
  }

private:
  // internal reference block
  struct RefBlock
  {
    typename std::aligned_storage<sizeof(T), alignof(T)>::type data;
    unsigned use_count_;
  };
  // decrease ref counter
  inline static void DecRef(RefBlock *block)
  {
    if (block != nullptr)
    {
      if (--block->use_count_ == 0)
      {
        ThreadlocalAllocator<RefBlock> arena;
        T *dptr = reinterpret_cast<T *>(&(block->data));
        dptr->~T();
        arena.deallocate(block, 1);
      }
    }
  }
  // increase ref counter
  inline static void IncRef(RefBlock *block)
  {
    if (block != nullptr)
    {
      ++block->use_count_;
    }
  }
  // internal block
  RefBlock *block_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ALLOCATOR_H_