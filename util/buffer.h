
#ifndef MYCC_UTIL_BUFFER_H_
#define MYCC_UTIL_BUFFER_H_

#include <stdlib.h>
#include <string.h>
#include <list>
#include <vector>
#include "coding_util.h"
#include "error_util.h"
#include "math_util.h"
#include "status.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

template <int32_t SIZE>
class FixedBuffer
{
public:
  FixedBuffer()
      : cur_(data_) {}

  ~FixedBuffer() {}

  void append(const char *buf, uint64_t len)
  {
    // FIXME: append partially
    if (avail() > len)
    {
      memcpy(cur_, buf, len);
      cur_ += len;
    }
  }

  const char *data() const { return data_; }
  uint64_t length() const { return static_cast<uint64_t>(cur_ - data_); }

  // write to data_ directly
  char *current() { return cur_; }
  uint64_t avail() const { return static_cast<uint64_t>(end() - cur_); }
  void add(uint64_t len) { cur_ += len; }

  void reset() { cur_ = data_; }
  void bzero() { ::bzero(data_, sizeof(data_)); }

  const char *debugString()
  {
    *cur_ = '\0';
    return data_;
  }

  // for used by unit test
  string toString() const { return string(data_, length()); }
  StringPiece toStringPiece() const { return StringPiece(data_, length()); }

private:
  const char *end() const { return data_ + sizeof(data_); }

  char data_[SIZE];
  char *cur_;

  DISALLOW_COPY_AND_ASSIGN(FixedBuffer);
};

class DataBuffer
{
public:
  static const int32_t MAX_BUFFER_SIZE = 2048;
  DataBuffer()
  {
    _pend = _pfree = _pdata = _pstart = NULL;
  }

  ~DataBuffer()
  {
    destroy();
  }

  void destroy()
  {
    if (_pstart)
    {
      free(_pstart);
      _pend = _pfree = _pdata = _pstart = NULL;
    }
  }

  char *getData()
  {
    return (char *)_pdata;
  }

  int32_t getDataLen()
  {
    return static_cast<int32_t>(_pfree - _pdata);
  }

  char *getFree()
  {
    return (char *)_pfree;
  }

  int32_t getFreeLen()
  {
    return static_cast<int32_t>(_pend - _pfree);
  }

  void drainData(int32_t len)
  {
    _pdata += len;

    if (_pdata >= _pfree)
    {
      clear();
    }
  }

  void pourData(int32_t len)
  {
    assert(_pend - _pfree >= len);
    _pfree += len;
  }

  void stripData(int32_t len)
  {
    assert(_pfree - _pdata >= len);
    _pfree -= len;
  }

  void clear()
  {
    _pdata = _pfree = _pstart;
  }

  void shrink()
  {
    if (_pstart == NULL)
    {
      return;
    }
    if ((_pend - _pstart) <= MAX_BUFFER_SIZE || (_pfree - _pdata) > MAX_BUFFER_SIZE)
    {
      return;
    }

    int32_t dlen = static_cast<int32_t>(_pfree - _pdata);
    if (dlen < 0)
    {
      dlen = 0;
    }

    unsigned char *newbuf = (unsigned char *)malloc(MAX_BUFFER_SIZE);
    assert(newbuf != NULL);

    if (dlen > 0)
    {
      memcpy(newbuf, _pdata, dlen);
    }
    free(_pstart);

    _pdata = _pstart = newbuf;
    _pfree = _pstart + dlen;
    _pend = _pstart + MAX_BUFFER_SIZE;

    return;
  }

  void writeInt8(uint8_t n)
  {
    expand(1);
    *_pfree++ = (unsigned char)n;
  }

  void writeInt16(uint16_t n)
  {
    expand(2);
    _pfree[1] = (unsigned char)n;
    n = static_cast<uint16_t>(n >> 8);
    _pfree[0] = (unsigned char)n;
    _pfree += 2;
  }

  void writeInt32(uint32_t n)
  {
    expand(4);
    _pfree[3] = (unsigned char)n;
    n >>= 8;
    _pfree[2] = (unsigned char)n;
    n >>= 8;
    _pfree[1] = (unsigned char)n;
    n >>= 8;
    _pfree[0] = (unsigned char)n;
    _pfree += 4;
  }

  void writeInt64(uint64_t n)
  {
    expand(8);
    _pfree[7] = (unsigned char)n;
    n >>= 8;
    _pfree[6] = (unsigned char)n;
    n >>= 8;
    _pfree[5] = (unsigned char)n;
    n >>= 8;
    _pfree[4] = (unsigned char)n;
    n >>= 8;
    _pfree[3] = (unsigned char)n;
    n >>= 8;
    _pfree[2] = (unsigned char)n;
    n >>= 8;
    _pfree[1] = (unsigned char)n;
    n >>= 8;
    _pfree[0] = (unsigned char)n;
    _pfree += 8;
  }

  void writeBytes(const void *src, int32_t len)
  {
    expand(len);
    memcpy(_pfree, src, len);
    _pfree += len;
  }

  void fillInt8(unsigned char *dst, uint8_t n)
  {
    *dst = n;
  }

  void fillInt16(unsigned char *dst, uint16_t n)
  {
    dst[1] = (unsigned char)n;
    n = static_cast<uint16_t>(n >> 8);
    dst[0] = (unsigned char)n;
  }

  void fillInt32(unsigned char *dst, uint32_t n)
  {
    dst[3] = (unsigned char)n;
    n >>= 8;
    dst[2] = (unsigned char)n;
    n >>= 8;
    dst[1] = (unsigned char)n;
    n >>= 8;
    dst[0] = (unsigned char)n;
  }

  void fillInt64(unsigned char *dst, uint64_t n)
  {
    dst[7] = (unsigned char)n;
    n >>= 8;
    dst[6] = (unsigned char)n;
    n >>= 8;
    dst[5] = (unsigned char)n;
    n >>= 8;
    dst[4] = (unsigned char)n;
    n >>= 8;
    dst[3] = (unsigned char)n;
    n >>= 8;
    dst[2] = (unsigned char)n;
    n >>= 8;
    dst[1] = (unsigned char)n;
    n >>= 8;
    dst[0] = (unsigned char)n;
  }

  void writeString(const char *str)
  {
    int32_t len = (str ? static_cast<int32_t>(strlen(str)) : 0);
    if (len > 0)
    {
      len++;
    }
    expand(static_cast<int32_t>(len + sizeof(uint32_t)));
    writeInt32(len);
    if (len > 0)
    {
      memcpy(_pfree, str, len);
      _pfree += (len);
    }
  }

  void writeString(const std::string &str)
  {
    writeString(str.c_str());
  }

  void writeVector(const std::vector<int32_t> &v)
  {
    const uint32_t iLen = static_cast<uint32_t>(v.size());
    writeInt32(iLen);
    for (uint32_t i = 0; i < iLen; ++i)
    {
      writeInt32(v[i]);
    }
  }

  void writeVector(const std::vector<uint32_t> &v)
  {
    const uint32_t iLen = static_cast<uint32_t>(v.size());
    writeInt32(iLen);
    for (uint32_t i = 0; i < iLen; ++i)
    {
      writeInt32(v[i]);
    }
  }

  void writeVector(const std::vector<int64_t> &v)
  {
    const uint32_t iLen = static_cast<uint32_t>(v.size());
    writeInt32(iLen);
    for (uint32_t i = 0; i < iLen; ++i)
    {
      writeInt64(v[i]);
    }
  }

  void writeVector(const std::vector<uint64_t> &v)
  {
    const uint32_t iLen = static_cast<uint32_t>(v.size());
    writeInt32(iLen);
    for (uint32_t i = 0; i < iLen; ++i)
    {
      writeInt64(v[i]);
    }
  }

  uint8_t readInt8()
  {
    return (*_pdata++);
  }

  uint16_t readInt16()
  {
    uint16_t n = _pdata[0];
    n = static_cast<uint16_t>(n << 8);
    n = static_cast<uint16_t>(n | _pdata[1]);
    _pdata += 2;
    return n;
  }

  uint32_t readInt32()
  {
    uint32_t n = _pdata[0];
    n <<= 8;
    n |= _pdata[1];
    n <<= 8;
    n |= _pdata[2];
    n <<= 8;
    n |= _pdata[3];
    _pdata += 4;
    assert(_pfree >= _pdata);
    return n;
  }

  uint64_t readInt64()
  {
    uint64_t n = _pdata[0];
    n <<= 8;
    n |= _pdata[1];
    n <<= 8;
    n |= _pdata[2];
    n <<= 8;
    n |= _pdata[3];
    n <<= 8;
    n |= _pdata[4];
    n <<= 8;
    n |= _pdata[5];
    n <<= 8;
    n |= _pdata[6];
    n <<= 8;
    n |= _pdata[7];
    _pdata += 8;
    assert(_pfree >= _pdata);
    return n;
  }

  bool readBytes(void *dst, int32_t len)
  {
    if (_pdata + len > _pfree)
    {
      return false;
    }
    memcpy(dst, _pdata, len);
    _pdata += len;
    assert(_pfree >= _pdata);
    return true;
  }

  bool readString(char *&str, int32_t len)
  {
    if (_pdata + sizeof(int32_t) > _pfree)
    {
      return false;
    }
    int32_t slen = readInt32();
    if (_pfree - _pdata < slen)
    {
      slen = static_cast<int32_t>(_pfree - _pdata);
    }
    if (str == NULL && slen > 0)
    {
      str = (char *)malloc(slen);
      len = slen;
    }
    if (len > slen)
    {
      len = slen;
    }
    if (len > 0)
    {
      memcpy(str, _pdata, len);
      str[len - 1] = '\0';
    }
    _pdata += slen;
    assert(_pfree >= _pdata);
    return true;
  }

  bool readVector(std::vector<int32_t> &v)
  {
    const uint32_t len = readInt32();
    for (uint32_t i = 0; i < len; ++i)
    {
      v.push_back(readInt32());
    }
    return true;
  }

  bool readVector(std::vector<uint32_t> &v)
  {
    const uint32_t len = readInt32();
    for (uint32_t i = 0; i < len; ++i)
    {
      v.push_back(readInt32());
    }
    return true;
  }

  bool readVector(std::vector<int64_t> &v)
  {
    const uint32_t len = readInt32();
    for (uint32_t i = 0; i < len; ++i)
    {
      v.push_back(readInt64());
    }
    return true;
  }

  bool readVector(std::vector<uint64_t> &v)
  {
    const uint32_t len = readInt32();
    for (uint32_t i = 0; i < len; ++i)
    {
      v.push_back(readInt64());
    }
    return true;
  }

  void ensureFree(int32_t len)
  {
    expand(len);
  }

  int32_t findBytes(const char *findstr, int32_t len)
  {
    int32_t dLen = static_cast<int32_t>(_pfree - _pdata - len + 1);
    for (int32_t i = 0; i < dLen; i++)
    {
      if (_pdata[i] == findstr[0] && memcmp(_pdata + i, findstr, len) == 0)
      {
        return i;
      }
    }
    return -1;
  }

private:
  /*
   * expand
   */
  inline void expand(int32_t need)
  {
    if (_pstart == NULL)
    {
      int32_t len = 256;
      while (len < need)
      {
        len <<= 1;
      }
      _pfree = _pdata = _pstart = (unsigned char *)malloc(len);
      _pend = _pstart + len;
    }
    else if (_pend - _pfree < need)
    { // space not enough
      int32_t flen = static_cast<int32_t>((_pend - _pfree) + (_pdata - _pstart));
      int32_t dlen = static_cast<int32_t>(_pfree - _pdata);

      if (flen < need || flen * 4 < dlen)
      {
        int32_t bufsize = static_cast<int32_t>((_pend - _pstart) * 2);
        while (bufsize - dlen < need)
        {
          bufsize <<= 1;
        }

        unsigned char *newbuf = (unsigned char *)malloc(bufsize);
        if (newbuf == NULL)
        {
          PANIC("expand data buffer failed, length: %d", bufsize);
        }
        assert(newbuf != NULL);
        if (dlen > 0)
        {
          memcpy(newbuf, _pdata, dlen);
        }
        free(_pstart);

        _pdata = _pstart = newbuf;
        _pfree = _pstart + dlen;
        _pend = _pstart + bufsize;
      }
      else
      {
        memmove(_pstart, _pdata, dlen);
        _pfree = _pstart + dlen;
        _pdata = _pstart;
      }
    }
  }

private:
  unsigned char *_pstart;
  unsigned char *_pend;
  unsigned char *_pfree;
  unsigned char *_pdata;
};

class TCBuffer
{
public:
  TCBuffer()
      : _readPos(0),
        _writePos(0),
        _capacity(0),
        _buffer(NULL),
        _highWaterPercent(50)
  {
  }

  ~TCBuffer()
  {
    delete[] _buffer;
  }

private:
  TCBuffer(const TCBuffer &);
  void operator=(const TCBuffer &);

public:
  uint64_t PushData(const void *data, uint64_t size);
  void Produce(uint64_t bytes) { _writePos += bytes; }
  // deep copy, may < size
  uint64_t PopData(void *buf, uint64_t size);
  void PeekData(void *&buf, uint64_t &size);
  void Consume(uint64_t bytes);
  char *ReadAddr() { return &_buffer[_readPos]; }
  char *WriteAddr() { return &_buffer[_writePos]; }
  bool IsEmpty() const { return ReadableSize() == 0; }
  uint64_t ReadableSize() const { return _writePos - _readPos; }
  uint64_t WritableSize() const { return _capacity - _writePos; }
  uint64_t Capacity() const { return _capacity; }
  // if mem > 2 * size, try to free mem
  void Shrink();
  void Clear();
  void Swap(TCBuffer &buf);
  void AssureSpace(uint64_t size);
  // > Shrink do nothing, percents [10,100)
  void SetHighWaterPercent(uint64_t percents);

  static const uint64_t kMaxBufferSize;
  static const uint64_t kDefaultSize;

private:
  void ResetBuffer(void *ptr = NULL);

  uint64_t _readPos;
  uint64_t _writePos;
  uint64_t _capacity;
  char *_buffer;
  // Shrink()
  uint64_t _highWaterPercent;
};

struct TCSlice
{
  explicit TCSlice(void *d = NULL, uint64_t ds = 0, uint64_t l = 0);
  void *data;
  uint64_t dataLen;
  uint64_t len;
};

class TCBufferPool
{
public:
  TCBufferPool(uint64_t minBlock, uint64_t maxBlock);
  ~TCBufferPool();

  TCSlice Allocate(uint64_t size);
  void Deallocate(TCSlice s);
  void SetMaxBytes(uint64_t bytes);
  uint64_t GetMaxBytes() const;
  string DebugPrint() const;

private:
  typedef std::list<void *> BufferList;

  TCSlice _Allocate(uint64_t size, BufferList &blist);
  // find one
  BufferList &_GetBufferList(uint64_t s);
  const BufferList &_GetBufferList(uint64_t s) const;

  std::vector<BufferList> _buffers;
  const uint64_t _minBlock;
  const uint64_t _maxBlock;
  uint64_t _maxBytes;
  // current bufferpool mem
  uint64_t _totalBytes;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BUFFER_H_