
#ifndef MYCC_UTIL_STRING_UTIL_H_
#define MYCC_UTIL_STRING_UTIL_H_

#include <stdarg.h>
#include <string.h>
#include <vector>
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

extern const string kNullptrString;

/// string ascii
inline bool IsAsciiVisible(char c)
{
  return (c >= 0x20 && c <= 0x7E);
}

inline bool IsAsciiWhitespace(char c)
{
  return c == ' ' || c == '\r' || c == '\n' || c == '\t';
}

inline bool IsAsciiAlnum(char c)
{
  return ('a' <= c && c <= 'z') ||
         ('A' <= c && c <= 'Z') ||
         ('0' <= c && c <= '9');
}

inline bool IsAsciiDigit(char c)
{
  return ('0' <= c && c <= '9');
}

inline bool IsAsciiSpace(char c)
{
  return c == ' ' || c == '\t' || c == '\n' ||
         c == '\v' || c == '\f' || c == '\r';
}

inline bool IsAsciiUpper(char c)
{
  return c >= 'A' && c <= 'Z';
}

inline bool IsAsciiLower(char c)
{
  return c >= 'a' && c <= 'z';
}

inline bool IsOctalDigit(char c)
{
  return c >= '0' && c <= '7';
}

inline bool IsHexDigit(char c)
{
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

/// @brief judge a number if it's nan
inline bool IsNaN(double value)
{
  return !(value > value) && !(value <= value);
}

inline char ToHexAscii(uint8_t v)
{
  if (v <= 9)
  {
    return '0' + v;
  }
  return 'A' + v - 10;
}

inline uint8_t FromHexAscii(char c)
{
  if (!IsHexDigit(c))
    return 0;
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  return 0;
}

inline char ToLowerAscii(char c)
{
  return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

inline char ToUpperAscii(char c)
{
  return (c >= 'a' && c <= 'z') ? (c + ('A' - 'a')) : c;
}

inline char *StringAsArray(string *str)
{
  return str->empty() ? nullptr : &*str->begin();
}

string DebugString(const string &src);

string Bin2HexStr(const char *data, uint64_t len);
bool Hex2binStr(const string &src, char *data, uint64_t &len);
void Str2Hex(const char *str, unsigned char *hex, uint64_t len);
void Hex2Str(unsigned char *data, uint64_t len, char *str);
void Str2Upper(const char *src, char *dest);
void Str2Lower(const char *src, char *dest);

bool IsHexNumberString(const string &str);
std::vector<uint8_t> ParseHex(const char *psz);
string StringToHex(const char *str, uint64_t len);

void StringToUpper(string *str);
void StringToLower(string *str);

// Return lower-cased version of s.
string Lowercase(StringPiece s);
// Return upper-cased version of s.
string Uppercase(StringPiece s);
// Capitalize first character of each word in "*s".  "delimiters" is a
// set of characters that can be used as word boundaries.
void TitlecaseString(string *s, StringPiece delimiters);

/// string format

// stringfy
template <typename T>
inline string ToString(T value)
{
  return std::to_string(value); // c++11
}

// Lower-level routine that takes a va_list and appends to a specified
// string.  All other routines are just convenience wrappers around it.
uint64_t StringFormatAppendVA(string *dst, const char *format, va_list ap);
// Append result to a supplied string
uint64_t StringFormatAppend(string *dst, const char *format, ...);
// Store result into a supplied string and return it
uint64_t StringFormatTo(string *dst, const char *format, ...);
// Return a C++ string
string StringFormat(const char *format, ...);

/// string number

// ----------------------------------------------------------------------
// FastIntToBufferLeft()
//    These are intended for speed.
//
//    All functions take the output buffer as an arg.  FastInt() uses
//    at most 22 bytes, FastTime() uses exactly 30 bytes.  They all
//    return a pointer to the beginning of the output, which is the same as
//    the beginning of the input buffer.
//
//    NOTE: In 64-bit land, sizeof(time_t) is 8, so it is possible
//    to pass to FastTimeToBuffer() a time whose year cannot be
//    represented in 4 digits. In this case, the output buffer
//    will contain the string "Invalid:<value>"
// ----------------------------------------------------------------------

// Previously documented minimums -- the buffers provided must be at least this
// long, though these numbers are subject to change:
//     Int32, UInt32:                   12 bytes
//     Int64, UInt64, Int, Uint:        22 bytes
//     Time:                            30 bytes
// Use kFastToBufferSize rather than hardcoding constants.
static const uint64_t kFastToBufferSize = 32;

// ----------------------------------------------------------------------
// FastInt32ToBufferLeft()
// FastUInt32ToBufferLeft()
// FastInt64ToBufferLeft()
// FastUInt64ToBufferLeft()
//
// These functions convert their numeric argument to an ASCII
// representation of the numeric value in base 10, with the
// representation being left-aligned in the buffer.  The caller is
// responsible for ensuring that the buffer has enough space to hold
// the output.  The buffer should typically be at least kFastToBufferSize
// bytes.
//
// Returns a pointer to the end of the string (i.e. the null character
// terminating the string).
// ----------------------------------------------------------------------

char *FastInt32ToBufferLeft(int32_t i, char *buffer);   // at least 12 bytes
char *FastUInt32ToBufferLeft(uint32_t i, char *buffer); // at least 12 bytes
char *FastInt64ToBufferLeft(int64_t i, char *buffer);   // at least 22 bytes
char *FastUInt64ToBufferLeft(uint64_t i, char *buffer); // at least 22 bytes

bool StringParseBoolean(const string &value);
uint32_t StringParseUint32(const string &value);
uint64_t StringParseUint64(const string &value);
int32_t StringParseInt32(const string &value);
double StringParseDouble(const string &value);
uint64_t StringParseSizeT(const string &value);

bool StringParseInt32(const string &str, int32_t *out);
bool StringParseInt64(const string &str, int64_t *out);
bool StringParseUInt32(const string &str, uint32_t *out);
bool StringParseUInt64(const string &str, uint64_t *out);
bool StringParseDouble(const string &str, double *out);

bool VectorInt32ToString(const std::vector<int32_t> &vec, string *value);
std::vector<int32_t> StringParseVectorInt32(const string &value);

// Convert a 64-bit fingerprint value to an ASCII representation that
// is terminated by a '\0'.
// Buf must point to an array of at least kFastToBufferSize characters
StringPiece Uint64ToHexString(uint64_t v, char *buf);

// Attempt to parse a uint64 in the form encoded by FastUint64ToHexString.  If
// successful, stores the value in *v and returns true.  Otherwise,
// returns false.
bool HexStringToUint64(const StringPiece &s, uint64_t *v);

// Convert strings to 32bit integer values.
// Leading and trailing spaces are allowed.
// Return false with overflow or invalid input.
bool SafeStrToInt32(StringPiece str, int32_t *value);

// Convert strings to unsigned 32bit integer values.
// Leading and trailing spaces are allowed.
// Return false with overflow or invalid input.
bool SafeStrToUint32(StringPiece str, uint32_t *value);

// Convert strings to 64bit integer values.
// Leading and trailing spaces are allowed.
// Return false with overflow or invalid input.
bool SafeStrToInt64(StringPiece str, int64_t *value);

// Convert strings to unsigned 64bit integer values.
// Leading and trailing spaces are allowed.
// Return false with overflow or invalid input.
bool SafeStrToUint64(StringPiece str, uint64_t *value);

// Append a human-readable printout of "num" to *str
void StringAppendNumberTo(string *str, uint64_t num);
// Append a human-readable printout of "value" to *str.
// Escapes any non-printable characters found in "value".
void StringAppendEscapedStringTo(string *str, const StringPiece &value);
// Return a human-readable printout of "num"
string NumberToString(uint64_t num);
// Return a human-readable version of "value".
// Escapes any non-printable characters found in "value".
string EscapeString(const StringPiece &value);
// Parse a human-readable number from "*in" into *value.  On success,
// advances "*in" past the consumed number and sets "*val" to the
// numeric value.  Otherwise, returns false and leaves *in in an
// unspecified state.
bool StringConsumeDecimalNumber(StringPiece *in, uint64_t *val);

/// ---------------------------------------------------------------
/// @brief converting numbers  to buffer, buffer size should be big enough
/// ---------------------------------------------------------------
const uint64_t kMaxIntegerStringSize = 32;
const uint64_t kMaxDoubleStringSize = 32;
const uint64_t kMaxFloatStringSize = 24;
const uint64_t kMaxIntStringSize = kMaxIntegerStringSize;

/// @brief write number to buffer as string
/// @return end of result
/// @note without '\0' appended
/// private functions for common library, don't use them in your code
char *WriteDoubleToBuffer(double n, char *buffer);
char *WriteFloatToBuffer(float n, char *buffer);
char *WriteInt32ToBuffer(int32_t i, char *buffer);
char *WriteUInt32ToBuffer(uint32_t u, char *buffer);
char *WriteInt64ToBuffer(int64_t i, char *buffer);
char *WriteUInt64ToBuffer(uint64_t u64, char *buffer);

/// @brief write number to buffer as string
/// @return start of buffer
/// @note with '\0' appended
char *DoubleToString(double n, char *buffer);
char *FloatToString(float n, char *buffer);
char *UInt16ToHexString(uint16_t value, char *buffer);
char *UInt32ToHexString(uint32_t value, char *buffer);
char *UInt64ToHexString(uint64_t value, char *buffer);

string Int64ToString(int64_t num);
string Uint64ToString(uint64_t num);
string Int32ToString(int32_t num);
string Uint32ToString(uint32_t num);
string FloatToString(float n);
string DoubleToString(double n);
string RoundNumberToNDecimalString(double n, int32_t d);

// string human-readable

// Converts from an int64 to a human readable string representing the
// same number, using decimal powers.  e.g. 1200000 -> "1.20M".
string HumanReadableNum(int64_t value);

// Converts from an int64 representing a number of bytes to a
// human readable string representing the same number.
// e.g. 12345678 -> "11.77MiB".
string HumanReadableNumBytes(int64_t num_bytes);

// Converts a time interval as double to a human readable
// string. For example:
//   0.001       -> "1 ms"
//   10.0        -> "10 s"
//   933120.0    -> "10.8 days"
//   39420000.0  -> "1.25 years"
//   -10         -> "-10 s"
string HumanReadableElapsedTime(double seconds);

// for micros < 10ms, print "XX us".
// for micros < 10sec, print "XX ms".
// for micros >= 10 sec, print "XX sec".
// for micros <= 1 hour, print Y:X M:S".
// for micros > 1 hour, print Z:Y:X H:M:S".
int32_t AppendHumanMicros(uint64_t micros, char *output, int64_t len);

/// string ops

bool StringStartsWith(const string &str, const string &prefix);
bool StringEndsWith(const string &str, const string &suffix);

// ----------------------------------------------------------------------
// HasSuffixString()
//    Return true if str ends in suffix.
// ----------------------------------------------------------------------
inline bool HasSuffixString(const string &str,
                            const string &suffix)
{
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}
// ----------------------------------------------------------------------
// HasPrefixString()
//    Check if a string begins with a given prefix.
// ----------------------------------------------------------------------
inline bool HasPrefixString(const string &str,
                            const string &prefix)
{
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

bool StringStripSuffix(string *str, const string &suffix);
bool StringStripPrefix(string *str, const string &prefix);

string &StringLtrim(string &str);
string &StringRtrim(string &str);
string &StringTrim(string &str);
void StringTrim(std::vector<string> *str_list);
string StringTrim(const string &ori, const string &charlist = " ");

void StringSplitChar(const string &str,
                     char delim,
                     std::vector<string> *result);

void StringSplit(const string &full,
                 const string &delim,
                 std::vector<string> *result);

// Splits |line| into key value pairs according to the given delimiters and
// removes whitespace leading each key and trailing each value. Returns true
// only if each pair has a non-empty key and value. |key_value_pairs| will
// include ("","") pairs for entries without |key_value_delimiter|.
typedef std::vector<std::pair<string, string>> StringPairs;
bool StringSplitIntoKeyValuePairs(const string &line,
                                  char key_value_delimiter,
                                  char key_value_pair_delimiter,
                                  StringPairs *key_value_pairs);

string StringReplace(const string &str,
                     const string &src,
                     const string &dest);

string StringConcat(const std::vector<string> &elems, char delim);

// Removes leading ascii_isspace() characters.
// Returns number of characters removed.
uint64_t StringRemoveLeadingWhitespace(StringPiece *text);

// Removes trailing ascii_isspace() characters.
// Returns number of characters removed.
uint64_t StringRemoveTrailingWhitespace(StringPiece *text);

// Removes leading and trailing ascii_isspace() chars.
// Returns number of chars removed.
uint64_t StringRemoveWhitespaceContext(StringPiece *text);

// Consume a leading positive integer value.  If any digits were
// found, store the value of the leading unsigned number in "*val",
// advance "*s" past the consumed number, and return true.  If
// overflow occurred, returns false.  Otherwise, returns false.
bool StringConsumeLeadingDigits(StringPiece *s, uint64_t *val);

// Consume a leading token composed of non-whitespace characters only.
// If *s starts with a non-zero number of non-whitespace characters, store
// them in *val, advance *s past them, and return true.  Else return false.
bool StringConsumeNonWhitespace(StringPiece *s, StringPiece *val);

// If "*s" starts with "expected", consume it and return true.
// Otherwise, return false.
bool StringConsumePrefix(StringPiece *s, StringPiece expected);

// If "*s" ends with "expected", remove it and return true.
// Otherwise, return false.
bool StringConsumeSuffix(StringPiece *s, StringPiece expected);

string StringRemoveCharset(const string &src, const char *charset);

std::vector<string> StringSplit(const string &s, char delim);
std::vector<string> StringSplit(StringPiece text, StringPiece delims);

inline std::vector<string> StringSplitChar(StringPiece text, char delim)
{
  return StringSplit(text, StringPiece(&delim, 1));
}

// Replaces the first occurrence (if replace_all is false) or all occurrences
// (if replace_all is true) of oldsub in s with newsub.
string StringReplace(StringPiece s, StringPiece oldsub, StringPiece newsub,
                     bool replace_all);

// ----------------------------------------------------------------------
// StringAppendPieces()
//    Same as above, but adds the output to the given string.
//    WARNING: For speed, StrAppend does not try to check each of its input
//    arguments to be sure that they are not a subset of the string being
//    appended to.  That is, while this will work:
//
//    string s = "foo";
//    s += s;
//
//    This will not (necessarily) work:
//
//    string s = "foo";
//    StringAppendPieces(&s, s);
// ----------------------------------------------------------------------

void StringAppendInitPieces(string *result, std::initializer_list<StringPiece> pieces);

string StringCatInitPieces(std::initializer_list<StringPiece> pieces);

// For performance reasons, we have specializations for <= 4 args.
void StringAppendPieces(string *dest, const StringPiece &a);
void StringAppendPieces(string *dest, const StringPiece &a, const StringPiece &b);
void StringAppendPieces(string *dest, const StringPiece &a, const StringPiece &b,
                        const StringPiece &c);
void StringAppendPieces(string *dest, const StringPiece &a, const StringPiece &b,
                        const StringPiece &c, const StringPiece &d);

// Support 5 or more arguments
template <typename... SP>
inline void StringAppendPieces(string *dest, const StringPiece &a, const StringPiece &b,
                               const StringPiece &c, const StringPiece &d, const StringPiece &e,
                               const SP &... args)
{
  StringAppendInitPieces(dest,
                         {a, b, c, d, e,
                          static_cast<const StringPiece &>(args)...});
}

// Split "text" at "delim" characters, and parse each component as
// an integer.  If successful, adds the individual numbers in order
// to "*result" and returns true.  Otherwise returns false.
bool StringSplitAndParseAsInts(StringPiece text, char delim,
                               std::vector<int32_t> *result);
bool StringSplitAndParseAsInts(StringPiece text, char delim,
                               std::vector<int64_t> *result);

/*!
 * \brief A faster version of strtof
 * TODO the current version does not support INF, NAN, and hex number
 */
float FastStrToF(const char *nptr, char **endptr);

/**
 * \brief A faster string to integer convertor
 * TODO only support base <=10
 */
template <typename V>
inline V FastStrToInt(const char *nptr, char **endptr, uint64_t base)
{
  const char *p = nptr;
  // Skip leading white space, if any. Not necessary
  while (isspace(*p))
    ++p;

  // Get sign if any
  bool sign = true;
  if (*p == '-')
  {
    sign = false;
    ++p;
  }
  else if (*p == '+')
  {
    ++p;
  }

  V value;
  for (value = 0; isdigit(*p); ++p)
  {
    value = value * base + (*p - '0');
  }

  if (endptr)
    *endptr = (char *)p; // NOLINT(*)
  return sign ? value : -value;
}

template <typename V>
inline V FastStrToUint(const char *nptr, char **endptr, uint64_t base)
{
  const char *p = nptr;
  // Skip leading white space, if any. Not necessary
  while (isspace(*p))
    ++p;

  // Get sign if any
  bool sign = true;
  if (*p == '-')
  {
    sign = false;
    ++p;
  }
  else if (*p == '+')
  {
    ++p;
  }

  // we are parsing unsigned, so no minus sign should be found
  assert(sign == true);

  V value;
  for (value = 0; isdigit(*p); ++p)
  {
    value = value * base + (*p - '0');
  }

  if (endptr)
    *endptr = (char *)p; // NOLINT(*)
  return value;
}

inline uint64_t StrLength(const char *string)
{
  uint64_t length = strlen(string);
  return (length);
}

int stringmatchlen(const char *p, int plen, const char *s, int slen, int nocase);
int stringmatch(const char *p, const char *s, int nocase);
int64_t memtoll(const char *p, int *err);
uint32_t digits10(uint64_t v);
uint32_t sdigits10(int64_t v);
int ll2string(char *s, uint64_t len, int64_t value);
int string2ll(const char *s, uint64_t slen, int64_t *value);
int string2l(const char *s, uint64_t slen, long *value);
int string2ld(const char *s, uint64_t slen, long double *dp);
int d2string(char *buf, uint64_t len, double value);
int ld2string(char *buf, uint64_t len, long double value, int humanfriendly);
uint64_t utf8ToWideString(const char *utf8, unsigned short *utf16, uint64_t sz);
uint64_t utf8FromWideString(const unsigned short *utf16, char *utf8, uint64_t sz);

// Helper class for building result strings in a character buffer. The
// purpose of the class is to use safe operations that checks the
// buffer bounds on all operations in debug mode.

// This is a simplified version of V8's StringBuilderVec class.
template <typename T>
class StringBuilderVec
{
public:
  StringBuilderVec() : start_(NULL), length_(0) {}
  StringBuilderVec(T *data, uint64_t len) : start_(data), length_(len)
  {
    ASSERT(len == 0 || (len > 0 && data != NULL));
  }

  // Returns a vector using the same backing storage as this one,
  // spanning from and including 'from', to but not including 'to'.
  StringBuilderVec<T> SubVector(uint64_t from, uint64_t to)
  {
    ASSERT(to <= length_);
    ASSERT(from < to);
    ASSERT(0 <= from);
    return StringBuilderVec<T>(start() + from, to - from);
  }

  // Returns the length of the vector.
  uint64_t length() const { return length_; }

  // Returns whether or not the vector is empty.
  bool is_empty() const { return length_ == 0; }

  // Returns the pointer to the start of the data in the vector.
  T *start() const { return start_; }

  // Access individual vector elements - checks bounds in debug mode.
  T &operator[](uint64_t index) const
  {
    ASSERT(0 <= index && index < length_);
    return start_[index];
  }

  T &first() { return start_[0]; }

  T &last() { return start_[length_ - 1]; }

private:
  T *start_;
  uint64_t length_;
};

class StringBuilder
{
public:
  StringBuilder(char *buffer, uint64_t buffer_size)
      : buffer_(buffer, buffer_size), position_(0) {}

  ~StringBuilder()
  {
    if (!is_finalized())
      Finalize();
  }

  uint64_t size() const { return buffer_.length(); }

  // Get the current position in the builder.
  uint64_t position() const
  {
    ASSERT(!is_finalized());
    return position_;
  }

  // Reset the position.
  void Reset() { position_ = 0; }

  // Add a single character to the builder. It is not allowed to add
  // 0-characters; use the Finalize() method to terminate the string
  // instead.
  void AddCharacter(char c)
  {
    ASSERT(c != '\0');
    ASSERT(!is_finalized() && position_ < (int64_t)(buffer_.length()));
    buffer_[position_++] = c;
  }

  // Add an entire string to the builder. Uses strlen() internally to
  // compute the length of the input string.
  void AddString(const char *s)
  {
    AddSubstring(s, StrLength(s));
  }

  // Add the first 'n' characters of the given string 's' to the
  // builder. The input string must have enough characters.
  void AddSubstring(const char *s, uint64_t n)
  {
    ASSERT(!is_finalized() && position_ + n < buffer_.length());
    ASSERT(static_cast<uint64_t>(n) <= strlen(s));
    memmove(&buffer_[position_], s, n * sizeof(char));
    position_ += n;
  }

  // Add character padding to the builder. If count is non-positive,
  // nothing is added to the builder.
  void AddPadding(char c, uint64_t count)
  {
    for (uint64_t i = 0; i < count; i++)
    {
      AddCharacter(c);
    }
  }

  // Finalize the string by 0-terminating it and returning the buffer.
  char *Finalize()
  {
    ASSERT(!is_finalized() && position_ < (int64_t)(buffer_.length()));
    buffer_[position_] = '\0';
    // Make sure nobody managed to add a 0-character to the
    // buffer while building the string.
    ASSERT(strlen(buffer_.start()) == static_cast<uint64_t>(position_));
    position_ = -1;
    ASSERT(is_finalized());
    return buffer_.start();
  }

private:
  StringBuilderVec<char> buffer_;
  int64_t position_;

  bool is_finalized() const { return position_ < 0; }

  DISALLOW_COPY_AND_ASSIGN(StringBuilder);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STRING_UTIL_H_