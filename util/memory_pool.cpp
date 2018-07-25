
#include "memory_pool.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include "bitmap.h"
#include "math_util.h"

namespace mycc
{
namespace util
{

BuddyPool *BuddyPool::buddy_new(int32_t level)
{
  int32_t size = 1 << level;
  BuddyPool *self = (BuddyPool *)malloc(sizeof(BuddyPool) + sizeof(uint8_t) * (size * 2 - 2));
  self->level = level;
  memset(self->tree, BUDDY_NODE_UNUSED, size * 2 - 1);
  return self;
}

void BuddyPool::buddy_delete(BuddyPool *self)
{
  free(self);
}

void BuddyPool::_mark_parent(BuddyPool *self, int32_t index)
{
  for (;;)
  {
    int32_t BuddyPool = index - 1 + (index & 1) * 2;
    if (BuddyPool > 0 && (self->tree[BuddyPool] == BUDDY_NODE_USED || self->tree[BuddyPool] == BUDDY_NODE_FULL))
    {
      index = (index + 1) / 2 - 1;
      self->tree[index] = BUDDY_NODE_FULL;
    }
    else
    {
      return;
    }
  }
}

int32_t BuddyPool::buddy_alloc(BuddyPool *self, int32_t s)
{
  int32_t size;
  if (s == 0)
  {
    size = 1;
  }
  else
  {
    size = (int32_t)next_pow_of_2(s);
  }
  int32_t length = 1 << self->level;

  if (size > length)
    return -1;

  int32_t index = 0;
  int32_t level = 0;

  while (index >= 0)
  {
    if (size == length)
    {
      if (self->tree[index] == BUDDY_NODE_UNUSED)
      {
        self->tree[index] = BUDDY_NODE_USED;
        _mark_parent(self, index);
        return _index_offset(index, level, self->level);
      }
    }
    else
    {
      // size < length
      switch (self->tree[index])
      {
      case BUDDY_NODE_USED:
      case BUDDY_NODE_FULL:
        break;
      case BUDDY_NODE_UNUSED:
        // split first
        self->tree[index] = BUDDY_NODE_SPLIT;
        self->tree[index * 2 + 1] = BUDDY_NODE_UNUSED;
        self->tree[index * 2 + 2] = BUDDY_NODE_UNUSED;
      default:
        index = index * 2 + 1;
        length /= 2;
        level++;
        continue;
      }
    }
    if (index & 1)
    {
      ++index;
      continue;
    }
    for (;;)
    {
      level--;
      length *= 2;
      index = (index + 1) / 2 - 1;
      if (index < 0)
        return -1;
      if (index & 1)
      {
        ++index;
        break;
      }
    }
  }

  return -1;
}

void BuddyPool::_combine(BuddyPool *self, int32_t index)
{
  for (;;)
  {
    int32_t BuddyPool = index - 1 + (index & 1) * 2;
    if (BuddyPool < 0 || self->tree[BuddyPool] != BUDDY_NODE_UNUSED)
    {
      self->tree[index] = BUDDY_NODE_UNUSED;
      while (((index = (index + 1) / 2 - 1) >= 0) && self->tree[index] == BUDDY_NODE_FULL)
      {
        self->tree[index] = BUDDY_NODE_SPLIT;
      }
      return;
    }
    index = (index + 1) / 2 - 1;
  }
}

void BuddyPool::buddy_free(BuddyPool *self, int32_t offset)
{
  assert(offset < (1 << self->level));
  int32_t left = 0;
  int32_t length = 1 << self->level;
  int32_t index = 0;

  for (;;)
  {
    switch (self->tree[index])
    {
    case BUDDY_NODE_USED:
      assert(offset == left);
      _combine(self, index);
      return;
    case BUDDY_NODE_UNUSED:
      assert(0);
      return;
    default:
      length /= 2;
      if (offset < left + length)
      {
        index = index * 2 + 1;
      }
      else
      {
        left += length;
        index = index * 2 + 2;
      }
      break;
    }
  }
}

int32_t BuddyPool::buddy_size(BuddyPool *self, int32_t offset)
{
  assert(offset < (1 << self->level));
  int32_t left = 0;
  int32_t length = 1 << self->level;
  int32_t index = 0;

  for (;;)
  {
    switch (self->tree[index])
    {
    case BUDDY_NODE_USED:
      assert(offset == left);
      return length;
    case BUDDY_NODE_UNUSED:
      assert(0);
      return length;
    default:
      length /= 2;
      if (offset < left + length)
      {
        index = index * 2 + 1;
      }
      else
      {
        left += length;
        index = index * 2 + 2;
      }
      break;
    }
  }
}

void BuddyPool::_dump(BuddyPool *self, int32_t index, int32_t level)
{
  switch (self->tree[index])
  {
  case BUDDY_NODE_UNUSED:
    printf("(%d:%d)", _index_offset(index, level, self->level), 1 << (self->level - level));
    break;
  case BUDDY_NODE_USED:
    printf("[%d:%d]", _index_offset(index, level, self->level), 1 << (self->level - level));
    break;
  case BUDDY_NODE_FULL:
    printf("{");
    _dump(self, index * 2 + 1, level + 1);
    _dump(self, index * 2 + 2, level + 1);
    printf("}");
    break;
  default:
    printf("(");
    _dump(self, index * 2 + 1, level + 1);
    _dump(self, index * 2 + 2, level + 1);
    printf(")");
    break;
  }
}

void BuddyPool::buddy_dump(BuddyPool *self)
{
  _dump(self, 0, 0);
  printf("\n");
}

namespace easy_pool
{

easy_pool_realloc_pt easy_pool_realloc = easy_pool_default_realloc;

void *easy_pool_alloc_block(easy_pool_t *pool, uint32_t size)
{
  uint8_t *m;
  uint32_t psize;
  easy_pool_t *p, *newpool, *current;

  psize = (uint32_t)(pool->end - (uint8_t *)pool);

  if ((m = (uint8_t *)easy_pool_realloc(NULL, psize)) == NULL)
    return NULL;

  newpool = (easy_pool_t *)m;
  newpool->end = m + psize;
  newpool->next = NULL;
  newpool->failed = 0;

  m += offsetof(easy_pool_t, current);
  m = BASE_ALIGN_PTR(m, sizeof(unsigned long));
  newpool->last = m + size;
  current = pool->current;

  for (p = current; p->next; p = p->next)
  {
    if (p->failed++ > 4)
    {
      current = p->next;
    }
  }

  p->next = newpool;
  pool->current = current ? current : newpool;

  return m;
}

void *easy_pool_alloc_large(easy_pool_t *pool, easy_pool_large_t *large, uint32_t size)
{
  if ((large->data = (uint8_t *)easy_pool_realloc(NULL, size)) == NULL)
    return NULL;

  large->next = pool->large;
  pool->large = large;
  return large->data;
}

easy_pool_t *easy_pool_create(uint32_t size)
{
  easy_pool_t *p;

  size = BASE_ALIGN_UP(size + sizeof(easy_pool_t), EASY_POOL_ALIGNMENT);

  if ((p = (easy_pool_t *)easy_pool_realloc(NULL, size)) == NULL)
    return NULL;

  memset(p, 0, sizeof(easy_pool_t));
  p->last = (uint8_t *)p + sizeof(easy_pool_t);
  p->end = (uint8_t *)p + size;
  p->max = size - sizeof(easy_pool_t);
  p->current = p;

  return p;
}

void easy_pool_clear(easy_pool_t *pool)
{
  easy_pool_t *p, *n;
  easy_pool_large_t *l;

  // large
  for (l = pool->large; l; l = l->next)
  {
    easy_pool_realloc(l->data, 0);
  }

  // other page
  for (p = pool->next; p; p = n)
  {
    n = p->next;
    easy_pool_realloc(p, 0);
  }

  pool->large = NULL;
  pool->next = NULL;
  pool->current = pool;
  pool->failed = 0;
  pool->last = (uint8_t *)pool + sizeof(easy_pool_t);
}

void easy_pool_destroy(easy_pool_t *pool)
{
  easy_pool_clear(pool);
  assert(pool->ref == 0);
  easy_pool_realloc(pool, 0);
}

void *easy_pool_alloc_ex(easy_pool_t *pool, uint32_t size, uint32_t align)
{
  uint8_t *m;
  easy_pool_t *p;
  int32_t flags, dsize;

  // init
  dsize = 0;
  flags = pool->flags;

  if (size > pool->max)
  {
    dsize = size;
    size = sizeof(easy_pool_large_t);
  }

  if (UNLIKELY(flags))
    atomic_spin_lock(&pool->tlock);

  p = pool->current;

  do
  {
    m = BASE_ALIGN_PTR(p->last, align);

    if (m + size <= p->end)
    {
      p->last = m + size;
      break;
    }

    p = p->next;
  } while (p);

  // reallocate one
  if (p == NULL)
  {
    m = (uint8_t *)easy_pool_alloc_block(pool, size);
  }

  if (m && dsize)
  {
    m = (uint8_t *)easy_pool_alloc_large(pool, (easy_pool_large_t *)m, dsize);
  }

  if (UNLIKELY(flags))
    atomic_spin_unlock(&pool->tlock);

  return m;
}

void *easy_pool_calloc(easy_pool_t *pool, uint32_t size)
{
  void *p;

  if ((p = easy_pool_alloc_ex(pool, size, sizeof(long))) != NULL)
    memset(p, 0, size);

  return p;
}

void easy_pool_set_lock(easy_pool_t *pool)
{
  pool->flags = 1;
}

void easy_pool_set_allocator(easy_pool_realloc_pt alloc)
{
  easy_pool_realloc = (alloc ? alloc : easy_pool_default_realloc);
}

void *easy_pool_default_realloc(void *ptr, size_t size)
{
  if (size)
  {
    return realloc(ptr, size);
  }
  else if (ptr)
  {
    free(ptr);
  }

  return 0;
}

char *easy_pool_strdup(easy_pool_t *pool, const char *str)
{
  int32_t sz;
  char *ptr;

  if (str == NULL)
    return NULL;

  sz = strlen(str) + 1;

  if ((ptr = (char *)easy_pool_alloc(pool, sz)) == NULL)
    return NULL;

  memcpy(ptr, str, sz);
  return ptr;
}

void *easy_pool_alloc(easy_pool_t *pool, uint32_t size)
{
  return easy_pool_alloc_ex(pool, size, sizeof(long));
}

void *easy_pool_nalloc(easy_pool_t *pool, uint32_t size)
{
  return easy_pool_alloc_ex(pool, size, 1);
}

} // namespace easy_pool

/*-----------------------------------------------------------------------------
 *  PageMemPoolImpl
 *-----------------------------------------------------------------------------*/
void PageMemPool::initialize(char *pool, int32_t page_size, int32_t total_pages,
                             int32_t meta_len)
{
  assert(pool != 0);
  assert(page_size >= (1 << 20));
  assert(total_pages > 0);
  assert(static_cast<int32_t>(sizeof(mem_pool_impl)) <=
         PageMemPool::MEM_HASH_METADATA_START);

  impl = reinterpret_cast<mem_pool_impl *>(pool);
  impl->initialize(pool, page_size, total_pages, meta_len);
}

PageMemPool::~PageMemPool()
{
  if (impl->pool != NULL)
  {
    munmap(impl->pool, static_cast<int64_t>(impl->page_size) * impl->total_pages);
  }
}

char *PageMemPool::alloc_page()
{
  int32_t index;
  return impl->alloc_page(index);
}

char *PageMemPool::alloc_page(int32_t &index)
{
  return impl->alloc_page(index);
}

void PageMemPool::free_page(const char *page)
{
  impl->free_page(page);
}

void PageMemPool::free_page(int32_t index)
{
  impl->free_page(index);
}

char *PageMemPool::index_to_page(int32_t index)
{
  return impl->index_to_page(index);
}

int32_t PageMemPool::page_to_index(char *page)
{
  return impl->page_to_index(page);
}

/* man shm_open:The newly-allocated bytes of a shared memory object are automatically initialised to 0.*/
void PageMemPool::mem_pool_impl::initialize(char *tpool, int32_t this_page_size,
                                            int32_t this_total_pages, int32_t meta_len)
{
  assert(tpool != 0);
  pool = tpool;
  asm volatile("mfence" ::
                   : "memory");
  if (inited != 1)
  {
    page_size = this_page_size;
    total_pages = this_total_pages;
    memset((void *)&page_bitmap, 0, BITMAP_SIZE);
    int32_t meta_pages = (meta_len + page_size - 1) / page_size;
    for (int32_t i = 0; i < meta_pages; ++i)
    {
      easybit_setbit(page_bitmap, i);
    }
    free_pages = total_pages - meta_pages;
    current_page = meta_pages;
    assert(free_pages > 0);

    inited = 1;
  }
}

char *PageMemPool::mem_pool_impl::alloc_page(int32_t &index)
{
  assert(inited == 1);
  if (free_pages == 0)
  {
    return 0; /* there are no pages to allocate */
  }

  /*
     * find a free page index from bitmap
     */
  int32_t page_index;
  for (;; ++current_page)
  {
    if (current_page == total_pages)
    {
      current_page = 0;
    }
    if (easybit_isset(page_bitmap, current_page) == 0)
    { /* found */
      page_index = current_page++;
      break;
    }
  }
  index = page_index;
  --free_pages;
  easybit_setbit(page_bitmap, index);
  return index_to_page(index);
}

void PageMemPool::mem_pool_impl::free_page(int32_t index)
{
  assert(index > 0 && index < total_pages); /* page 0 is used to META DATA */
  if (easybit_isset(page_bitmap, index) == 0)
  { /* has already released */
    return;
  }
  easybit_clrbit(page_bitmap, index);
  ++free_pages;
}

void PageMemPool::mem_pool_impl::free_page(const char *page)
{
  assert(page != 0);
  int32_t index = page_to_index(page);
  free_page(index);
}

char *PageMemPool::mem_pool_impl::index_to_page(int32_t index)
{
  assert(index > 0 && index < total_pages);
  uint64_t offset = static_cast<uint64_t>(index);
  offset *= page_size;
  return pool + offset;
}

int32_t PageMemPool::mem_pool_impl::page_to_index(const char *page)
{
  assert(page != 0);
  uint64_t offset = page - pool;
  return offset / static_cast<uint64_t>(page_size);
}

char *PageMemPool::mem_pool_impl::get_pool_addr()
{
  return pool;
}

/////////////////////// TCMemPool /////////////////////////////////

TCMemChunk::TCMemChunk()
    : _pHead(NULL), _pData(NULL)
{
}

uint64_t TCMemChunk::calcMemSize(uint64_t iBlockSize, uint64_t iBlockCount)
{
  return iBlockSize * iBlockCount + sizeof(tagChunkHead);
}

uint64_t TCMemChunk::calcBlockCount(uint64_t iMemSize, uint64_t iBlockSize)
{
  if (iMemSize <= sizeof(tagChunkHead))
  {
    return 0;
  }
  return MATH_MIN(((iMemSize - sizeof(tagChunkHead)) / iBlockSize), (uint64_t)(-1));
}

void TCMemChunk::create(void *pAddr, uint64_t iBlockSize, uint64_t iBlockCount)
{
  assert(iBlockSize > sizeof(uint64_t));
  init(pAddr);

  _pHead->_iBlockSize = iBlockSize;
  _pHead->_iBlockCount = iBlockCount;
  _pHead->_firstAvailableBlock = 0;
  _pHead->_blockAvailable = iBlockCount;

  memset(_pData, 0x00, iBlockCount * iBlockSize);
  unsigned char *pt = _pData;
  for (uint64_t i = 0; i != iBlockCount; pt += iBlockSize)
  {
    ++i;
    memcpy(pt, &i, sizeof(uint64_t));
  }
}

void TCMemChunk::connect(void *pAddr)
{
  init(pAddr);
}

void TCMemChunk::init(void *pAddr)
{
  _pHead = static_cast<tagChunkHead *>(pAddr);
  _pData = (unsigned char *)((char *)_pHead + sizeof(tagChunkHead));
}

void *TCMemChunk::allocate()
{
  if (!isBlockAvailable())
    return NULL;

  unsigned char *result = _pData + (_pHead->_firstAvailableBlock * _pHead->_iBlockSize);
  --_pHead->_blockAvailable;
  _pHead->_firstAvailableBlock = *((uint64_t *)result);
  memset(result, 0x00, sizeof(_pHead->_iBlockSize));
  return result;
}

void *TCMemChunk::allocate2(uint64_t &iIndex)
{
  iIndex = _pHead->_firstAvailableBlock + 1;
  void *pAddr = allocate();

  if (pAddr == NULL)
  {
    iIndex = 0;
    return NULL;
  }

  return pAddr;
}

void TCMemChunk::deallocate(void *pAddr)
{
  assert(pAddr >= _pData);
  unsigned char *prelease = static_cast<unsigned char *>(pAddr);
  assert((prelease - _pData) % _pHead->_iBlockSize == 0);
  memset(pAddr, 0x00, _pHead->_iBlockSize);
  *((uint64_t *)prelease) = _pHead->_firstAvailableBlock;
  _pHead->_firstAvailableBlock = static_cast<uint64_t>((prelease - _pData) / _pHead->_iBlockSize);
  assert(_pHead->_firstAvailableBlock == (prelease - _pData) / _pHead->_iBlockSize);
  ++_pHead->_blockAvailable;
}

void TCMemChunk::deallocate2(uint64_t iIndex)
{
  assert(iIndex > 0 && iIndex <= _pHead->_iBlockCount);
  void *pAddr = _pData + (iIndex - 1) * _pHead->_iBlockSize;
  deallocate(pAddr);
}

void *TCMemChunk::getAbsolute(uint64_t iIndex)
{
  assert(iIndex > 0 && iIndex <= _pHead->_iBlockCount);
  void *pAddr = _pData + (iIndex - 1) * _pHead->_iBlockSize;
  return pAddr;
}

uint64_t TCMemChunk::getRelative(void *pAddr)
{
  assert((char *)pAddr >= (char *)_pData && ((char *)pAddr <= (char *)_pData + _pHead->_iBlockSize * _pHead->_iBlockCount));
  assert(((char *)pAddr - (char *)_pData) % _pHead->_iBlockSize == 0);
  return 1 + ((char *)pAddr - (char *)_pData) / _pHead->_iBlockSize;
}

void TCMemChunk::rebuild()
{
  assert(_pHead);

  _pHead->_firstAvailableBlock = 0;
  _pHead->_blockAvailable = _pHead->_iBlockCount;

  memset(_pData, 0x00, _pHead->_iBlockCount * _pHead->_iBlockSize);
  unsigned char *pt = _pData;
  for (uint64_t i = 0; i != _pHead->_iBlockCount; pt += _pHead->_iBlockSize)
  {
    ++i;
    memcpy(pt, &i, sizeof(uint64_t));
  }
}

TCMemChunk::tagChunkHead TCMemChunk::getChunkHead() const
{
  return *_pHead;
}

TCMemChunkAllocator::TCMemChunkAllocator()
    : _pHead(NULL), _pChunk(NULL)
{
}

void TCMemChunkAllocator::init(void *pAddr)
{
  _pHead = static_cast<tagChunkAllocatorHead *>(pAddr);
  _pChunk = (char *)_pHead + sizeof(tagChunkAllocatorHead);
}

void TCMemChunkAllocator::initChunk()
{
  assert(_pHead->_iSize > sizeof(tagChunkAllocatorHead));
  uint64_t iChunkCapacity = _pHead->_iSize - sizeof(tagChunkAllocatorHead);
  assert(iChunkCapacity > TCMemChunk::getHeadSize());
  uint64_t iBlockCount = (iChunkCapacity - TCMemChunk::getHeadSize()) / _pHead->_iBlockSize;
  assert(iBlockCount > 0);
  _chunk.create((void *)((char *)_pChunk), _pHead->_iBlockSize, iBlockCount);
}

void TCMemChunkAllocator::create(void *pAddr, uint64_t iSize, uint64_t iBlockSize)
{
  init(pAddr);
  _pHead->_iSize = iSize;
  _pHead->_iBlockSize = iBlockSize;
  initChunk();
}

void TCMemChunkAllocator::connectChunk()
{
  assert(_pHead->_iSize > sizeof(tagChunkAllocatorHead));
  uint64_t iChunkCapacity = _pHead->_iSize - sizeof(tagChunkAllocatorHead);
  assert(iChunkCapacity > TCMemChunk::getHeadSize());
  _chunk.connect((void *)((char *)_pChunk));
}

void TCMemChunkAllocator::connect(void *pAddr)
{
  init(pAddr);
  connectChunk();
}

void TCMemChunkAllocator::rebuild()
{
  _chunk.rebuild();
}

void *TCMemChunkAllocator::allocate()
{
  return _chunk.allocate();
}

void *TCMemChunkAllocator::allocate2(uint64_t &iIndex)
{
  return _chunk.allocate2(iIndex);
}

void TCMemChunkAllocator::deallocate(void *pAddr)
{
  assert(pAddr >= _pChunk);

  _chunk.deallocate(pAddr);
}

void TCMemChunkAllocator::deallocate2(uint64_t iIndex)
{
  _chunk.deallocate2(iIndex);
}

TCMemChunk::tagChunkHead TCMemChunkAllocator::getBlockDetail() const
{
  return _chunk.getChunkHead();
}

TCMemMultiChunkAllocator::TCMemMultiChunkAllocator()
    : _pHead(NULL), _pChunk(NULL), _iBlockCount(0), _iAllIndex(0), _nallocator(NULL)
{
}

TCMemMultiChunkAllocator::~TCMemMultiChunkAllocator()
{
  clear();
}

void TCMemMultiChunkAllocator::clear()
{
  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    delete _allocator[i];
  }
  _allocator.clear();

  if (_nallocator)
  {
    _nallocator->clear();
    delete _nallocator;
    _nallocator = NULL;
  }

  _vBlockSize.clear();

  _pHead = NULL;
  _pChunk = NULL;
  _iBlockCount = 0;
  _iAllIndex = 0;
}

std::vector<uint64_t> TCMemMultiChunkAllocator::getBlockSize() const
{
  std::vector<uint64_t> v = _vBlockSize;
  if (_nallocator)
  {
    std::vector<uint64_t> vNext = _nallocator->getBlockSize();
    copy(vNext.begin(), vNext.end(), inserter(v, v.end()));
  }

  return v;
}

uint64_t TCMemMultiChunkAllocator::getCapacity() const
{
  uint64_t iCapacity = 0;
  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    iCapacity += _allocator[i]->getCapacity();
  }

  if (_nallocator)
  {
    iCapacity += _nallocator->getCapacity();
  }

  return iCapacity;
}

std::vector<TCMemChunk::tagChunkHead> TCMemMultiChunkAllocator::getBlockDetail() const
{
  std::vector<TCMemChunk::tagChunkHead> vt;
  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    vt.push_back(_allocator[i]->getBlockDetail());
  }

  if (_nallocator)
  {
    std::vector<TCMemChunk::tagChunkHead> v = _nallocator->getBlockDetail();

    copy(v.begin(), v.end(), inserter(vt, vt.end()));
  }

  return vt;
}

std::vector<uint64_t> TCMemMultiChunkAllocator::singleBlockChunkCount() const
{
  std::vector<uint64_t> vv;
  vv.push_back(_iBlockCount);

  if (_nallocator)
  {
    std::vector<uint64_t> v = _nallocator->singleBlockChunkCount();
    copy(v.begin(), v.end(), inserter(vv, vv.end()));
  }

  return vv;
}

uint64_t TCMemMultiChunkAllocator::allBlockChunkCount() const
{
  uint64_t n = _iBlockCount * _vBlockSize.size();

  if (_nallocator)
  {
    n += _nallocator->allBlockChunkCount();
  }

  return n;
}

void TCMemMultiChunkAllocator::init(void *pAddr)
{
  _pHead = static_cast<tagChunkAllocatorHead *>(pAddr);
  _pChunk = (char *)_pHead + sizeof(tagChunkAllocatorHead);
}

void TCMemMultiChunkAllocator::calc()
{
  _vBlockSize.clear();

  uint64_t sum = 0;
  for (uint64_t n = _pHead->_iMinBlockSize; n < _pHead->_iMaxBlockSize;)
  {
    sum += n;
    _vBlockSize.push_back(n);

    if (_pHead->_iMaxBlockSize > _pHead->_iMinBlockSize)
    {
      n = MATH_MAX((uint64_t)(n * _pHead->_fFactor), n + 1);
    }
  }

  sum += _pHead->_iMaxBlockSize;
  _vBlockSize.push_back(_pHead->_iMaxBlockSize);

  assert(_pHead->_iSize > (TCMemMultiChunkAllocator::getHeadSize() + TCMemChunkAllocator::getHeadSize() * _vBlockSize.size() + TCMemChunk::getHeadSize() * _vBlockSize.size()));

  _iBlockCount = (_pHead->_iSize - TCMemMultiChunkAllocator::getHeadSize() - TCMemChunkAllocator::getHeadSize() * _vBlockSize.size() - TCMemChunk::getHeadSize() * _vBlockSize.size()) / sum;

  assert(_iBlockCount >= 1);
}

void TCMemMultiChunkAllocator::create(void *pAddr, uint64_t iSize, uint64_t iMinBlockSize, uint64_t iMaxBlockSize, float fFactor)
{
  assert(iMaxBlockSize >= iMinBlockSize);
  assert(fFactor >= 1.0);

  init(pAddr);

  _pHead->_iSize = iSize;
  _pHead->_iTotalSize = iSize;
  _pHead->_iMinBlockSize = iMinBlockSize;
  _pHead->_iMaxBlockSize = iMaxBlockSize;
  _pHead->_fFactor = fFactor;
  _pHead->_iNext = 0;

  calc();

  char *pChunkBegin = (char *)_pChunk;
  for (uint64_t i = 0; i < _vBlockSize.size(); i++)
  {
    TCMemChunkAllocator *p = new TCMemChunkAllocator;
    uint64_t iAllocSize = TCMemChunkAllocator::getHeadSize() + TCMemChunk::calcMemSize(_vBlockSize[i], _iBlockCount);
    p->create(pChunkBegin, iAllocSize, _vBlockSize[i]);
    pChunkBegin += iAllocSize;
    _allocator.push_back(p);
  }

  _iAllIndex = _allocator.size() * getBlockCount();
}

void TCMemMultiChunkAllocator::connect(void *pAddr)
{
  clear();

  init(pAddr);

  calc();

  char *pChunkBegin = (char *)_pChunk;
  for (uint64_t i = 0; i < _vBlockSize.size(); i++)
  {
    TCMemChunkAllocator *p = new TCMemChunkAllocator;

    p->connect(pChunkBegin);
    pChunkBegin += TCMemChunkAllocator::getHeadSize() + TCMemChunk::calcMemSize(_vBlockSize[i], _iBlockCount);
    _allocator.push_back(p);
  }

  _iAllIndex = _allocator.size() * getBlockCount();

  if (_pHead->_iNext == 0)
  {
    return;
  }

  assert(_pHead->_iNext == _pHead->_iSize);
  assert(_nallocator == NULL);

  tagChunkAllocatorHead *pNextHead = (tagChunkAllocatorHead *)((char *)_pHead + _pHead->_iNext);
  _nallocator = new TCMemMultiChunkAllocator();
  _nallocator->connect(pNextHead);
}

void TCMemMultiChunkAllocator::rebuild()
{
  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    _allocator[i]->rebuild();
  }

  if (_nallocator)
  {
    _nallocator->rebuild();
  }
}

TCMemMultiChunkAllocator *TCMemMultiChunkAllocator::lastAlloc()
{
  if (_nallocator == NULL)
    return NULL;

  TCMemMultiChunkAllocator *p = _nallocator;

  while (p && p->_nallocator)
  {
    p = p->_nallocator;
  }

  return p;
}

void TCMemMultiChunkAllocator::append(void *pAddr, uint64_t iSize)
{
  connect(pAddr);

  assert(iSize > _pHead->_iTotalSize);

  void *pAppendAddr = (char *)pAddr + _pHead->_iTotalSize;

  TCMemMultiChunkAllocator *p = new TCMemMultiChunkAllocator();
  p->create(pAppendAddr, iSize - _pHead->_iTotalSize, _pHead->_iMinBlockSize, _pHead->_iMaxBlockSize, _pHead->_fFactor);

  TCMemMultiChunkAllocator *palloc = lastAlloc();
  if (palloc)
  {
    palloc->_pHead->_iNext = (char *)pAppendAddr - (char *)palloc->_pHead;
    palloc->_nallocator = p;
  }
  else
  {
    _pHead->_iNext = (char *)pAppendAddr - (char *)_pHead;
    _nallocator = p;
  }

  assert(_pHead->_iNext == _pHead->_iSize);
  _pHead->_iTotalSize = iSize;
}

void *TCMemMultiChunkAllocator::allocate(uint64_t iNeedSize, uint64_t &iAllocSize)
{
  uint64_t iIndex;
  return allocate2(iNeedSize, iAllocSize, iIndex);
}

void *TCMemMultiChunkAllocator::allocate2(uint64_t iNeedSize, uint64_t &iAllocSize, uint64_t &iIndex)
{
  void *p = NULL;
  iIndex = 0;

  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    if (_allocator[i]->getBlockSize() >= iNeedSize)
    {
      uint64_t n;
      p = _allocator[i]->allocate2(n);
      if (p != NULL)
      {
        iAllocSize = _vBlockSize[i];
        iIndex += i * getBlockCount() + n;
        return p;
      }
    }
  }

  for (uint64_t i = _allocator.size(); i != 0; i--)
  {
    if (_allocator[i - 1]->getBlockSize() < iNeedSize)
    {
      uint64_t n;
      p = _allocator[i - 1]->allocate2(n);
      if (p != NULL)
      {
        iAllocSize = _vBlockSize[i - 1];
        iIndex += (i - 1) * getBlockCount() + n;
        return p;
      }
    }
  }

  if (_nallocator)
  {
    p = _nallocator->allocate2(iNeedSize, iAllocSize, iIndex);
    if (p != NULL)
    {
      iIndex += _iAllIndex;
      return p;
    }
  }

  return NULL;
}

void TCMemMultiChunkAllocator::deallocate(void *pAddr)
{
  if (pAddr < (void *)((char *)_pHead + _pHead->_iSize))
  {
    char *pChunkBegin = (char *)_pChunk;

    for (uint64_t i = 0; i < _vBlockSize.size(); i++)
    {
      pChunkBegin += _allocator[i]->getMemSize();
      // pChunkBegin += TCMemChunkAllocator::getHeadSize() + TCMemChunk::calcMemSize(_vBlockSize[i], _iBlockCount);

      if ((char *)pAddr < pChunkBegin)
      {
        _allocator[i]->deallocate(pAddr);
        return;
      }
    }

    assert(false);
  }
  else
  {
    if (_nallocator)
    {
      _nallocator->deallocate(pAddr);
      return;
    }
  }

  assert(false);
}

void TCMemMultiChunkAllocator::deallocate2(uint64_t iIndex)
{
  for (uint64_t i = 0; i < _allocator.size(); i++)
  {
    if (iIndex <= getBlockCount())
    {
      _allocator[i]->deallocate2(iIndex);
      return;
    }
    iIndex -= getBlockCount();
  }

  if (_nallocator)
  {
    _nallocator->deallocate2(iIndex);
    return;
  }

  assert(false);
}

void *TCMemMultiChunkAllocator::getAbsolute(uint64_t iIndex)
{
  if (iIndex == 0)
  {
    return NULL;
  }

  uint64_t n = _iAllIndex;
  if (iIndex <= n)
  {
    uint64_t i = (iIndex - 1) / getBlockCount();
    iIndex -= i * getBlockCount();
    return _allocator[i]->getAbsolute(iIndex);
  }
  else
  {
    iIndex -= n;
  }

  if (_nallocator)
  {
    return _nallocator->getAbsolute(iIndex);
  }

  assert(false);
  return NULL;
}

uint64_t TCMemMultiChunkAllocator::getRelative(void *pAddr)
{
  if (pAddr == NULL)
  {
    return 0;
  }

  if (pAddr < (char *)_pHead + _pHead->_iSize)
  {
    for (uint64_t i = 0; i < _vBlockSize.size(); i++)
    {
      if ((char *)pAddr < ((char *)_allocator[i]->getHead() + _allocator[i]->getMemSize()))
      {
        // index start from 1
        return i * getBlockCount() + _allocator[i]->getRelative(pAddr);
      }
    }
    assert(false);
  }
  else
  {
    if (_nallocator)
    {
      return _iAllIndex + _nallocator->getRelative((char *)pAddr);
    }
  }

  return 0;
}

} // namespace util
} // namespace mycc