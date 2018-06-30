
#ifndef MYCC_UTIL_BUFFER_UTIL_H_
#define MYCC_UTIL_BUFFER_UTIL_H_

#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

template <int SIZE>
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

class AutoBuffer
{
public:
  enum TSeek
  {
    ESeekStart,
    ESeekCur,
    ESeekEnd,
  };

public:
  explicit AutoBuffer(uint64_t _size = 128);
  explicit AutoBuffer(void *_pbuffer, uint64_t _len, uint64_t _size = 128);
  explicit AutoBuffer(const void *_pbuffer, uint64_t _len, uint64_t _size = 128);
  ~AutoBuffer();

  void AllocWrite(uint64_t _readytowrite, bool _changelength = true);
  void AddCapacity(uint64_t _len);

  template <class T>
  void Write(const T &_val)
  {
    Write(&_val, sizeof(_val));
  }

  template <class T>
  void Write(int64_t &_pos, const T &_val)
  {
    Write(_pos, &_val, sizeof(_val));
  }

  template <class T>
  void Write(const int64_t &_pos, const T &_val)
  {
    Write(_pos, &_val, sizeof(_val));
  }

  void Write(const char *const _val)
  {
    Write(_val, strlen(_val));
  }

  void Write(int64_t &_pos, const char *const _val)
  {
    Write(_pos, _val, strlen(_val));
  }

  void Write(const int64_t &_pos, const char *const _val)
  {
    Write(_pos, _val, strlen(_val));
  }

  void Write(const AutoBuffer &_buffer);
  void Write(const void *_pbuffer, uint64_t _len);
  void Write(int64_t &_pos, const AutoBuffer &_buffer);
  void Write(int64_t &_pos, const void *_pbuffer, uint64_t _len);
  void Write(const int64_t &_pos, const AutoBuffer &_buffer);
  void Write(const int64_t &_pos, const void *_pbuffer, uint64_t _len);
  void Write(TSeek _seek, const void *_pbuffer, uint64_t _len);

  template <class T>
  uint64_t Read(T &_val)
  {
    return Read(&_val, sizeof(_val));
  }

  template <class T>
  uint64_t Read(int64_t &_pos, T &_val) const
  {
    return Read(_pos, &_val, sizeof(_val));
  }

  template <class T>
  uint64_t Read(const int64_t &_pos, T &_val) const
  {
    return Read(_pos, &_val, sizeof(_val));
  }

  uint64_t Read(void *_pbuffer, uint64_t _len);
  uint64_t Read(AutoBuffer &_rhs, uint64_t _len);

  uint64_t Read(int64_t &_pos, void *_pbuffer, uint64_t _len) const;
  uint64_t Read(int64_t &_pos, AutoBuffer &_rhs, uint64_t _len) const;

  uint64_t Read(const int64_t &_pos, void *_pbuffer, uint64_t _len) const;
  uint64_t Read(const int64_t &_pos, AutoBuffer &_rhs, uint64_t _len) const;

  int64_t Move(int64_t _move_len);

  void Seek(int64_t _offset, TSeek _eorigin);
  void Length(int64_t _pos, uint64_t _lenght);

  void *Ptr(int64_t _offset = 0);
  void *PosPtr();
  const void *Ptr(int64_t _offset = 0) const;
  const void *PosPtr() const;

  int64_t Pos() const;
  uint64_t PosLength() const;
  uint64_t Length() const;
  uint64_t Capacity() const;

  void Attach(void *_pbuffer, uint64_t _len);
  void Attach(AutoBuffer &_rhs);
  void *Detach(uint64_t *_plen = NULL);

  void Reset();

private:
  void __FitSize(uint64_t _len);

private:
  unsigned char *parray_;
  int64_t pos_;
  uint64_t length_;
  uint64_t capacity_;
  uint64_t malloc_unitsize_;

  DISALLOW_COPY_AND_ASSIGN(AutoBuffer);
};

extern const AutoBuffer KNullAtuoBuffer;

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BUFFER_UTIL_H_