
// Slice is a simple structure containing a pointer into some external
// storage and a size.  The user of a Slice must ensure that the slice
// is not used after the corresponding external storage has been
// deallocated.
//
// Multiple threads can invoke const methods on a Slice without
// external synchronization, but if any of the threads may call a
// non-const method, all threads accessing the same Slice must use
// external synchronization.

#ifndef MYCC_UTIL_STRINGPIECE_H_
#define MYCC_UTIL_STRINGPIECE_H_

#include <assert.h>
#include <stdarg.h>
#include <string.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

// also known as Slice
class StringPiece
{
public:
  static const uint64_t npos;

  static void AppendStringPieceTo(string *str, const StringPiece &value)
  {
    str->append(value.data(), value.size());
  }

  // Create an empty slice.
  StringPiece() : data_(""), size_(0) {}

  // Create a slice that refers to d[0,n-1].
  StringPiece(const char *d, uint64_t n) : data_(d), size_(n) {}

  // Create a slice that refers to the contents of "s"
  StringPiece(const string &s) : data_(s.data()), size_(s.size()) {}

  // Create a slice that refers to s[0,strlen(s)-1], c_string
  StringPiece(const char *s) : data_(s), size_(strlen(s)) {}

  void set(const void *data, uint64_t len)
  {
    data_ = reinterpret_cast<const char *>(data);
    size_ = len;
  }

  // Return a pointer to the beginning of the referenced data
  const char *data() const { return data_; }

  // Return the length (in bytes) of the referenced data
  uint64_t size() const { return size_; }
  uint64_t length() const { return size_; }

  // Return true iff the length of the referenced data is zero
  bool empty() const { return size_ == 0; }

  typedef const char *const_iterator;
  typedef const char *iterator;
  iterator begin() const { return data_; }
  iterator end() const { return data_ + size_; }

  string as_string() const
  {
    // string doesn't like to take a NULL pointer even with a 0 size.
    return empty() ? string() : string(data(), size());
  }

  const char *c_str() const
  {
    assert(data_[size_] == 0);
    return data_;
  }

  // Return a string that contains the copy of a suffix of the referenced data.
  string substr_copy(uint64_t start) const
  {
    assert(start <= size());
    return string(data_ + start, size_ - start);
  }

  // Return the ith byte in the referenced data.
  // REQUIRES: n < size()
  char operator[](uint64_t n) const
  {
    assert(n < size());
    return data_[n];
  }

  // Change this slice to refer to an empty array
  void clear()
  {
    data_ = "";
    size_ = 0;
  }

  // Drop the first "n" bytes from this slice.
  void remove_prefix(uint64_t n)
  {
    assert(n <= size());
    data_ += n;
    size_ -= n;
  }

  void remove_suffix(uint64_t n)
  {
    assert(size_ >= n);
    size_ -= n;
  }

  // Return true iff "x" is a prefix of "*this"
  bool starts_with(StringPiece x) const
  {
    return ((size_ >= x.size_) && (memcmp(data_, x.data_, x.size_) == 0));
  }
  // Return true iff "x" is a suffix of "*this"
  bool ends_with(StringPiece x) const
  {
    return ((size_ >= x.size_) &&
            (memcmp(data_ + (size_ - x.size_), x.data_, x.size_) == 0));
  }

  // Checks whether StringPiece starts with x and if so advances the beginning
  // of it to past the match.  It's basically a shortcut for starts_with
  // followed by remove_prefix.
  bool consume(StringPiece x)
  {
    if (starts_with(x))
    {
      remove_prefix(x.size_);
      return true;
    }
    return false;
  }

  uint64_t find(char c, uint64_t pos = 0) const;
  uint64_t rfind(char c, uint64_t pos = npos) const;
  bool contains(StringPiece s) const;

  StringPiece substr(uint64_t pos, uint64_t n = npos) const;

  // Three-way comparison.  Returns value:
  //   <  0 iff "*this" <  "b",
  //   == 0 iff "*this" == "b",
  //   >  0 iff "*this" >  "b"
  int32_t compare(StringPiece b) const;

  // Return a string that contains the copy of the referenced data.
  // when hex is true, returns a string of twice the length hex encoded (0-9A-F)
  string toString(bool hex = false) const;

  // Decodes the current slice interpreted as an hexadecimal string into result,
  // if successful returns true, if this isn't a valid hex string
  // (e.g not coming from Slice::ToString(true)) DecodeHex returns false.
  // This slice is expected to have an even number of 0-9A-F characters
  // also accepts lowercase (a-f)
  bool decodeHex(string *result) const;

  // Compare two slices and returns the first byte where they differ
  uint64_t difference_offset(const StringPiece b) const;

  struct Hasher
  {
    std::size_t operator()(StringPiece arg) const;
  };

public:
  // private: make these public for rocksdbjni access
  const char *data_;
  uint64_t size_;

  // Intentionally copyable.
  StringPiece(const StringPiece &) = default;
  StringPiece &operator=(const StringPiece &) = default;
};

inline bool operator==(StringPiece x, StringPiece y)
{
  return ((x.size() == y.size()) &&
          (memcmp(x.data(), y.data(), x.size()) == 0));
}

inline bool operator!=(StringPiece x, StringPiece y) { return !(x == y); }

inline bool operator<(StringPiece x, StringPiece y) { return x.compare(y) < 0; }
inline bool operator>(StringPiece x, StringPiece y) { return x.compare(y) > 0; }
inline bool operator<=(StringPiece x, StringPiece y)
{
  return x.compare(y) <= 0;
}
inline bool operator>=(StringPiece x, StringPiece y)
{
  return x.compare(y) >= 0;
}

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STRINGPIECE_H_