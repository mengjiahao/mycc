
#ifndef MYCC_UTIL_BUFFER_H_
#define MYCC_UTIL_BUFFER_H_

#include <list>
#include <vector>
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