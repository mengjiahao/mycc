
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
  void add(size_t len) { cur_ += len; }

  void reset() { cur_ = data_; }
  void bzero() { ::bzero(data_, sizeof data_); }

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BUFFER_UTIL_H_