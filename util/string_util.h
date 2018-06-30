
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
static const int kFastToBufferSize = 32;

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
size_t StringParseSizeT(const string &value);

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
const int kMaxIntegerStringSize = 32;
const int kMaxDoubleStringSize = 32;
const int kMaxFloatStringSize = 24;
const int kMaxIntStringSize = kMaxIntegerStringSize;

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

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_STRING_UTIL_H_