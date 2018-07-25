
#ifndef MYCC_UTIL_MEMORY_POOL_H_
#define MYCC_UTIL_MEMORY_POOL_H_

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <algorithm>
#include <list>
#include <memory>
#include <vector>
#include "locks_util.h"
#include "singleton.h"
#include "threadlocal_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

// lazy destroy
class Crematory
{
public:
  Crematory()
      : last_fire_time_(0)
  {
  }

  ~Crematory()
  {
    fire(true);
  }

  typedef void (*CleanupFunc)(void *trash);

  // LAZY_DESTROY(trash, cleanup)
  void add(void *trash, CleanupFunc cleanup)
  {
    MutexLock guard(&lock_);
    bodys_.push_back(new Body(time(NULL), trash, cleanup));
  }

  // TRY_DESTROY_TRASH
  // not want to be self-thread here, somebody should call me.
  void fire(bool force = false)
  {
    if (!bodys_.empty() && (force || time(NULL) - last_fire_time_ > Body::ALIVE_TIME_S))
    {
      // avoid side-effect of cleanup
      std::vector<Body *> deads;
      {
        MutexLock guard(&lock_);
        time_t now = time(NULL);
        std::list<Body *>::iterator it = bodys_.begin();
        while (it != bodys_.end() && (force || (*it)->dead(now))) // list is time-sorted
        {
          deads.push_back(*it);
          it = bodys_.erase(it);
        }
        last_fire_time_ = time(NULL);
      }

      for (uint64_t i = 0; i < deads.size(); ++i)
      {
        delete deads[i];
      }
    }
  }

public:
  static Crematory *g_crematory;

private:
  typedef struct Body
  {
    Body(time_t time, void *trash, CleanupFunc cleanup)
        : dead_time_(time + ALIVE_TIME_S), trash_(trash), cleanup_(cleanup) {}

    ~Body()
    {
      cleanup_(trash_);
    }

    bool dead(time_t now)
    {
      return now > dead_time_;
    }

    time_t dead_time_;
    void *trash_;
    CleanupFunc cleanup_;

    static const time_t ALIVE_TIME_S = 10;
  } Body;

  Mutex lock_;
  std::list<Body *> bodys_;
  time_t last_fire_time_;
};

// A simple buddy memory allocation library.
// http://blog.codingnow.com/2011/12/buddy_memory_allocation.html (in Chinese)
struct BuddyPool
{
  static const int32_t BUDDY_NODE_UNUSED = 0;
  static const int32_t BUDDY_NODE_USED = 1;
  static const int32_t BUDDY_NODE_SPLIT = 2;
  static const int32_t BUDDY_NODE_FULL = 3;

  static BuddyPool *buddy_new(int32_t level);
  static void buddy_delete(BuddyPool *b);
  static int32_t buddy_alloc(BuddyPool *b, int32_t size);
  static void buddy_free(BuddyPool *b, int32_t offset);
  static int32_t buddy_size(BuddyPool *b, int32_t offset);
  static void buddy_dump(BuddyPool *b);

private:
  static inline int32_t _index_offset(int32_t index, int32_t level, int32_t max_level)
  {
    return ((index + 1) - (1 << level)) << (max_level - level);
  }
  static void _mark_parent(BuddyPool *self, int32_t index);
  static void _dump(BuddyPool *self, int32_t index, int32_t level);
  static void _combine(BuddyPool *self, int32_t index);

  int32_t level;
  uint8_t tree[1];
};

namespace easy_pool
{

typedef void *(*easy_pool_realloc_pt)(void *ptr, size_t size);

struct easy_pool_large_t
{
  easy_pool_large_t *next;
  uint8_t *data;
};

struct easy_pool_t
{
  uint8_t *last;
  uint8_t *end;
  easy_pool_t *next;
  uint16_t failed;
  uint16_t flags;
  uint32_t max;

  // pool header
  easy_pool_t *current;
  easy_pool_large_t *large;
  volatile int32_t ref;   // atomic
  volatile int32_t tlock; // atomic
};

static const uint32_t EASY_POOL_ALIGNMENT = 512;
static const uint32_t EASY_POOL_PAGE_SIZE = 4096;
extern easy_pool_realloc_pt easy_pool_realloc;

void *easy_pool_alloc_block(easy_pool_t *pool, uint32_t size);
void *easy_pool_alloc_large(easy_pool_t *pool, easy_pool_large_t *large, uint32_t size);
void *easy_pool_default_realloc(void *ptr, size_t size);
easy_pool_t *easy_pool_create(uint32_t size);
void easy_pool_clear(easy_pool_t *pool);
void easy_pool_destroy(easy_pool_t *pool);
void *easy_pool_alloc_ex(easy_pool_t *pool, uint32_t size, uint32_t align);
void *easy_pool_calloc(easy_pool_t *pool, uint32_t size);
void easy_pool_set_allocator(easy_pool_realloc_pt alloc);
void easy_pool_set_lock(easy_pool_t *pool);
char *easy_pool_strdup(easy_pool_t *pool, const char *str);
void *easy_pool_alloc(easy_pool_t *pool, uint32_t size);
void *easy_pool_nalloc(easy_pool_t *pool, uint32_t size);

} // namespace easy_pool

class PageMemPool
{
public:
  PageMemPool(char *pool, int32_t this_page_size, int32_t this_total_pages,
              int32_t meta_len)
  {
    initialize(pool, this_page_size, this_total_pages, meta_len);
  }
  ~PageMemPool();

  char *alloc_page();
  char *alloc_page(int32_t &index);
  void free_page(const char *page);
  void free_page(int32_t index);
  char *index_to_page(int32_t index);
  int32_t page_to_index(char *page);
  char *get_pool_addr()
  {
    return impl->get_pool_addr();
  }
  int32_t get_page_size()
  {
    return impl->get_page_size();
  }
  void display_statics()
  {
    impl->display_statics();
  }
  int32_t get_free_pages_num()
  {
    return impl->free_pages;
  }
  static const int32_t MAX_PAGES_NO = 65536;
  static const int32_t MDB_VERSION_INFO_START = 12288;  //12k
  static const int32_t MEM_HASH_METADATA_START = 16384; //16K
  static const int32_t MDB_STATINFO_START = 32768;      //32K
  static const int32_t MEM_POOL_METADATA_LEN = 524288;  //512K
private:
  void initialize(char *pool, int32_t page_size, int32_t total_pages, int32_t meta_len);
  static const int32_t BITMAP_SIZE = (MAX_PAGES_NO + 7) / 8;

  struct mem_pool_impl
  {
    void initialize(char *tpool, int32_t page_size, int32_t total_pages,
                    int32_t meta_len);
    char *alloc_page(int32_t &index);
    void free_page(const char *page);
    void free_page(int32_t index);
    char *index_to_page(int32_t index);
    int32_t page_to_index(const char *page);
    char *get_pool_addr();
    int32_t get_page_size()
    {
      return page_size;
    }

    void display_statics()
    {
      printf("inited:%d,page_size:%d,total_pages:%d,"
             "free_pages:%d,current_page:%d\n",
             inited,
             page_size, total_pages, free_pages, current_page);
    }

    int32_t inited;
    int32_t page_size;
    int32_t total_pages;
    int32_t free_pages;
    int32_t current_page;
    uint8_t page_bitmap[BITMAP_SIZE];
    char *pool;
  };
  mem_pool_impl *impl;
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
  static const int32_t kPageSize = ((1 << 22) / size);
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
  int32_t m_cached_count[kMaxCacheByte + 1];
  char *m_cached_objects[kMaxCacheByte + 1][kMaxCacheCount];
  int32_t m_length;
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

//////////////////////// TC_MemMalloc /////////////////////////

class TCMemChunk
{
public:
  TCMemChunk();

  static uint64_t calcMemSize(uint64_t iBlockSize, uint64_t iBlockCount);
  static uint64_t calcBlockCount(uint64_t iMemSize, uint64_t iBlockSize);
  static uint64_t getHeadSize() { return sizeof(tagChunkHead); }
  void create(void *pAddr, uint64_t iBlockSize, uint64_t iBlockCount);
  void connect(void *pAddr);
  uint64_t getBlockSize() const { return _pHead->_iBlockSize; }
  uint64_t getMemSize() const { return _pHead->_iBlockSize * _pHead->_iBlockCount + sizeof(tagChunkHead); }
  uint64_t getCapacity() const { return _pHead->_iBlockSize * _pHead->_iBlockCount; }
  uint64_t getBlockCount() const { return _pHead->_iBlockCount; }
  bool isBlockAvailable() const { return _pHead->_blockAvailable > 0; }
  uint64_t getBlockAvailableCount() const { return _pHead->_blockAvailable; }
  void *allocate();
  void *allocate2(uint64_t &iIndex);
  void deallocate(void *pAddr);
  void deallocate2(uint64_t iIndex);
  void rebuild();

  struct tagChunkHead
  {
    uint64_t _iBlockSize;
    uint64_t _iBlockCount;
    uint64_t _firstAvailableBlock;
    uint64_t _blockAvailable;
  } __attribute__((packed));

  tagChunkHead getChunkHead() const;
  void *getAbsolute(uint64_t iIndex);
  uint64_t getRelative(void *pAddr);

protected:
  void init(void *pAddr);

private:
  tagChunkHead *_pHead;
  unsigned char *_pData;
};

class TCMemChunkAllocator
{
public:
  TCMemChunkAllocator();

  void create(void *pAddr, uint64_t iSize, uint64_t iBlockSize);
  void connect(void *pAddr);
  void *getHead() const { return _pHead; }
  uint64_t getBlockSize() const { return _pHead->_iBlockSize; }
  uint64_t getMemSize() const { return _pHead->_iSize; }
  uint64_t getCapacity() const { return _chunk.getCapacity(); }
  void *allocate();
  void *allocate2(uint64_t &iIndex);
  void deallocate(void *pAddr);
  void deallocate2(uint64_t iIndex);
  uint64_t blockCount() const { return _chunk.getBlockCount(); }
  void *getAbsolute(uint64_t iIndex) { return _chunk.getAbsolute(iIndex); };
  uint64_t getRelative(void *pAddr) { return _chunk.getRelative(pAddr); };
  TCMemChunk::tagChunkHead getBlockDetail() const;
  void rebuild();

  struct tagChunkAllocatorHead
  {
    uint64_t _iSize;
    uint64_t _iBlockSize;
  } __attribute__((packed));

  static uint64_t getHeadSize() { return sizeof(tagChunkAllocatorHead); }

protected:
  void init(void *pAddr);
  void initChunk();
  void connectChunk();

  TCMemChunkAllocator(const TCMemChunkAllocator &);
  TCMemChunkAllocator &operator=(const TCMemChunkAllocator &);
  bool operator==(const TCMemChunkAllocator &mca) const;
  bool operator!=(const TCMemChunkAllocator &mca) const;

private:
  tagChunkAllocatorHead *_pHead;
  void *_pChunk;
  TCMemChunk _chunk;
};

class TCMemMultiChunkAllocator
{
public:
  TCMemMultiChunkAllocator();
  ~TCMemMultiChunkAllocator();

  void create(void *pAddr, uint64_t iSize, uint64_t iMinBlockSize, uint64_t iMaxBlockSize, float fFactor = 1.1);
  void connect(void *pAddr);
  void append(void *pAddr, uint64_t iSize);
  std::vector<uint64_t> getBlockSize() const;
  uint64_t getBlockCount() const { return _iBlockCount; }
  std::vector<TCMemChunk::tagChunkHead> getBlockDetail() const;
  uint64_t getMemSize() const { return _pHead->_iTotalSize; }
  uint64_t getCapacity() const;
  std::vector<uint64_t> singleBlockChunkCount() const;
  uint64_t allBlockChunkCount() const;
  void *allocate(uint64_t iNeedSize, uint64_t &iAllocSize);
  void *allocate2(uint64_t iNeedSize, uint64_t &iAllocSize, uint64_t &iIndex);
  void deallocate(void *pAddr);
  void deallocate2(uint64_t iIndex);
  void rebuild();
  void *getAbsolute(uint64_t iIndex);
  uint64_t getRelative(void *pAddr);

  struct tagChunkAllocatorHead
  {
    uint64_t _iSize;
    uint64_t _iTotalSize;
    uint64_t _iMinBlockSize;
    uint64_t _iMaxBlockSize;
    float _fFactor;
    uint64_t _iNext;
  } __attribute__((packed));

  static uint64_t getHeadSize() { return sizeof(tagChunkAllocatorHead); }

protected:
  void init(void *pAddr);
  void calc();
  void clear();
  TCMemMultiChunkAllocator *lastAlloc();
  TCMemMultiChunkAllocator(const TCMemMultiChunkAllocator &);
  TCMemMultiChunkAllocator &operator=(const TCMemMultiChunkAllocator &);
  bool operator==(const TCMemMultiChunkAllocator &mca) const;
  bool operator!=(const TCMemMultiChunkAllocator &mca) const;

private:
  tagChunkAllocatorHead *_pHead;
  void *_pChunk;
  std::vector<uint64_t> _vBlockSize;
  uint64_t _iBlockCount;
  std::vector<TCMemChunkAllocator *> _allocator;
  uint64_t _iAllIndex;
  TCMemMultiChunkAllocator *_nallocator;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_MEMORY_POOL_H_