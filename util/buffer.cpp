
#include "buffer.h"
#include <stdlib.h>
#include <algorithm>
#include <iostream>
#include <limits>
#include <iomanip>
#include <sstream>
#include "error_util.h"
#include "math_util.h"

namespace mycc
{
namespace util
{

/////////////////////// InputBuffer //////////////////////////////

InputBuffer::InputBuffer(RandomAccessFile *file, uint64_t buffer_bytes)
    : file_(file),
      file_pos_(0),
      size_(buffer_bytes),
      buf_(new char[size_]),
      pos_(buf_),
      limit_(buf_) {}

InputBuffer::~InputBuffer() { delete[] buf_; }

Status InputBuffer::FillBuffer()
{
  StringPiece data;
  Status s = file_->Read(file_pos_, size_, &data, buf_);
  if (data.data() != buf_)
  {
    memmove(buf_, data.data(), data.size());
  }
  pos_ = buf_;
  limit_ = pos_ + data.size();
  file_pos_ += data.size();
  return s;
}

Status InputBuffer::ReadLine(string *result)
{
  result->clear();
  Status s;
  do
  {
    uint64_t buf_remain = limit_ - pos_;
    char *newline = static_cast<char *>(memchr(pos_, '\n', buf_remain));
    if (newline != nullptr)
    {
      uint64_t result_len = newline - pos_;
      result->append(pos_, result_len);
      pos_ = newline + 1;
      if (!result->empty() && result->back() == '\r')
      {
        result->resize(result->size() - 1);
      }
      return Status::OK();
    }
    if (buf_remain > 0)
      result->append(pos_, buf_remain);
    // Get more data into buffer
    s = FillBuffer();
    assert(pos_ == buf_);
  } while (limit_ != buf_);
  if (!result->empty() && result->back() == '\r')
  {
    result->resize(result->size() - 1);
  }
  if (s.ok() && !result->empty())
  {
    return Status::OK();
  }
  return s;
}

Status InputBuffer::ReadNBytes(int64_t bytes_to_read, string *result)
{
  result->clear();
  if (bytes_to_read < 0)
  {
    return Status::Error("Can't read a negative number of bytes: ");
  }
  result->resize(bytes_to_read);
  uint64_t bytes_read = 0;
  Status status = ReadNBytes(bytes_to_read, &(*result)[0], &bytes_read);
  if (static_cast<int64_t>(bytes_read) < bytes_to_read)
    result->resize(bytes_read);
  return status;
}

Status InputBuffer::ReadNBytes(int64_t bytes_to_read, char *result,
                               uint64_t *bytes_read)
{
  if (bytes_to_read < 0)
  {
    return Status::Error("Can't read a negative number of bytes: ");
  }
  Status status;
  *bytes_read = 0;
  while (*bytes_read < static_cast<uint64_t>(bytes_to_read))
  {
    if (pos_ == limit_)
    {
      // Get more data into buffer.
      status = FillBuffer();
      if (limit_ == buf_)
      {
        break;
      }
    }
    // Do not go over the buffer boundary.
    const int64_t bytes_to_copy =
        MATH_MIN((int64_t)(limit_ - pos_), (int64_t)(bytes_to_read - *bytes_read));
    // Copies buffered data into the destination.
    memcpy(result + *bytes_read, pos_, bytes_to_copy);
    pos_ += bytes_to_copy;
    *bytes_read += bytes_to_copy;
  }
  if (status.ok() &&
      (*bytes_read == static_cast<uint64_t>(bytes_to_read)))
  {
    return Status::OK();
  }
  return status;
}

Status InputBuffer::ReadVarint32Fallback(uint32_t *result)
{
  uint8_t scratch = 0;
  char *p = reinterpret_cast<char *>(&scratch);
  uint64_t unused_bytes_read = 0;

  *result = 0;
  for (int shift = 0; shift <= 28; shift += 7)
  {
    RETURN_IF_ERROR(ReadNBytes(1, p, &unused_bytes_read));
    *result |= (scratch & 127) << shift;
    if (!(scratch & 128))
      return Status::OK();
  }
  return Status::Error("Stored data is too large to be a varint32.");
}

Status InputBuffer::SkipNBytes(int64_t bytes_to_skip)
{
  if (bytes_to_skip < 0)
  {
    return Status::Error("Can only skip forward, not ");
  }
  int64_t bytes_skipped = 0;
  Status s;
  while (bytes_skipped < bytes_to_skip)
  {
    if (pos_ == limit_)
    {
      // Get more data into buffer
      s = FillBuffer();
      if (limit_ == buf_)
      {
        break;
      }
    }
    const int64_t bytes_to_advance =
        MATH_MIN(limit_ - pos_, bytes_to_skip - bytes_skipped);
    bytes_skipped += bytes_to_advance;
    pos_ += bytes_to_advance;
  }
  if (s.ok() && bytes_skipped == bytes_to_skip)
  {
    return Status::OK();
  }
  return s;
}

Status InputBuffer::Seek(int64_t position)
{
  if (position < 0)
  {
    return Status::Error("Seeking to a negative position: ");
  }
  // Position of the buffer within file.
  const int64_t bufpos = file_pos_ - static_cast<int64_t>(limit_ - buf_);
  if (position >= bufpos && position < file_pos_)
  {
    // Seeks to somewhere inside the buffer.
    pos_ = buf_ + (position - bufpos);
    assert(pos_ >= buf_ && pos_ < limit_);
  }
  else
  {
    // Seeks to somewhere outside.  Discards the buffered data.
    pos_ = limit_ = buf_;
    file_pos_ = position;
  }
  return Status::OK();
}

////////////////////// TCBuffer /////////////////////////

const uint64_t TCBuffer::kMaxBufferSize = std::numeric_limits<uint64_t>::max() / 2;
const uint64_t TCBuffer::kDefaultSize = 128;

uint64_t TCBuffer::PushData(const void *data, uint64_t size)
{
  if (!data || size == 0)
    return 0;
  if (ReadableSize() + size >= kMaxBufferSize)
    return 0; // overflow

  AssureSpace(size);
  ::memcpy(&_buffer[_writePos], data, size);
  Produce(size);
  return size;
}

uint64_t TCBuffer::PopData(void *buf, uint64_t size)
{
  const uint64_t dataSize = ReadableSize();
  if (!buf || size == 0 || dataSize == 0)
    return 0;

  if (size > dataSize)
    size = dataSize; // truncate

  ::memcpy(buf, &_buffer[_readPos], size);
  Consume(size);
  return size;
}

void TCBuffer::PeekData(void *&buf, uint64_t &size)
{
  buf = ReadAddr();
  size = ReadableSize();
}

void TCBuffer::Consume(uint64_t bytes)
{
  assert(_readPos + bytes <= _writePos);
  _readPos += bytes;
  if (IsEmpty())
    Clear();
}

void TCBuffer::AssureSpace(uint64_t needsize)
{
  if (WritableSize() >= needsize)
    return;

  const uint64_t dataSize = ReadableSize();
  const uint64_t oldCap = _capacity;

  while (WritableSize() + _readPos < needsize)
  {
    if (_capacity < kDefaultSize)
    {
      _capacity = kDefaultSize;
    }
    else if (_capacity <= kMaxBufferSize)
    {
      const uint64_t newCapcity = RoundupPower2(_capacity);
      if (_capacity < newCapcity)
        _capacity = newCapcity;
      else
        _capacity = 2 * newCapcity;
    }
    else
    {
      assert(false);
    }
  }

  if (oldCap < _capacity)
  {
    char *tmp(new char[_capacity]);
    if (dataSize != 0)
    {
      memcpy(&tmp[0], &_buffer[_readPos], dataSize);
    }
    ResetBuffer(tmp);
  }
  else
  {
    assert(_readPos > 0);
    ::memmove(&_buffer[0], &_buffer[_readPos], dataSize);
  }

  _readPos = 0;
  _writePos = dataSize;
  assert(needsize <= WritableSize());
}

void TCBuffer::Shrink()
{
  if (IsEmpty())
  {
    Clear();
    _capacity = 0;
    ResetBuffer();
    return;
  }

  if (_capacity <= kDefaultSize)
  {
    return;
  }

  uint64_t oldCap = _capacity;
  uint64_t dataSize = ReadableSize();
  if (dataSize * 100 > oldCap * _highWaterPercent)
  {
    return;
  }

  uint64_t newCap = RoundupPower2(dataSize);

  char *tmp(new char[newCap]);
  memcpy(&tmp[0], &_buffer[_readPos], dataSize);
  ResetBuffer(tmp);
  _capacity = newCap;
  _readPos = 0;
  _writePos = dataSize;
}

void TCBuffer::Clear()
{
  _readPos = _writePos = 0;
}

void TCBuffer::Swap(TCBuffer &buf)
{
  std::swap(_readPos, buf._readPos);
  std::swap(_writePos, buf._writePos);
  std::swap(_capacity, buf._capacity);
  std::swap(_buffer, buf._buffer);
}

void TCBuffer::ResetBuffer(void *ptr)
{
  delete[] _buffer;
  _buffer = reinterpret_cast<char *>(ptr);
}

void TCBuffer::SetHighWaterPercent(uint64_t percents)
{
  if (percents < 10 || percents >= 100)
  {
    return;
  }
  _highWaterPercent = percents;
}

TCSlice::TCSlice(void *d, uint64_t dl, uint64_t l)
    : data(d),
      dataLen(dl),
      len(l)
{
}

TCBufferPool::TCBufferPool(uint64_t minBlock, uint64_t maxBlock)
    : _minBlock(RoundupPower2(minBlock)),
      _maxBlock(RoundupPower2(maxBlock)),
      _maxBytes(1024 * 1024),
      _totalBytes(0)
{
  uint64_t listCount = 0;
  uint64_t testVal = _minBlock;
  while (testVal <= _maxBlock)
  {
    testVal *= 2;
    ++listCount;
  }

  assert(listCount > 0);
  _buffers.resize(listCount);
}

TCBufferPool::~TCBufferPool()
{
  std::vector<BufferList>::iterator it(_buffers.begin());
  for (; it != _buffers.end(); ++it)
  {
    BufferList &blist = *it;
    BufferList::iterator bit(blist.begin());
    for (; bit != blist.end(); ++bit)
    {
      //delete[] (*bit);
      delete[] reinterpret_cast<char *>(*bit);
    }
  }
}

TCSlice TCBufferPool::Allocate(uint64_t size)
{
  TCSlice s;
  size = RoundupPower2(size);
  if (size == 0)
    return s;

  if (size < _minBlock || size > _maxBlock)
  {
    // not managed by pool, directly new
    s.data = new char[size];
    s.len = size;
  }
  else
  {
    BufferList &blist = _GetBufferList(size);
    s = _Allocate(size, blist);
  }

  return s;
}

void TCBufferPool::Deallocate(TCSlice s)
{
  if (s.len < _minBlock || s.len > _maxBlock)
  {
    // not managed by pool, directly delete
    delete[] reinterpret_cast<char *>(s.data);
  }
  else if (_totalBytes >= _maxBytes)
  {
    // use too more, directly delete
    delete[] reinterpret_cast<char *>(s.data);
  }
  else
  {
    // free to pool
    BufferList &blist = _GetBufferList(s.len);
    blist.push_back(s.data);
    _totalBytes += s.len;
  }
}

void TCBufferPool::SetMaxBytes(uint64_t bytes)
{
  _maxBytes = bytes;
}

uint64_t TCBufferPool::GetMaxBytes() const
{
  return _maxBytes;
}

string TCBufferPool::DebugPrint() const
{
  std::ostringstream oss;

  oss << "\n===============================================================\n";
  oss << "============  BucketCount " << std::setiosflags(std::ios::left) << std::setw(4) << _buffers.size() << " ================================" << std::endl;
  oss << "============  PoolBytes " << std::setw(10) << _totalBytes << " ============================" << std::endl;

  int bucket = 0;
  uint64_t size = _minBlock;
  std::vector<BufferList>::const_iterator it(_buffers.begin());
  for (; it != _buffers.end(); ++it)
  {
    const BufferList &blist = *it;
    oss << "== Bucket " << std::setw(3) << bucket
        << ": BlockSize " << std::setw(8) << size
        << " Remain blocks " << std::setw(6) << blist.size()
        << " ======== \n";

    ++bucket;
    size *= 2;
  }

  return oss.str();
}

TCSlice TCBufferPool::_Allocate(uint64_t size, BufferList &blist)
{
  assert((size & (size - 1)) == 0);
  TCSlice s;
  s.len = size;

  if (blist.empty())
  {
    s.data = new char[size];
  }
  else
  {
    s.data = *blist.begin();
    blist.pop_front();
    _totalBytes -= s.len;
  }
  return s;
}

TCBufferPool::BufferList &TCBufferPool::_GetBufferList(uint64_t s)
{
  const BufferList &blist = const_cast<const TCBufferPool &>(*this)._GetBufferList(s);
  return const_cast<BufferList &>(blist);
}

const TCBufferPool::BufferList &TCBufferPool::_GetBufferList(uint64_t s) const
{
  assert((s & (s - 1)) == 0);
  assert(s >= _minBlock && s <= _maxBlock);

  uint64_t index = _buffers.size();
  uint64_t testVal = s;
  while (testVal <= _maxBlock)
  {
    testVal *= 2;
    index--;
  }
  return _buffers[index];
}

} // namespace util
} // namespace mycc