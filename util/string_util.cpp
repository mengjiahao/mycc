
#include "string_util.h"
#include <ctype.h>
#include <float.h>
#include <iconv.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>
#include "stl_util.h"

namespace mycc
{
namespace util
{

const string kNullptrString = "nullptr";

#define DIG2CHR(dig) (((dig) <= 0x09) ? ('0' + (dig)) : ('a' + (dig)-0x0a))
#define CHR2DIG(chr) (((chr) >= '0' && (chr) <= '9') ? ((chr) - '0') : (((chr) >= 'a' && (chr) <= 'f') ? ((chr) - 'a' + 0x0a) : ((chr) - 'A' + 0x0a)))

namespace
{ // anonymous namespace

// Append is merely a version of memcpy that returns the address of the byte
// after the area just overwritten.  It comes in multiple flavors to minimize
// call overhead.
char *Append1(char *out, const StringPiece &x)
{
  memcpy(out, x.data(), x.size());
  return out + x.size();
}

char *Append2(char *out, const StringPiece &x1, const StringPiece &x2)
{
  memcpy(out, x1.data(), x1.size());
  out += x1.size();

  memcpy(out, x2.data(), x2.size());
  return out + x2.size();
}

char *Append4(char *out, const StringPiece &x1, const StringPiece &x2,
              const StringPiece &x3, const StringPiece &x4)
{
  memcpy(out, x1.data(), x1.size());
  out += x1.size();

  memcpy(out, x2.data(), x2.size());
  out += x2.size();

  memcpy(out, x3.data(), x3.size());
  out += x3.size();

  memcpy(out, x4.data(), x4.size());
  return out + x4.size();
}

char *UIntToHexBufferInternal(uint64_t value, char *buffer, int64_t num_byte)
{
  static const char hexdigits[] = "0123456789abcdef";
  int64_t digit_byte = 2 * num_byte;
  for (int64_t i = digit_byte - 1; i >= 0; --i)
  {
    buffer[i] = hexdigits[uint32_t(value) & 0xf];
    value >>= 4;
  }
  return buffer + digit_byte;
}

template <typename T>
bool DoSplitAndParseAsInts(StringPiece text, char delim,
                           std::function<bool(StringPiece, T *)> converter,
                           std::vector<T> *result)
{
  result->clear();
  std::vector<string> num_strings = StringSplitChar(text, delim);
  for (const auto &s : num_strings)
  {
    T num;
    if (!converter(s, &num))
      return false;
    result->push_back(num);
  }
  return true;
}

char SafeFirstChar(StringPiece str)
{
  if (str.empty())
    return '\0';
  return str[0];
}

void SkipSpaces(StringPiece *str)
{
  while (isspace(SafeFirstChar(*str)))
    str->remove_prefix(1);
}

bool ParsePrechecks(const string &str)
{
  if (str.empty()) // No empty string allowed
    return false;
  if (str.size() >= 1 && (::isspace(str[0]) || ::isspace(str[str.size() - 1]))) // No padding allowed
    return false;
  if (str.size() != strlen(str.c_str())) // No embedded NUL characters allowed
    return false;
  return true;
}

} // namespace

// string ascii

string DebugString(const string &src)
{
  uint64_t src_len = src.size();
  string dst;
  dst.resize(src_len << 2);

  uint64_t j = 0;
  for (uint64_t i = 0; i < src_len; ++i)
  {
    uint8_t c = src[i];
    if (IsAsciiVisible(c))
    {
      dst[j++] = c;
    }
    else
    {
      dst[j++] = '\\';
      dst[j++] = 'x';
      dst[j++] = ToHexAscii(c >> 4);
      dst[j++] = ToHexAscii(c & 0xF);
    }
  }

  return dst.substr(0, j);
}

string Bin2HexStr(const char *data, uint64_t len)
{
  if (len <= 0)
    return "";

  static char hexmap[] = {
      '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

  string result(len * 2, ' ');
  for (uint64_t i = 0; i < len; ++i)
  {
    result[2 * i] = hexmap[(data[i] & 0xF0) >> 4];
    result[2 * i + 1] = hexmap[data[i] & 0x0F];
  }
  return result;
}

bool Hex2binStr(const string &src, char *data, uint64_t &len)
{
  uint64_t str_len = src.size();
  if (str_len % 2 != 0 || str_len >= (2 * len))
    return false;

  char c, b;
  bool first = true;
  for (uint64_t i = 0; i < str_len; i += 2)
  {
    c = src[i];
    if ((c > 47) && (c < 58))
      c -= 48;
    else if ((c > 64) && (c < 71))
      c -= (65 - 10);
    else if ((c > 96) && (c < 103))
      c -= (97 - 10);
    else
      continue;

    if (first)
      b = c << 4;
    else
      data[i / 2] = static_cast<uint8_t>(b + c);

    first = !first;
  }
  return true;
}

void Str2Hex(const char *str, unsigned char *hex, uint64_t len)
{
  uint64_t i = 0;
  for (; i < len; i++)
  {
    hex[i] = ((CHR2DIG(str[i * 2]) << 4) & 0xf0) + CHR2DIG(str[i * 2 + 1]);
  }
}

void Hex2Str(unsigned char *data, uint64_t len, char *str)
{
  uint64_t i = 0;
  for (; i < len; i++)
  {
    str[i * 2] = DIG2CHR((data[i] >> 4) & 0x0f);
    str[i * 2 + 1] = DIG2CHR((data[i]) & 0x0f);
  }
  str[len * 2] = '\0';
}

void Str2Upper(const char *src, char *dest)
{
  while (*src)
  {
    if (*src >= 'a' && *src <= 'z')
    {
      *dest++ = *src - 32;
    }
    else
    {
      *dest++ = *src;
    }
    ++src;
  }
  *dest = '\0';
}

void Str2Lower(const char *src, char *dest)
{
  while (*src)
  {
    if (*src >= 'A' && *src <= 'Z')
    {
      *dest++ = *src + 32;
    }
    else
    {
      *dest++ = *src;
    }
    ++src;
  }
  *dest = '\0';
}

bool IsHexNumberString(const string &str)
{
  uint64_t starting_location = 0;
  if (str.size() > 2 && *str.begin() == '0' && *(str.begin() + 1) == 'x')
  {
    starting_location = 2;
  }
  for (auto c : str.substr(starting_location))
  {
    if (!IsHexDigit(c))
      return false;
  }
  // Return false for empty string or "0x".
  return (str.size() > starting_location);
}

std::vector<uint8_t> ParseHex(const char *psz)
{
  // convert hex dump to vector
  std::vector<uint8_t> vch;
  while (true)
  {
    while (isspace(*psz))
      psz++;
    signed char c = *psz++;
    if (!IsHexDigit(c))
    {
      break;
    }
    uint8_t n = (c << 4);
    c = *psz++;
    if (!IsHexDigit(c))
    {
      break;
    }
    n |= c;
    vch.push_back(n);
  }
  return vch;
}

uint64_t StringBinToHex(const char *str, uint64_t len,
                        char *ret, uint64_t msize)
{
  uint64_t n = 0;
  for (uint64_t i = 0; i < len && n < (msize - 4); ++i)
  {
    n += snprintf(ret + n, msize - n, "\\%02x", (uint8_t)str[i]);
  }
  ret[n] = '\0';
  return n;
}

uint64_t StringBinToAscii(char *str, uint64_t size,
                          char *ret, uint64_t msize)
{
  //msize = size * 3 + 5;
  char *p = str;
  uint64_t i = 0;
  while (size-- > 0 && i < msize - 4)
  {
    if (*p >= '!' && *p <= '~')
    { //~ printable, excluding space
      i += sprintf(ret + i, "%c", *p++);
    }
    else
    {
      i += sprintf(ret + i, "\\%02X", *p++);
    }
  }
  ret[i] = '\0';
  return i;
}

uint64_t StringAsciiToBin(const char *str, char *result, uint64_t size)
{
  uint64_t index = 0;
  const unsigned char *p = (const unsigned char *)str;
  while (*p && index < (size - 1))
  {
    if (*p == '\\')
    {
      unsigned char c1 = *(p + 1);
      unsigned char c2 = *(p + 2);
      if (c1 == 0 || c2 == 0)
        break;
      if (::isupper(c1))
        c1 = ::tolower(c1);
      int32_t value = (c1 >= '0' && c1 <= '9' ? c1 - '0' : c1 - 'a' + 10) * 16;
      if (::isupper(c2))
        c2 = ::tolower(c2);
      value += c2 >= '0' && c2 <= '9' ? c2 - '0' : c2 - 'a' + 10;
      result[index++] = (char)(value & 0xff);
      p += 2;
    }
    else
    {
      result[index++] = *p;
    }
    p++;
  }
  return index;
}

void StringToUpper(string *str)
{
  std::transform(str->begin(), str->end(), str->begin(), ::toupper);
}

void StringToLower(string *str)
{
  std::transform(str->begin(), str->end(), str->begin(), ::tolower);
}

// Return lower-cased version of s.
string Lowercase(StringPiece s)
{
  string result(s.data(), s.size());
  for (char &c : result)
  {
    c = ::tolower(c);
  }
  return result;
}

// Return upper-cased version of s.
string Uppercase(StringPiece s)
{
  string result(s.data(), s.size());
  for (char &c : result)
  {
    c = ::toupper(c);
  }
  return result;
}

void TitlecaseString(string *s, StringPiece delimiters)
{
  bool upper = true;
  for (string::iterator ss = s->begin(); ss != s->end(); ++ss)
  {
    if (upper)
    {
      *ss = ::toupper(*ss);
    }
    upper = (delimiters.find(*ss) != StringPiece::npos);
  }
}

uint64_t StringFormatAppendVA(string *dst, const char *format, va_list ap)
{
  // First try with a small fixed size buffer
  static const int32_t kSpaceLength = 1024;
  char space[kSpaceLength];

  // It's possible for methods that use a va_list to invalidate
  // the data in it upon use.  The fix is to make a copy
  // of the structure before using it and use that copy instead.
  va_list backup_ap;
  va_copy(backup_ap, ap);
  int32_t result = ::vsnprintf(space, kSpaceLength, format, backup_ap);
  va_end(backup_ap);

  if (result < kSpaceLength)
  {
    if (result >= 0)
    {
      // Normal case -- everything fit.
      dst->append(space, result);
      return result;
    }

    if (result < 0)
    {
      // Just an error.
      return 0;
    }
  }

  // Increase the buffer size to the size requested by vsnprintf,
  // plus one for the closing \0.
  int32_t length = result + 1;
  char *buf = new char[length];

  // Restore the va_list before we use it again
  va_copy(backup_ap, ap);
  result = ::vsnprintf(buf, length, format, backup_ap);
  va_end(backup_ap);

  if (result >= 0 && result < length)
  {
    // It fit
    dst->append(buf, result);
  }
  delete[] buf;
  return result;
}

uint64_t StringFormatAppend(string *dst, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  uint64_t result = StringFormatAppendVA(dst, format, ap);
  va_end(ap);
  return result;
}

uint64_t StringFormatTo(string *dst, const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  dst->clear();
  uint64_t result = StringFormatAppendVA(dst, format, ap);
  va_end(ap);
  return result;
}

string StringFormat(const char *format, ...)
{
  va_list ap;
  va_start(ap, format);
  string result;
  StringFormatAppendVA(&result, format, ap);
  va_end(ap);
  return result;
}

/// string numbers

char *FastInt32ToBufferLeft(int32_t i, char *buffer)
{
  uint32_t u = i;
  if (i < 0)
  {
    *buffer++ = '-';
    // We need to do the negation in modular (i.e., "unsigned")
    // arithmetic; MSVC++ apparently warns for plain "-u", so
    // we write the equivalent expression "0 - u" instead.
    u = 0 - u;
  }
  return FastUInt32ToBufferLeft(u, buffer);
}

char *FastUInt32ToBufferLeft(uint32_t i, char *buffer)
{
  char *start = buffer;
  do
  {
    *buffer++ = ((i % 10) + '0');
    i /= 10;
  } while (i > 0);
  *buffer = 0;
  std::reverse(start, buffer);
  return buffer;
}

char *FastInt64ToBufferLeft(int64_t i, char *buffer)
{
  uint64_t u = i;
  if (i < 0)
  {
    *buffer++ = '-';
    u = 0 - u;
  }
  return FastUInt64ToBufferLeft(u, buffer);
}

char *FastUInt64ToBufferLeft(uint64_t i, char *buffer)
{
  char *start = buffer;
  do
  {
    *buffer++ = ((i % 10) + '0');
    i /= 10;
  } while (i > 0);
  *buffer = 0;
  std::reverse(start, buffer);
  return buffer;
}

bool StringParseBoolean(const string &value)
{
  if (value == "true" || value == "1")
  {
    return true;
  }
  else if (value == "false" || value == "0")
  {
    return false;
  }
  return false;
}

bool StringParseInt32(const char *s, uint64_t n, int32_t *out)
{
  assert(out);
  const char *end = s + n;
  int32_t result = 0;
  while (s != end)
  {
    if (*s < '0' || *s > '9')
    {
      return false;
    }
    result *= 10;
    result += *s - '0';
    ++s;
  }
  *out = result;
  return true;
}

bool StringParseVectorInt32(const char *s, uint64_t n, std::vector<int32_t> &result)
{
  bool is_ok = false;
  int32_t val = 0;
  const char *e = s + n;
  const char *p = NULL;

  while (p != e && s < e)
  {
    p = strchr(s, ',');
    if (NULL == p)
    {
      p = e;
    }
    is_ok = StringParseInt32(s, (uint64_t)(p - s), &val);
    if (!is_ok)
    {
      return false;
    }
    result.push_back(val);
    s = p + 1;
  }

  return true;
}

uint32_t StringParseUint32(const string &value)
{
  uint64_t num = StringParseUint64(value);
  if ((num >> 32LL) == 0)
  {
    return static_cast<uint32_t>(num);
  }
  else
  {
    return 0;
  }
}

uint64_t StringParseUint64(const string &value)
{
  uint64_t endchar;
  uint64_t num = std::stoull(value.c_str(), &endchar);
  return num;
}

int32_t StringParseInt32(const string &value)
{
  uint64_t endchar;
  int32_t num = std::stoi(value.c_str(), &endchar);
  return num;
}

double StringParseDouble(const string &value)
{
  return std::stod(value);
}

uint64_t StringParseSizeT(const string &value)
{
  return static_cast<uint64_t>(StringParseUint64(value));
}

bool StringParseInt32(const string &str, int32_t *out)
{
  if (!ParsePrechecks(str))
    return false;
  char *endp = nullptr;
  errno = 0; // strtol will not set errno if valid
  int32_t n = strtol(str.c_str(), &endp, 10);
  if (out)
    *out = (int32_t)n;
  // Note that strtol returns a *long int*, so even if strtol doesn't report an over/underflow
  // we still have to check that the returned value is within the range of an *int32_t*. On 64-bit
  // platforms the size of these types may be different.
  return endp && *endp == 0 && !errno &&
         n >= std::numeric_limits<int32_t>::min() &&
         n <= std::numeric_limits<int32_t>::max();
}

bool StringParseInt64(const string &str, int64_t *out)
{
  if (!ParsePrechecks(str))
    return false;
  char *endp = nullptr;
  errno = 0; // strtoll will not set errno if valid
  int64_t n = strtoll(str.c_str(), &endp, 10);
  if (out)
    *out = (int64_t)n;
  // Note that strtoll returns a *int64_t*, so even if strtol doesn't report an over/underflow
  // we still have to check that the returned value is within the range of an *int64_t*.
  return endp && *endp == 0 && !errno &&
         n >= std::numeric_limits<int64_t>::min() &&
         n <= std::numeric_limits<int64_t>::max();
}

bool StringParseUInt32(const string &str, uint32_t *out)
{
  if (!ParsePrechecks(str))
    return false;
  if (str.size() >= 1 && str[0] == '-') // Reject negative values, unfortunately strtoul accepts these by default if they fit in the range
    return false;
  char *endp = nullptr;
  errno = 0; // strtoul will not set errno if valid
  uint64_t n = strtoul(str.c_str(), &endp, 10);
  if (out)
    *out = (uint32_t)n;
  // Note that strtoul returns a *uint64_t*, so even if it doesn't report an over/underflow
  // we still have to check that the returned value is within the range of an *uint32_t*. On 64-bit
  // platforms the size of these types may be different.
  return endp && *endp == 0 && !errno &&
         n <= std::numeric_limits<uint32_t>::max();
}

bool StringParseUInt64(const string &str, uint64_t *out)
{
  if (!ParsePrechecks(str))
    return false;
  if (str.size() >= 1 && str[0] == '-') // Reject negative values, unfortunately strtoull accepts these by default if they fit in the range
    return false;
  char *endp = nullptr;
  errno = 0; // strtoull will not set errno if valid
  uint64_t n = strtoull(str.c_str(), &endp, 10);
  if (out)
    *out = (uint64_t)n;
  // Note that strtoull returns a *uint64_t*, so even if it doesn't report an over/underflow
  // we still have to check that the returned value is within the range of an *uint64_t*.
  return endp && *endp == 0 && !errno &&
         n <= std::numeric_limits<uint64_t>::max();
}

bool StringParseDouble(const string &str, double *out)
{
  if (!ParsePrechecks(str))
    return false;
  if (str.size() >= 2 && str[0] == '0' && str[1] == 'x') // No hexadecimal floats allowed
    return false;
  std::istringstream text(str);
  text.imbue(std::locale::classic());
  double result;
  text >> result;
  if (out)
    *out = result;
  return text.eof() && !text.fail();
}

bool VectorInt32ToString(const std::vector<int32_t> &vec, string *value)
{
  *value = "";
  for (uint64_t i = 0; i < vec.size(); ++i)
  {
    if (i > 0)
    {
      *value += ",";
    }
    *value += ToString(vec[i]);
  }
  return true;
}

std::vector<int32_t> StringParseVectorInt32(const string &value)
{
  std::vector<int32_t> result;
  uint64_t start = 0;
  while (start < value.size())
  {
    uint64_t end = value.find(',', start);
    if (end == string::npos)
    {
      result.push_back(StringParseInt32(value.substr(start)));
      break;
    }
    else
    {
      result.push_back(StringParseInt32(value.substr(start, end - start)));
      start = end + 1;
    }
  }
  return result;
}

StringPiece Uint64ToHexString(uint64_t v, char *buf)
{
  static const char *hexdigits = "0123456789abcdef";
  const int32_t num_byte = 16;
  buf[num_byte] = '\0';
  for (int32_t i = num_byte - 1; i >= 0; --i)
  {
    buf[i] = hexdigits[v & 0xf];
    v >>= 4;
  }
  return StringPiece(buf, num_byte);
}

bool HexStringToUint64(const StringPiece &s, uint64_t *result)
{
  uint64_t v = 0;
  if (s.empty())
  {
    return false;
  }
  for (uint64_t i = 0; i < s.size(); i++)
  {
    char c = s[i];
    if (c >= '0' && c <= '9')
    {
      v = (v << 4) + (c - '0');
    }
    else if (c >= 'a' && c <= 'f')
    {
      v = (v << 4) + 10 + (c - 'a');
    }
    else if (c >= 'A' && c <= 'F')
    {
      v = (v << 4) + 10 + (c - 'A');
    }
    else
    {
      return false;
    }
  }
  *result = v;
  return true;
}

bool SafeStrToInt32(StringPiece str, int32_t *value)
{
  SkipSpaces(&str);

  int64_t vmax = kint32max;
  int32_t sign = 1;
  if (str.consume("-"))
  {
    sign = -1;
    // Different max for positive and negative integers.
    ++vmax;
  }

  if (!isdigit(SafeFirstChar(str)))
    return false;

  int64_t result = 0;
  do
  {
    result = result * 10 + SafeFirstChar(str) - '0';
    if (result > vmax)
    {
      return false;
    }
    str.remove_prefix(1);
  } while (isdigit(SafeFirstChar(str)));

  SkipSpaces(&str);

  if (!str.empty())
    return false;

  *value = static_cast<int32_t>(result * sign);
  return true;
}

bool SafeStrToUint32(StringPiece str, uint32_t *value)
{
  SkipSpaces(&str);
  if (!isdigit(SafeFirstChar(str)))
    return false;

  int64_t result = 0;
  do
  {
    result = result * 10 + SafeFirstChar(str) - '0';
    if (result > kuint32max)
    {
      return false;
    }
    str.remove_prefix(1);
  } while (isdigit(SafeFirstChar(str)));

  SkipSpaces(&str);
  if (!str.empty())
    return false;

  *value = static_cast<uint32_t>(result);
  return true;
}

bool SafeStrToInt64(StringPiece str, int64_t *value)
{
  SkipSpaces(&str);

  int64_t vlimit = kint64max;
  int32_t sign = 1;
  if (str.consume("-"))
  {
    sign = -1;
    // Different limit for positive and negative integers.
    vlimit = kint64min;
  }

  if (!isdigit(SafeFirstChar(str)))
    return false;

  int64_t result = 0;
  if (sign == 1)
  {
    do
    {
      int digit = SafeFirstChar(str) - '0';
      if ((vlimit - digit) / 10 < result)
      {
        return false;
      }
      result = result * 10 + digit;
      str.remove_prefix(1);
    } while (isdigit(SafeFirstChar(str)));
  }
  else
  {
    do
    {
      int digit = SafeFirstChar(str) - '0';
      if ((vlimit + digit) / 10 > result)
      {
        return false;
      }
      result = result * 10 - digit;
      str.remove_prefix(1);
    } while (isdigit(SafeFirstChar(str)));
  }

  SkipSpaces(&str);
  if (!str.empty())
    return false;

  *value = result;
  return true;
}

bool SafeStrToUint64(StringPiece str, uint64_t *value)
{
  SkipSpaces(&str);
  if (!isdigit(SafeFirstChar(str)))
    return false;

  int64_t result = 0;
  do
  {
    int digit = SafeFirstChar(str) - '0';
    if (static_cast<int64_t>(kuint64max - digit) / 10 < result)
    {
      return false;
    }
    result = result * 10 + digit;
    str.remove_prefix(1);
  } while (isdigit(SafeFirstChar(str)));

  SkipSpaces(&str);
  if (!str.empty())
    return false;

  *value = result;
  return true;
}

void StringAppendNumberTo(string *str, uint64_t num)
{
  char buf[30] = {0};
  snprintf(buf, sizeof(buf), PRIu64_FORMAT, (uint64_t)num);
  str->append(buf);
}

void StringAppendEscapedStringTo(string *str, const StringPiece &value)
{
  for (uint64_t i = 0; i < value.size(); i++)
  {
    char c = value[i];
    if (c >= ' ' && c <= '~')
    {
      str->push_back(c);
    }
    else
    {
      char buf[10];
      snprintf(buf, sizeof(buf), "\\x%02x",
               static_cast<unsigned int>(c) & 0xff);
      str->append(buf);
    }
  }
}

string NumberToString(uint64_t num)
{
  string r;
  StringAppendNumberTo(&r, num);
  return r;
}

string EscapeString(const StringPiece &value)
{
  string r;
  StringAppendEscapedStringTo(&r, value);
  return r;
}

bool StringConsumeDecimalNumber(StringPiece *in, uint64_t *val)
{
  // Constants that will be optimized away.
  static const uint64_t kMaxUint64 = std::numeric_limits<uint64_t>::max();
  static const char kLastDigitOfMaxUint64 =
      '0' + static_cast<char>(kMaxUint64 % 10);

  uint64_t value = 0;

  // reinterpret_cast-ing from char* to unsigned char* to avoid signedness.
  const unsigned char *start =
      reinterpret_cast<const unsigned char *>(in->data());

  const unsigned char *end = start + in->size();
  const unsigned char *current = start;
  for (; current != end; ++current)
  {
    const unsigned char ch = *current;
    if (ch < '0' || ch > '9')
      break;

    // Overflow check.
    // kMaxUint64 / 10 is also constant and will be optimized away.
    if (value > kMaxUint64 / 10 ||
        (value == kMaxUint64 / 10 && ch > kLastDigitOfMaxUint64))
    {
      return false;
    }

    value = (value * 10) + (ch - '0');
  }

  *val = value;
  const uint64_t digits_consumed = current - start;
  in->remove_prefix(digits_consumed);
  return digits_consumed != 0;
}

char *WriteHexUInt16ToBuffer(uint16_t value, char *buffer)
{
  return UIntToHexBufferInternal(value, buffer, sizeof(value));
}

char *WriteHexUInt32ToBuffer(uint32_t value, char *buffer)
{
  return UIntToHexBufferInternal(value, buffer, sizeof(value));
}

char *WriteHexUInt64ToBuffer(uint64_t value, char *buffer)
{
  return UIntToHexBufferInternal(value, buffer, sizeof(value));
}

char *UInt16ToHexString(uint16_t value, char *buffer)
{
  *WriteHexUInt16ToBuffer(value, buffer) = '\0';
  return buffer;
}

char *UInt32ToHexString(uint32_t value, char *buffer)
{
  *WriteHexUInt32ToBuffer(value, buffer) = '\0';
  return buffer;
}

char *UInt64ToHexString(uint64_t value, char *buffer)
{
  *WriteHexUInt64ToBuffer(value, buffer) = '\0';
  return buffer;
}

string UInt16ToHexString(uint16_t value)
{
  char buffer[2 * sizeof(value) + 1];
  return string(buffer, WriteHexUInt16ToBuffer(value, buffer));
}

string UInt32ToHexString(uint32_t value)
{
  char buffer[2 * sizeof(value) + 1];
  return string(buffer, WriteHexUInt32ToBuffer(value, buffer));
}

string UInt64ToHexString(uint64_t value)
{
  char buffer[2 * sizeof(value) + 1];
  return string(buffer, WriteHexUInt64ToBuffer(value, buffer));
}

// -----------------------------------------------------------------
// Double to string or buffer.
// Make sure buffer size >= kMaxDoubleStringSize
// -----------------------------------------------------------------
char *WriteDoubleToBuffer(double value, char *buffer)
{
  // DBL_DIG is 15 on almost all platforms.
  // If it's too big, the buffer will overflow
  //     STATIC_ASSERT(DBL_DIG < 20, "DBL_DIG is too big");

  if (value >= std::numeric_limits<double>::infinity())
  {
    strcpy(buffer, "inf"); // NOLINT
    return buffer + 3;
  }
  else if (value <= -std::numeric_limits<double>::infinity())
  {
    strcpy(buffer, "-inf"); // NOLINT
    return buffer + 4;
  }
  else if (IsNaN(value))
  {
    strcpy(buffer, "nan"); // NOLINT
    return buffer + 3;
  }

  return buffer + ::snprintf(buffer, kMaxDoubleStringSize, "%.*g", DBL_DIG, value);
}

// -------------------------------------------------------------
// Float to string or buffer.
// Makesure buffer size >= kMaxFloatStringSize
// -------------------------------------------------------------
char *WriteFloatToBuffer(float value, char *buffer)
{
  // FLT_DIG is 6 on almost all platforms.
  // If it's too big, the buffer will overflow
  //     STATIC_ASSERT(FLT_DIG < 10, "FLT_DIG is too big");
  if (value >= std::numeric_limits<double>::infinity())
  {
    strcpy(buffer, "inf"); // NOLINT
    return buffer + 3;
  }
  else if (value <= -std::numeric_limits<double>::infinity())
  {
    strcpy(buffer, "-inf"); // NOLINT
    return buffer + 4;
  }
  else if (IsNaN(value))
  {
    strcpy(buffer, "nan"); // NOLINT
    return buffer + 3;
  }

  return buffer + ::snprintf(buffer, kMaxFloatStringSize, "%.*g", FLT_DIG, value);
}

char *DoubleToString(double n, char *buffer)
{
  WriteDoubleToBuffer(n, buffer);
  return buffer;
}

char *FloatToString(float n, char *buffer)
{
  WriteFloatToBuffer(n, buffer);
  return buffer;
}

string Int64ToString(int64_t num)
{
  char buf[32];
  ::snprintf(buf, sizeof(buf), PRId64_FORMAT, num);
  return string(buf);
}

string Uint64ToString(uint64_t num)
{
  char buf[32];
  ::snprintf(buf, sizeof(buf), PRIu64_FORMAT, num);
  return string(buf);
}

string Int32ToString(int32_t num)
{
  return Int64ToString(static_cast<int64_t>(num));
}

string Uint32ToString(uint32_t num)
{
  return Uint64ToString(static_cast<uint64_t>(num));
}

string DoubleToString(double value)
{
  char buffer[kMaxDoubleStringSize];
  return string(buffer, WriteDoubleToBuffer(value, buffer));
}

string FloatToString(float value)
{
  char buffer[kMaxFloatStringSize];
  return string(buffer, WriteFloatToBuffer(value, buffer));
}

string RoundNumberToNDecimalString(double n, int32_t d)
{
  if (d < 0 || 9 < d)
  {
    return "(null)";
  }
  std::stringstream ss;
  ss << std::fixed;
  ss.precision(d);
  ss << n;
  return ss.str();
}

// string human-readable

string HumanReadableNum(int64_t value)
{
  string s;
  if (value < 0)
  {
    s += "-";
    value = -value;
  }
  if (value < 1000)
  {
    StringFormatAppend(&s, "%" PRId64, value);
  }
  else if (value >= static_cast<int64_t>(1e15))
  {
    // Number bigger than 1E15; use that notation.
    StringFormatAppend(&s, "%0.3G", static_cast<double>(value));
  }
  else
  {
    static const char units[] = "kMBT";
    const char *unit = units;
    while (value >= static_cast<int64_t>(1000000))
    {
      value /= static_cast<int64_t>(1000);
      ++unit;
      //CHECK(unit < units + ARRAYSIZE_UNSAFE(units));
    }
    StringFormatAppend(&s, "%.2f%c", value / 1000.0, *unit);
  }
  return s;
}

string HumanReadableNumBytes(int64_t num_bytes)
{
  if (num_bytes == kint64min)
  {
    // Special case for number with not representable negation.
    return "-8E";
  }

  const char *neg_str = (num_bytes < 0) ? "-" : "";
  if (num_bytes < 0)
  {
    num_bytes = -num_bytes;
  }

  // Special case for bytes.
  if (num_bytes < 1024)
  {
    // No fractions for bytes.
    char buf[8]; // Longest possible string is '-XXXXB'
    ::snprintf(buf, sizeof(buf), "%s%" PRId64 "B", neg_str,
               static_cast<int64_t>(num_bytes));
    return string(buf);
  }

  static const char units[] = "KMGTPE"; // int64 only goes up to E.
  const char *unit = units;
  while (num_bytes >= static_cast<int64_t>(1024) * 1024)
  {
    num_bytes /= 1024;
    ++unit;
    //CHECK(unit < units + ARRAYSIZE_UNSAFE(units));
  }

  // We use SI prefixes.
  char buf[16];
  ::snprintf(buf, sizeof(buf), ((*unit == 'K') ? "%s%.1f%ciB" : "%s%.2f%ciB"),
             neg_str, num_bytes / 1024.0, *unit);
  return string(buf);
}

string HumanReadableElapsedTime(double seconds)
{
  string human_readable;

  if (seconds < 0)
  {
    human_readable = "-";
    seconds = -seconds;
  }

  // Start with us and keep going up to years.
  // The comparisons must account for rounding to prevent the format breaking
  // the tested condition and returning, e.g., "1e+03 us" instead of "1 ms".
  const double microseconds = seconds * 1.0e6;
  if (microseconds < 999.5)
  {
    StringFormatAppend(&human_readable, "%0.3g us", microseconds);
    return human_readable;
  }
  double milliseconds = seconds * 1e3;
  if (milliseconds >= .995 && milliseconds < 1)
  {
    // Round half to even in Appendf would convert this to 0.999 ms.
    milliseconds = 1.0;
  }
  if (milliseconds < 999.5)
  {
    StringFormatAppend(&human_readable, "%0.3g ms", milliseconds);
    return human_readable;
  }
  if (seconds < 60.0)
  {
    StringFormatAppend(&human_readable, "%0.3g s", seconds);
    return human_readable;
  }
  seconds /= 60.0;
  if (seconds < 60.0)
  {
    StringFormatAppend(&human_readable, "%0.3g min", seconds);
    return human_readable;
  }
  seconds /= 60.0;
  if (seconds < 24.0)
  {
    StringFormatAppend(&human_readable, "%0.3g h", seconds);
    return human_readable;
  }
  seconds /= 24.0;
  if (seconds < 30.0)
  {
    StringFormatAppend(&human_readable, "%0.3g days", seconds);
    return human_readable;
  }
  if (seconds < 365.2425)
  {
    StringFormatAppend(&human_readable, "%0.3g months", seconds / 30.436875);
    return human_readable;
  }
  seconds /= 365.2425;
  StringFormatAppend(&human_readable, "%0.3g years", seconds);
  return human_readable;
}

// for micros < 10ms, print "XX us".
// for micros < 10sec, print "XX ms".
// for micros >= 10 sec, print "XX sec".
// for micros <= 1 hour, print Y:X M:S".
// for micros > 1 hour, print Z:Y:X H:M:S".
int32_t AppendHumanMicros(uint64_t micros, char *output, int64_t len)
{
  return ::snprintf(output, len, "%02" PRIu64 ":%02" PRIu64 ":%05.3f H:M:S",
                    micros / 1000000 / 3600, (micros / 1000000 / 60) % 60,
                    static_cast<double>(micros % 60000000) / 1000000);
}

// string ops

bool StringStartsWith(const string &str, const string &prefix)
{
  if (prefix.length() > str.length())
  {
    return false;
  }
  if (memcmp(str.c_str(), prefix.c_str(), prefix.length()) == 0)
  {
    return true;
  }
  return false;
}

bool StringEndsWith(const string &str, const string &suffix)
{
  if (suffix.length() > str.length())
  {
    return false;
  }
  return (str.substr(str.length() - suffix.length()) == suffix);
}

bool StringStripSuffix(string *str, const string &suffix)
{
  if (str->length() >= suffix.length())
  {
    uint64_t suffix_pos = str->length() - suffix.length();
    if (str->compare(suffix_pos, string::npos, suffix) == 0)
    {
      str->resize(str->size() - suffix.size());
      return true;
    }
  }

  return false;
}

bool StringStripPrefix(string *str, const string &prefix)
{
  if (str->length() >= prefix.length())
  {
    if (str->substr(0, prefix.size()) == prefix)
    {
      *str = str->substr(prefix.size());
      return true;
    }
  }
  return false;
}

string &StringLtrim(string &str)
{
  string::iterator it = std::find_if(str.begin(), str.end(), std::not1(std::ptr_fun(::isspace)));
  str.erase(str.begin(), it);
  return str;
}

string &StringRtrim(string &str)
{
  string::reverse_iterator it = std::find_if(str.rbegin(),
                                             str.rend(), std::not1(std::ptr_fun(::isspace)));
  str.erase(it.base(), str.end());
  return str;
}

string &StringTrim(string &str)
{
  return StringRtrim(StringLtrim(str));
}

void StringTrim(std::vector<string> *str_list)
{
  if (nullptr == str_list)
  {
    return;
  }

  std::vector<string>::iterator it;
  for (it = str_list->begin(); it != str_list->end(); ++it)
  {
    *it = StringTrim(*it);
  }
}

string StringTrim(const string &ori, const string &charlist)
{
  if (ori.empty())
    return ori;

  uint64_t pos = 0;
  int32_t rpos = ori.size() - 1;
  while (pos < ori.size())
  {
    bool meet = false;
    for (char c : charlist)
      if (ori.at(pos) == c)
      {
        meet = true;
        break;
      }
    if (!meet)
      break;
    ++pos;
  }
  while (rpos >= 0)
  {
    bool meet = false;
    for (char c : charlist)
      if (ori.at(rpos) == c)
      {
        meet = true;
        break;
      }
    if (!meet)
      break;
    --rpos;
  }
  return ori.substr(pos, rpos - pos + 1);
}

void StringSplitChar(const string &str,
                     char delim,
                     std::vector<string> *result)
{
  result->clear();
  if (str.empty())
  {
    return;
  }
  if (delim == '\0')
  {
    result->push_back(str);
    return;
  }

  string::size_type delim_length = 1;

  for (string::size_type begin_index = 0; begin_index < str.size();)
  {
    string::size_type end_index = str.find(delim, begin_index);
    if (end_index == string::npos)
    {
      result->push_back(str.substr(begin_index));
      return;
    }
    if (end_index > begin_index)
    {
      result->push_back(str.substr(begin_index, (end_index - begin_index)));
    }

    begin_index = end_index + delim_length;
  }
}

void StringSplit(const string &full,
                 const string &delim,
                 std::vector<string> *result)
{
  result->clear();
  if (full.empty())
  {
    return;
  }

  string tmp;
  string::size_type pos_begin = full.find_first_not_of(delim);
  string::size_type comma_pos = 0;

  while (pos_begin != string::npos)
  {
    comma_pos = full.find(delim, pos_begin);
    if (comma_pos != string::npos)
    {
      tmp = full.substr(pos_begin, comma_pos - pos_begin);
      pos_begin = comma_pos + delim.length();
    }
    else
    {
      tmp = full.substr(pos_begin);
      pos_begin = comma_pos;
    }

    if (!tmp.empty())
    {
      result->push_back(tmp);
      tmp.clear();
    }
  }
}

bool SplitStringIntoKeyValue(const string &line,
                             char key_value_delimiter,
                             string *key,
                             string *value)
{
  key->clear();
  value->clear();

  // Find the delimiter.
  uint64_t end_key_pos = line.find_first_of(key_value_delimiter);
  if (end_key_pos == string::npos)
  {
    fprintf(stderr, "cannot find delimiter in: %s\n", line.c_str());
    return false; // no delimiter
  }
  key->assign(line, 0, end_key_pos);

  // Find the value string.
  string remains(line, end_key_pos, line.size() - end_key_pos);
  uint64_t begin_value_pos = remains.find_first_not_of(key_value_delimiter);
  if (begin_value_pos == string::npos)
  {
    fprintf(stderr, "cannot parse value from line: %s\n", line.c_str());
    return false; // no value
  }
  value->assign(remains, begin_value_pos, remains.size() - begin_value_pos);
  return true;
}

bool StringSplitIntoKeyValuePairs(const string &line,
                                  char key_value_delimiter,
                                  char key_value_pair_delimiter,
                                  StringPairs *key_value_pairs)
{
  key_value_pairs->clear();

  std::vector<string> pairs;
  StringSplitChar(line, key_value_pair_delimiter, &pairs);

  bool success = true;
  for (uint64_t i = 0; i < pairs.size(); ++i)
  {
    // Don't add empty pairs into the result.
    if (pairs[i].empty())
      continue;

    string key;
    string value;
    if (!SplitStringIntoKeyValue(pairs[i], key_value_delimiter, &key, &value))
    {
      // Don't return here, to allow for pairs without associated
      // value or key; just record that the split failed.
      success = false;
    }
    key_value_pairs->push_back(make_pair(key, value));
  }
  return success;
}

string StringReplace(const string &str, const string &src,
                     const string &dest)
{
  string ret;

  string::size_type pos_begin = 0;
  string::size_type pos = str.find(src);
  while (pos != string::npos)
  {
    ret.append(str.data() + pos_begin, pos - pos_begin);
    ret += dest;
    pos_begin = pos + src.length();
    pos = str.find(src, pos_begin);
  }
  if (pos_begin < str.length())
  {
    ret.append(str.begin() + pos_begin, str.end());
  }
  return ret;
}

string StringConcat(const std::vector<string> &elems, char delim)
{
  string result;
  std::vector<string>::const_iterator it = elems.begin();
  while (it != elems.end())
  {
    result.append(*it);
    result.append(1, delim);
    ++it;
  }
  if (!result.empty())
  {
    result.resize(result.size() - 1);
  }
  return result;
}

string StringRemoveCharset(const string &src, char *charset)
{
  string result(src);
  for (uint64_t i = 0; i < strlen(charset); i++)
  {
    result.erase(std::remove(result.begin(), result.end(), charset[i]), result.end());
  }
  return result;
}

uint64_t StringRemoveLeadingWhitespace(StringPiece *text)
{
  uint64_t count = 0;
  const char *ptr = text->data();
  while (count < text->size() && isspace(*ptr))
  {
    count++;
    ptr++;
  }
  text->remove_prefix(count);
  return count;
}

uint64_t StringRemoveTrailingWhitespace(StringPiece *text)
{
  uint64_t count = 0;
  const char *ptr = text->data() + text->size() - 1;
  while (count < text->size() && isspace(*ptr))
  {
    ++count;
    --ptr;
  }
  text->remove_suffix(count);
  return count;
}

uint64_t StringRemoveWhitespaceContext(StringPiece *text)
{
  // use RemoveLeadingWhitespace() and RemoveTrailingWhitespace() to do the job
  return (StringRemoveLeadingWhitespace(text) + StringRemoveTrailingWhitespace(text));
}

bool StringConsumePrefix(StringPiece *s, StringPiece expected)
{
  if (s->starts_with(expected))
  {
    s->remove_prefix(expected.size());
    return true;
  }
  return false;
}

bool StringConsumeSuffix(StringPiece *s, StringPiece expected)
{
  if (s->ends_with(expected))
  {
    s->remove_suffix(expected.size());
    return true;
  }
  return false;
}

bool StringConsumeLeadingDigits(StringPiece *s, uint64_t *val)
{
  const char *p = s->data();
  const char *limit = p + s->size();
  uint64_t v = 0;
  while (p < limit)
  {
    const char c = *p;
    if (c < '0' || c > '9')
      break;
    uint64_t new_v = (v * 10) + (c - '0');
    if (new_v / 8 < v)
    {
      // Overflow occurred
      return false;
    }
    v = new_v;
    p++;
  }
  if (p > s->data())
  {
    // Consume some digits
    s->remove_prefix(p - s->data());
    *val = v;
    return true;
  }
  else
  {
    return false;
  }
}

bool StringConsumeNonWhitespace(StringPiece *s, StringPiece *val)
{
  const char *p = s->data();
  const char *limit = p + s->size();
  while (p < limit)
  {
    const char c = *p;
    if (isspace(c))
      break;
    p++;
  }
  const uint64_t n = p - s->data();
  if (n > 0)
  {
    *val = StringPiece(s->data(), n);
    s->remove_prefix(n);
    return true;
  }
  else
  {
    *val = StringPiece();
    return false;
  }
}

string StringRemoveCharset(const string &src, const char *charset)
{
  string result(src);
  for (uint64_t i = 0; i < strlen(charset); i++)
  {
    result.erase(std::remove(result.begin(), result.end(), charset[i]), result.end());
  }
  return result;
}

/*!
 * \brief Split a string by delimiter
 * \param s String to be splitted.
 * \param delim The delimiter.
 * \return a splitted vector of strings.
 */
std::vector<string> StringSplit(const string &s, char delim)
{
  string item;
  std::istringstream is(s);
  std::vector<string> ret;
  while (std::getline(is, item, delim))
  {
    ret.push_back(item);
  }
  return ret;
}

std::vector<string> StringSplit(StringPiece text, StringPiece delims)
{
  std::vector<string> result;
  uint64_t token_start = 0;
  if (!text.empty())
  {
    for (uint64_t i = 0; i < text.size() + 1; i++)
    {
      if ((i == text.size()) || (delims.find(text[i]) != StringPiece::npos))
      {
        StringPiece token(text.data() + token_start, i - token_start);
        result.push_back(token.toString());
        token_start = i + 1;
      }
    }
  }
  return result;
}

string StringReplace(StringPiece s, StringPiece oldsub, StringPiece newsub,
                     bool replace_all)
{
  // We could avoid having to shift data around in the string if
  // we had a StringPiece::find() overload that searched for a StringPiece.
  string res = s.toString();
  uint64_t pos = 0;
  while ((pos = res.find(oldsub.data(), pos, oldsub.size())) != string::npos)
  {
    res.replace(pos, oldsub.size(), newsub.data(), newsub.size());
    pos += newsub.size();
    if (oldsub.empty())
    {
      pos++; // Match at the beginning of the text and after every byte
    }
    if (!replace_all)
    {
      break;
    }
  }
  return res;
}

void StringAppendInitPieces(string *result, std::initializer_list<StringPiece> pieces)
{
  uint64_t old_size = result->size();
  uint64_t total_size = old_size;
  for (const StringPiece piece : pieces)
  {
    //DCHECK_NO_OVERLAP(*result, piece);
    total_size += piece.size();
  }
  STLStringResizeUninitialized(result, total_size);

  char *const begin = &*result->begin();
  char *out = begin + old_size;
  for (const StringPiece piece : pieces)
  {
    const uint64_t this_size = piece.size();
    memcpy(out, piece.data(), this_size);
    out += this_size;
  }
  //DCHECK_EQ(out, begin + result->size());
}

string StringCatInitPieces(std::initializer_list<StringPiece> pieces)
{
  string result;
  StringAppendInitPieces(&result, pieces);
  return result;
}

void StringAppendPieces(string *result, const StringPiece &a)
{
  //DCHECK_NO_OVERLAP(*result, a);
  result->append(a.data(), a.size());
}

void StringAppendPieces(string *result, const StringPiece &a, const StringPiece &b)
{
  //DCHECK_NO_OVERLAP(*result, a);
  //DCHECK_NO_OVERLAP(*result, b);
  string::size_type old_size = result->size();
  STLStringResizeUninitialized(result, old_size + a.size() + b.size());
  char *const begin = &*result->begin();
  char *out = Append2(begin + old_size, a, b);
  UNUSED_PARAM(out);
  //DCHECK_EQ(out, begin + result->size());
}

void StringAppendPieces(string *result, const StringPiece &a, const StringPiece &b,
                        const StringPiece &c)
{
  //DCHECK_NO_OVERLAP(*result, a);
  //DCHECK_NO_OVERLAP(*result, b);
  //DCHECK_NO_OVERLAP(*result, c);
  string::size_type old_size = result->size();
  STLStringResizeUninitialized(result,
                               old_size + a.size() + b.size() + c.size());
  char *const begin = &*result->begin();
  char *out = Append2(begin + old_size, a, b);
  out = Append1(out, c);
  //DCHECK_EQ(out, begin + result->size());
}

void StringAppendPieces(string *result, const StringPiece &a, const StringPiece &b,
                        const StringPiece &c, const StringPiece &d)
{
  //DCHECK_NO_OVERLAP(*result, a);
  //DCHECK_NO_OVERLAP(*result, b);
  //DCHECK_NO_OVERLAP(*result, c);
  //DCHECK_NO_OVERLAP(*result, d);
  string::size_type old_size = result->size();
  STLStringResizeUninitialized(
      result, old_size + a.size() + b.size() + c.size() + d.size());
  char *const begin = &*result->begin();
  char *out = Append4(begin + old_size, a, b, c, d);
  UNUSED_PARAM(out);
  //DCHECK_EQ(out, begin + result->size());
}

bool StringSplitAndParseAsInts(StringPiece text, char delim,
                               std::vector<int32_t> *result)
{
  return DoSplitAndParseAsInts<int32_t>(text, delim, SafeStrToInt32, result);
}

bool StringSplitAndParseAsInts(StringPiece text, char delim,
                               std::vector<int64_t> *result)
{
  return DoSplitAndParseAsInts<int64_t>(text, delim, SafeStrToInt64, result);
}

float FastStrToF(const char *nptr, char **endptr)
{
  const char *p = nptr;
  // Skip leading white space, if any. Not necessary
  while (isspace(*p))
    ++p;

  // Get sign, if any.
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

  // Get digits before decimal point or exponent, if any.
  float value;
  for (value = 0; isdigit(*p); ++p)
  {
    value = value * 10.0f + (*p - '0');
  }

  // Get digits after decimal point, if any.
  if (*p == '.')
  {
    uint64_t pow10 = 1;
    uint64_t val2 = 0;
    ++p;
    while (isdigit(*p))
    {
      val2 = val2 * 10 + (*p - '0');
      pow10 *= 10;
      ++p;
    }
    value += static_cast<float>(
        static_cast<double>(val2) / static_cast<double>(pow10));
  }

  // Handle exponent, if any.
  if ((*p == 'e') || (*p == 'E'))
  {
    ++p;
    bool frac = false;
    float scale = 1.0;
    unsigned expon;
    // Get sign of exponent, if any.
    if (*p == '-')
    {
      frac = true;
      ++p;
    }
    else if (*p == '+')
    {
      ++p;
    }
    // Get digits of exponent, if any.
    for (expon = 0; isdigit(*p); p += 1)
    {
      expon = expon * 10 + (*p - '0');
    }
    if (expon > 38)
      expon = 38;
    // Calculate scaling factor.
    while (expon >= 8)
    {
      scale *= 1E8;
      expon -= 8;
    }
    while (expon > 0)
    {
      scale *= 10.0;
      expon -= 1;
    }
    // Return signed and scaled floating point result.
    value = frac ? (value / scale) : (value * scale);
  }

  if (endptr)
    *endptr = (char *)p; // NOLINT(*)
  return sign ? value : -value;
}

/* Glob-style pattern matching. */
int stringmatchlen(const char *pattern, int patternLen,
                   const char *string, int stringLen, int nocase)
{
  while (patternLen)
  {
    switch (pattern[0])
    {
    case '*':
      while (pattern[1] == '*')
      {
        pattern++;
        patternLen--;
      }
      if (patternLen == 1)
        return 1; /* match */
      while (stringLen)
      {
        if (stringmatchlen(pattern + 1, patternLen - 1,
                           string, stringLen, nocase))
          return 1; /* match */
        string++;
        stringLen--;
      }
      return 0; /* no match */
      break;
    case '?':
      if (stringLen == 0)
        return 0; /* no match */
      string++;
      stringLen--;
      break;
    case '[':
    {
      int is_not;
      int match;

      pattern++;
      patternLen--;
      is_not = pattern[0] == '^';
      if (is_not)
      {
        pattern++;
        patternLen--;
      }
      match = 0;
      while (1)
      {
        if (pattern[0] == '\\' && patternLen >= 2)
        {
          pattern++;
          patternLen--;
          if (pattern[0] == string[0])
            match = 1;
        }
        else if (pattern[0] == ']')
        {
          break;
        }
        else if (patternLen == 0)
        {
          pattern--;
          patternLen++;
          break;
        }
        else if (pattern[1] == '-' && patternLen >= 3)
        {
          int start = pattern[0];
          int end = pattern[2];
          int c = string[0];
          if (start > end)
          {
            int t = start;
            start = end;
            end = t;
          }
          if (nocase)
          {
            start = tolower(start);
            end = tolower(end);
            c = tolower(c);
          }
          pattern += 2;
          patternLen -= 2;
          if (c >= start && c <= end)
            match = 1;
        }
        else
        {
          if (!nocase)
          {
            if (pattern[0] == string[0])
              match = 1;
          }
          else
          {
            if (tolower((int)pattern[0]) == tolower((int)string[0]))
              match = 1;
          }
        }
        pattern++;
        patternLen--;
      }
      if (is_not)
        match = !match;
      if (!match)
        return 0; /* no match */
      string++;
      stringLen--;
      break;
    }
    case '\\':
      if (patternLen >= 2)
      {
        pattern++;
        patternLen--;
      }
      /* fall through */
    default:
      if (!nocase)
      {
        if (pattern[0] != string[0])
          return 0; /* no match */
      }
      else
      {
        if (tolower((int)pattern[0]) != tolower((int)string[0]))
          return 0; /* no match */
      }
      string++;
      stringLen--;
      break;
    }
    pattern++;
    patternLen--;
    if (stringLen == 0)
    {
      while (*pattern == '*')
      {
        pattern++;
        patternLen--;
      }
      break;
    }
  }
  if (patternLen == 0 && stringLen == 0)
    return 1;
  return 0;
}

int stringmatch(const char *pattern, const char *string, int nocase)
{
  return stringmatchlen(pattern, strlen(pattern), string, strlen(string), nocase);
}

/* Convert a string representing an amount of memory into the number of
 * bytes, so for instance memtoll("1Gb") will return 1073741824 that is
 * (1024*1024*1024).
 *
 * On parsing error, if *err is not NULL, it's set to 1, otherwise it's
 * set to 0. On error the function return value is 0, regardless of the
 * fact 'err' is NULL or not. */
int64_t memtoll(const char *p, int *err)
{
  const char *u;
  char buf[128];
  long mul; /* unit multiplier */
  int64_t val;
  unsigned int digits;

  if (err)
    *err = 0;

  /* Search the first non digit character. */
  u = p;
  if (*u == '-')
    u++;
  while (*u && isdigit(*u))
    u++;
  if (*u == '\0' || !strcasecmp(u, "b"))
  {
    mul = 1;
  }
  else if (!strcasecmp(u, "k"))
  {
    mul = 1000;
  }
  else if (!strcasecmp(u, "kb"))
  {
    mul = 1024;
  }
  else if (!strcasecmp(u, "m"))
  {
    mul = 1000 * 1000;
  }
  else if (!strcasecmp(u, "mb"))
  {
    mul = 1024 * 1024;
  }
  else if (!strcasecmp(u, "g"))
  {
    mul = 1000L * 1000 * 1000;
  }
  else if (!strcasecmp(u, "gb"))
  {
    mul = 1024L * 1024 * 1024;
  }
  else
  {
    if (err)
      *err = 1;
    return 0;
  }

  /* Copy the digits into a buffer, we'll use strtoll() to convert
     * the digit (without the unit) into a number. */
  digits = u - p;
  if (digits >= sizeof(buf))
  {
    if (err)
      *err = 1;
    return 0;
  }
  memcpy(buf, p, digits);
  buf[digits] = '\0';

  char *endptr;
  errno = 0;
  val = strtoll(buf, &endptr, 10);
  if ((val == 0 && errno == EINVAL) || *endptr != '\0')
  {
    if (err)
      *err = 1;
    return 0;
  }
  return val * mul;
}

/* Return the number of digits of 'v' when converted to string in radix 10.
 * See ll2string() for more information. */
uint32_t digits10(uint64_t v)
{
  if (v < 10)
    return 1;
  if (v < 100)
    return 2;
  if (v < 1000)
    return 3;
  if (v < 1000000000000UL)
  {
    if (v < 100000000UL)
    {
      if (v < 1000000)
      {
        if (v < 10000)
          return 4;
        return 5 + (v >= 100000);
      }
      return 7 + (v >= 10000000UL);
    }
    if (v < 10000000000UL)
    {
      return 9 + (v >= 1000000000UL);
    }
    return 11 + (v >= 100000000000UL);
  }
  return 12 + digits10(v / 1000000000000UL);
}

/* Like digits10() but for signed values. */
uint32_t sdigits10(int64_t v)
{
  if (v < 0)
  {
    /* Abs value of LLONG_MIN requires special handling. */
    uint64_t uv = (v != LLONG_MIN) ? (uint64_t)-v : ((uint64_t)LLONG_MAX) + 1;
    return digits10(uv) + 1; /* +1 for the minus. */
  }
  else
  {
    return digits10(v);
  }
}

/* Convert a int64_t into a string. Returns the number of
 * characters needed to represent the number.
 * If the buffer is not big enough to store the string, 0 is returned.
 *
 * Based on the following article (that apparently does not provide a
 * novel approach but only publicizes an already used technique):
 *
 * https://www.facebook.com/notes/facebook-engineering/three-optimization-tips-for-c/10151361643253920
 *
 * Modified in order to handle signed integers since the original code was
 * designed for unsigned integers. */
int ll2string(char *dst, uint64_t dstlen, int64_t svalue)
{
  static const char digits[201] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  int negative;
  uint64_t value;

  /* The main loop works with 64bit unsigned integers for simplicity, so
     * we convert the number here and remember if it is negative. */
  if (svalue < 0)
  {
    if (svalue != LLONG_MIN)
    {
      value = -svalue;
    }
    else
    {
      value = ((uint64_t)LLONG_MAX) + 1;
    }
    negative = 1;
  }
  else
  {
    value = svalue;
    negative = 0;
  }

  /* Check length. */
  uint32_t const length = digits10(value) + negative;
  if (length >= dstlen)
    return 0;

  /* Null term. */
  uint32_t next = length;
  dst[next] = '\0';
  next--;
  while (value >= 100)
  {
    int const i = (value % 100) * 2;
    value /= 100;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
    next -= 2;
  }

  /* Handle last 1-2 digits. */
  if (value < 10)
  {
    dst[next] = '0' + (uint32_t)value;
  }
  else
  {
    int i = (uint32_t)value * 2;
    dst[next] = digits[i + 1];
    dst[next - 1] = digits[i];
  }

  /* Add sign. */
  if (negative)
    dst[0] = '-';
  return length;
}

/* Convert a string into a int64_t. Returns 1 if the string could be parsed
 * into a (non-overflowing) int64_t, 0 otherwise. The value will be set to
 * the parsed value when appropriate.
 *
 * Note that this function demands that the string strictly represents
 * a int64_t: no spaces or other characters before or after the string
 * representing the number are accepted, nor zeroes at the start if not
 * for the string "0" representing the zero number.
 *
 * Because of its strictness, it is safe to use this function to check if
 * you can convert a string into a int64_t, and obtain back the string
 * from the number without any loss in the string representation. */
int string2ll(const char *s, uint64_t slen, int64_t *value)
{
  const char *p = s;
  uint64_t plen = 0;
  int negative = 0;
  uint64_t v;

  if (plen == slen)
    return 0;

  /* Special case: first and only digit is 0. */
  if (slen == 1 && p[0] == '0')
  {
    if (value != NULL)
      *value = 0;
    return 1;
  }

  if (p[0] == '-')
  {
    negative = 1;
    p++;
    plen++;

    /* Abort on only a negative sign. */
    if (plen == slen)
      return 0;
  }

  /* First digit should be 1-9, otherwise the string should just be 0. */
  if (p[0] >= '1' && p[0] <= '9')
  {
    v = p[0] - '0';
    p++;
    plen++;
  }
  else if (p[0] == '0' && slen == 1)
  {
    *value = 0;
    return 1;
  }
  else
  {
    return 0;
  }

  while (plen < slen && p[0] >= '0' && p[0] <= '9')
  {
    if (v > (ULLONG_MAX / 10)) /* Overflow. */
      return 0;
    v *= 10;

    if (v > (ULLONG_MAX - (p[0] - '0'))) /* Overflow. */
      return 0;
    v += p[0] - '0';

    p++;
    plen++;
  }

  /* Return if not all bytes were used. */
  if (plen < slen)
    return 0;

  if (negative)
  {
    if (v > ((uint64_t)(-(LLONG_MIN + 1)) + 1)) /* Overflow. */
      return 0;
    if (value != NULL)
      *value = -v;
  }
  else
  {
    if (v > LLONG_MAX) /* Overflow. */
      return 0;
    if (value != NULL)
      *value = v;
  }
  return 1;
}

/* Convert a string into a long. Returns 1 if the string could be parsed into a
 * (non-overflowing) long, 0 otherwise. The value will be set to the parsed
 * value when appropriate. */
int string2l(const char *s, uint64_t slen, long *lval)
{
  int64_t llval;

  if (!string2ll(s, slen, &llval))
    return 0;

  if (llval < LONG_MIN || llval > LONG_MAX)
    return 0;

  *lval = (long)llval;
  return 1;
}

/* Convert a string into a double. Returns 1 if the string could be parsed
 * into a (non-overflowing) double, 0 otherwise. The value will be set to
 * the parsed value when appropriate.
 *
 * Note that this function demands that the string strictly represents
 * a double: no spaces or other characters before or after the string
 * representing the number are accepted. */
int string2ld(const char *s, uint64_t slen, long double *dp)
{
  char buf[256];
  long double value;
  char *eptr;

  if (slen >= sizeof(buf))
    return 0;
  memcpy(buf, s, slen);
  buf[slen] = '\0';

  errno = 0;
  value = strtold(buf, &eptr);
  if (isspace(buf[0]) || eptr[0] != '\0' ||
      (errno == ERANGE &&
       (value == HUGE_VAL || value == -HUGE_VAL || value == 0)) ||
      errno == EINVAL) // isnan(value)
    return 0;

  if (dp)
    *dp = value;
  return 1;
}

/* Convert a double to a string representation. Returns the number of bytes
 * required. The representation should always be parsable by strtod(3).
 * This function does not support human-friendly formatting like ld2string
 * does. It is intended mainly to be used inside t_zset.c when writing scores
 * into a ziplist representing a sorted set. */
int d2string(char *buf, uint64_t len, double value)
{
  // if (isnan(value))
  // {
  //   len = snprintf(buf, len, "nan");
  // }
  // if (isinf(value))
  // {
  //   if (value < 0)
  //     len = snprintf(buf, len, "-inf");
  //   else
  //     len = snprintf(buf, len, "inf");
  // }
  if (value == 0)
  {
    /* See: http://en.wikipedia.org/wiki/Signed_zero, "Comparisons". */
    if (1.0 / value < 0)
      len = snprintf(buf, len, "-0");
    else
      len = snprintf(buf, len, "0");
  }
  else
  {
#if (DBL_MANT_DIG >= 52) && (LLONG_MAX == 0x7fffffffffffffffLL)
    /* Check if the float is in a safe range to be casted into a
         * int64_t. We are assuming that int64_t is 64 bit here.
         * Also we are assuming that there are no implementations around where
         * double has precision < 52 bit.
         *
         * Under this assumptions we test if a double is inside an interval
         * where casting to int64_t is safe. Then using two castings we
         * make sure the decimal part is zero. If all this is true we use
         * integer printing function that is much faster. */
    double min = -4503599627370495; /* (2^52)-1 */
    double max = 4503599627370496;  /* -(2^52) */
    if (value > min && value < max && value == ((double)((int64_t)value)))
      len = ll2string(buf, len, (int64_t)value);
    else
#endif
      len = snprintf(buf, len, "%.17g", value);
  }

  return len;
}

/* Convert a long double into a string. If humanfriendly is non-zero
 * it does not use exponential format and trims trailing zeroes at the end,
 * however this results in loss of precision. Otherwise exp format is used
 * and the output of snprintf() is not modified.
 *
 * The function returns the length of the string or zero if there was not
 * enough buffer room to store it. */
int ld2string(char *buf, uint64_t len, long double value, int humanfriendly)
{
  uint64_t l;

  // if (isinf(value))
  // {
  //   /* Libc in odd systems (Hi Solaris!) will format infinite in a
  //        * different way, so better to handle it in an explicit way. */
  //   if (len < 5)
  //     return 0; /* No room. 5 is "-inf\0" */
  //   if (value > 0)
  //   {
  //     memcpy(buf, "inf", 3);
  //     l = 3;
  //   }
  //   else
  //   {
  //     memcpy(buf, "-inf", 4);
  //     l = 4;
  //   }
  // }
  if (humanfriendly)
  {
    /* We use 17 digits precision since with 128 bit floats that precision
         * after rounding is able to represent most small decimal numbers in a
         * way that is "non surprising" for the user (that is, most small
         * decimal numbers will be represented in a way that when converted
         * back into a string are exactly the same as what the user typed.) */
    l = snprintf(buf, len, "%.17Lf", value);
    if (l + 1 > len)
      return 0; /* No room. */
    /* Now remove trailing zeroes after the '.' */
    if (strchr(buf, '.') != NULL)
    {
      char *p = buf + l - 1;
      while (*p == '0')
      {
        p--;
        l--;
      }
      if (*p == '.')
        l--;
    }
  }
  else
  {
    l = snprintf(buf, len, "%.17Lg", value);
    if (l + 1 > len)
      return 0; /* No room. */
  }
  buf[l] = '\0';
  return l;
}

bool StringCodeConvert(const char *from_charset, const char *to_charset,
                       char *inbuf, int32_t inlen, char *outbuf, int32_t outlen)
{
  iconv_t cd;
  char **pin = &inbuf;
  char **pout = &outbuf;

  if ((cd = iconv_open(to_charset, from_charset)) == (iconv_t)-1)
  {
    //iconv_close(cd);
    //char err_buf[256] = {0};
    //ACE_DEBUG((LM_ERROR,"[%D] iconv_open failed,errno=%d,err=%s\n", errno, strerror_r(errno, err_buf, sizeof(err_buf))));
    return false;
  }

  uint64_t s_inlen = inlen;
  uint64_t s_outlen = outlen;
  memset(outbuf, 0, outlen);
  if (iconv(cd, pin, &s_inlen, pout, &s_outlen) == (size_t)-1)
  {
    iconv_close(cd);
    //ACE_DEBUG((LM_ERROR,"[%D] iconv failed errno=%d,outlen=%d,inlen=%d,inbuf=%s\n", errno, outlen, inlen, inbuf));
    return false;
  }

  outlen = s_outlen;
  iconv_close(cd);
  return true;
}

} // namespace util
} // namespace mycc