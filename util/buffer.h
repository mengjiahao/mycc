
#ifndef MYCC_UTIL_BUFFER_H_
#define MYCC_UTIL_BUFFER_H_

#include "env_util.h"
#include "coding_util.h"
#include "math_util.h"
#include "status.h"
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

// An InputBuffer provides a buffer on top of a RandomAccessFile.
// A given instance of an InputBuffer is NOT safe for concurrent use
// by multiple threads
class InputBuffer
{
public:
  // Create an InputBuffer for "file" with a buffer size of
  // "buffer_bytes" bytes.  'file' must outlive *this.
  InputBuffer(RandomAccessFile *file, uint64_t buffer_bytes);
  ~InputBuffer();

  // Read one text line of data into "*result" until end-of-file or a
  // \n is read.  (The \n is not included in the result.)  Overwrites
  // any existing data in *result.
  //
  // If successful, returns OK.  If we are already at the end of the
  // file, we return an OUT_OF_RANGE error.  Otherwise, we return
  // some other non-OK status.
  Status ReadLine(string *result);

  // Reads bytes_to_read bytes into *result, overwriting *result.
  //
  // If successful, returns OK.  If we there are not enough bytes to
  // read before the end of the file, we return an OUT_OF_RANGE error.
  // Otherwise, we return some other non-OK status.
  Status ReadNBytes(int64_t bytes_to_read, string *result);

  // An overload that writes to char*.  Caller must ensure result[0,
  // bytes_to_read) is valid to be overwritten.  Returns OK iff "*bytes_read ==
  // bytes_to_read".
  Status ReadNBytes(int64_t bytes_to_read, char *result, uint64_t *bytes_read);

  // Reads a single varint32.
  Status ReadVarint32(uint32_t *result);

  // Like ReadNBytes() without returning the bytes read.
  Status SkipNBytes(int64_t bytes_to_skip);

  // Seek to this offset within the file.
  //
  // If we seek to somewhere within our pre-buffered data, we will re-use what
  // data we can.  Otherwise, Seek() throws out the current buffer and the next
  // read will trigger a File::Read().
  Status Seek(int64_t position);

  // Returns the position in the file.
  int64_t Tell() const { return file_pos_ - (limit_ - pos_); }

  // Returns the underlying RandomAccessFile.
  RandomAccessFile *file() const { return file_; }

private:
  Status FillBuffer();

  // Internal slow-path routine used by ReadVarint32().
  Status ReadVarint32Fallback(uint32_t *result);

  RandomAccessFile *file_; // Not owned
  int64_t file_pos_;       // Next position to read from in "file_"
  uint64_t size_;          // Size of "buf_"
  char *buf_;              // The buffer itself
  // [pos_,limit_) hold the "limit_ - pos_" bytes just before "file_pos_"
  char *pos_;   // Current position in "buf"
  char *limit_; // Just past end of valid data in "buf"

  DISALLOW_COPY_AND_ASSIGN(InputBuffer);
};

// Implementation details.

// Inlined for performance.
inline Status InputBuffer::ReadVarint32(uint32_t *result)
{
  if (pos_ + kMaxVarint32Bytes <= limit_)
  {
    // Fast path: directly parse from buffered data.
    // Reads strictly from the range [pos_, limit_).
    const char *offset = GetVarint32Ptr(pos_, limit_, result);
    if (offset == nullptr)
      return Status::Error("Parsed past limit.");
    pos_ = const_cast<char *>(offset);
    return Status::OK();
  }
  else
  {
    return ReadVarint32Fallback(result);
  }
}

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

#endif // MYCC_UTIL_BUFFER_H_