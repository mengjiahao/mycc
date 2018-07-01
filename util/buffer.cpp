
#include "buffer.h"
#include <stdlib.h>
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
  Status s = file_->read(file_pos_, size_, &data, buf_);
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
        MATH_MIN(limit_ - pos_, bytes_to_read - *bytes_read);
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

/////////////////////// AutoBuffer //////////////////////////////

AutoBuffer::AutoBuffer(uint64_t _nSize)
    : parray_(NULL), pos_(0), length_(0), capacity_(0), malloc_unitsize_(_nSize)
{
}

AutoBuffer::AutoBuffer(void *_pbuffer, uint64_t _len, uint64_t _nSize)
    : parray_(NULL), pos_(0), length_(0), capacity_(0), malloc_unitsize_(_nSize)
{
  Attach(_pbuffer, _len);
}

AutoBuffer::AutoBuffer(const void *_pbuffer, uint64_t _len, uint64_t _nSize)
    : parray_(NULL), pos_(0), length_(0), capacity_(0), malloc_unitsize_(_nSize)
{
  Write(0, _pbuffer, _len);
}

AutoBuffer::~AutoBuffer()
{
  Reset();
}

void AutoBuffer::AllocWrite(uint64_t _readytowrite, bool _changelength)
{
  uint64_t nLen = Pos() + _readytowrite;
  __FitSize(nLen);

  if (_changelength)
    length_ = MATH_MAX(nLen, length_);
}

void AutoBuffer::AddCapacity(uint64_t _len)
{
  __FitSize(Capacity() + _len);
}

void AutoBuffer::Write(const AutoBuffer &_buffer)
{
  Write(_buffer.Ptr(), _buffer.Length());
}

void AutoBuffer::Write(const void *_pbuffer, uint64_t _len)
{
  Write(Pos(), _pbuffer, _len);
  Seek(_len, ESeekCur);
}

void AutoBuffer::Write(int64_t &_pos, const AutoBuffer &_buffer)
{
  Write((const int64_t &)_pos, _buffer.Ptr(), _buffer.Length());
  _pos += _buffer.Length();
}

void AutoBuffer::Write(int64_t &_pos, const void *_pbuffer, uint64_t _len)
{
  Write((const int64_t &)_pos, _pbuffer, _len);
  _pos += _len;
}

void AutoBuffer::Write(const int64_t &_pos, const AutoBuffer &_buffer)
{
  Write((const int64_t &)_pos, _buffer.Ptr(), _buffer.Length());
}

void AutoBuffer::Write(const int64_t &_pos, const void *_pbuffer, uint64_t _len)
{
  ASSERT(NULL != _pbuffer || 0 == _len);
  ASSERT(0 <= _pos);
  ASSERT((uint64_t)_pos <= Length());
  uint64_t nLen = _pos + _len;
  __FitSize(nLen);
  length_ = MATH_MAX(nLen, length_);
  memcpy((unsigned char *)Ptr() + _pos, _pbuffer, _len);
}

void AutoBuffer::Write(TSeek _seek, const void *_pbuffer, uint64_t _len)
{
  int64_t pos = 0;
  switch (_seek)
  {
  case ESeekStart:
    pos = 0;
    break;
  case ESeekCur:
    pos = pos_;
    break;
  case ESeekEnd:
    pos = length_;
    break;
  default:
    ASSERT(false);
    break;
  }

  Write(pos, _pbuffer, _len);
}

uint64_t AutoBuffer::Read(void *_pbuffer, uint64_t _len)
{
  uint64_t readlen = Read(Pos(), _pbuffer, _len);
  Seek(readlen, ESeekCur);
  return readlen;
}

uint64_t AutoBuffer::Read(AutoBuffer &_rhs, uint64_t _len)
{
  uint64_t readlen = Read(Pos(), _rhs, _len);
  Seek(readlen, ESeekCur);
  return readlen;
}

uint64_t AutoBuffer::Read(int64_t &_pos, void *_pbuffer, uint64_t _len) const
{
  uint64_t readlen = Read((const int64_t &)_pos, _pbuffer, _len);
  _pos += readlen;
  return readlen;
}

uint64_t AutoBuffer::Read(int64_t &_pos, AutoBuffer &_rhs, uint64_t _len) const
{
  uint64_t readlen = Read((const int64_t &)_pos, _rhs, _len);
  _pos += readlen;
  return readlen;
}

uint64_t AutoBuffer::Read(const int64_t &_pos, void *_pbuffer, uint64_t _len) const
{
  ASSERT(NULL != _pbuffer);
  ASSERT(0 <= _pos);
  ASSERT((uint64_t)_pos <= Length());

  uint64_t readlen = Length() - _pos;
  readlen = MATH_MIN(readlen, _len);
  memcpy(_pbuffer, PosPtr(), readlen);
  return readlen;
}

uint64_t AutoBuffer::Read(const int64_t &_pos, AutoBuffer &_rhs, uint64_t _len) const
{
  uint64_t readlen = Length() - _pos;
  readlen = MATH_MIN(readlen, _len);
  _rhs.Write(PosPtr(), readlen);
  return readlen;
}

int64_t AutoBuffer::Move(int64_t _move_len)
{
  if (0 < _move_len)
  {
    __FitSize(Length() + _move_len);
    memmove(parray_ + _move_len, parray_, Length());
    memset(parray_, 0, _move_len);
    Length(Pos() + _move_len, Length() + _move_len);
  }
  else
  {
    uint64_t move_len = -_move_len;

    if (move_len > Length())
      move_len = Length();

    memmove(parray_, parray_ + move_len, Length() - move_len);
    Length(move_len < (uint64_t)Pos() ? Pos() - move_len : 0, Length() - move_len);
  }

  return Length();
}

void AutoBuffer::Seek(int64_t _offset, TSeek _eorigin)
{
  switch (_eorigin)
  {
  case ESeekStart:
    pos_ = _offset;
    break;

  case ESeekCur:
    pos_ += _offset;
    break;

  case ESeekEnd:
    pos_ = length_ + _offset;
    break;

  default:
    ASSERT(false);
    break;
  }

  if (pos_ < 0)
    pos_ = 0;

  if ((uint64_t)pos_ > length_)
    pos_ = length_;
}

void AutoBuffer::Length(int64_t _pos, uint64_t _lenght)
{
  ASSERT(0 <= _pos);
  ASSERT((uint64_t)_pos <= _lenght);
  ASSERT(_lenght <= Capacity());
  length_ = _lenght;
  Seek(_pos, ESeekStart);
}

void *AutoBuffer::Ptr(int64_t _offset)
{
  return (char *)parray_ + _offset;
}

const void *AutoBuffer::Ptr(int64_t _offset) const
{
  return (const char *)parray_ + _offset;
}

void *AutoBuffer::PosPtr()
{
  return ((unsigned char *)Ptr()) + Pos();
}

const void *AutoBuffer::PosPtr() const
{
  return ((unsigned char *)Ptr()) + Pos();
}

int64_t AutoBuffer::Pos() const
{
  return pos_;
}

uint64_t AutoBuffer::PosLength() const
{
  return length_ - pos_;
}

uint64_t AutoBuffer::Length() const
{
  return length_;
}

uint64_t AutoBuffer::Capacity() const
{
  return capacity_;
}

void AutoBuffer::Attach(void *_pbuffer, uint64_t _len)
{
  Reset();
  parray_ = (unsigned char *)_pbuffer;
  length_ = _len;
  capacity_ = _len;
}

void AutoBuffer::Attach(AutoBuffer &_rhs)
{
  Reset();
  parray_ = _rhs.parray_;
  pos_ = _rhs.pos_;
  length_ = _rhs.length_;
  capacity_ = _rhs.capacity_;

  _rhs.parray_ = NULL;
  _rhs.Reset();
}

void *AutoBuffer::Detach(uint64_t *_plen)
{
  unsigned char *ret = parray_;
  parray_ = NULL;
  uint64_t nLen = Length();

  if (NULL != _plen)
    *_plen = nLen;

  Reset();
  return ret;
}

void AutoBuffer::Reset()
{
  if (NULL != parray_)
    free(parray_);

  parray_ = NULL;
  pos_ = 0;
  length_ = 0;
  capacity_ = 0;
}

void AutoBuffer::__FitSize(uint64_t _len)
{
  if (_len > capacity_)
  {
    uint64_t mallocsize = ((_len + malloc_unitsize_ - 1) / malloc_unitsize_) * malloc_unitsize_;

    void *p = realloc(parray_, mallocsize);

    if (NULL == p)
    {
      //ASSERT2(p, "_len=%" PRIu64 ", m_nMallocUnitSize=%" PRIu64 ", nMallocSize=%" PRIu64", m_nCapacity=%" PRIu64,
      //        (uint64_t)_len, (uint64_t)malloc_unitsize_, (uint64_t)mallocsize, (uint64_t)capacity_);
      free(parray_);
    }

    parray_ = (unsigned char *)p;

    //ASSERT2(_len <= 10 * 1024 * 1024, "%u", (uint32_t)_len);
    ASSERT(parray_);

    memset(parray_ + capacity_, 0, mallocsize - capacity_);
    capacity_ = mallocsize;
  }
}

} // namespace util
} // namespace mycc