
#include "memory_pool.h"
#include <string.h>
#include <algorithm>
#include <iostream>
#include <sstream>
#include "math_util.h"

namespace mycc
{
namespace util
{

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