
#include "string_util.h"
#include <float.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>
#include <string>
#include <vector>
#include "macros_util.h"
#include "stl_util.h"

namespace mycc
{
namespace util
{

const string kNullptrString = "nullptr";

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

} // namespace

// string ascii

string StringToHex(const char *str, uint64_t len)
{
  uint64_t len_size = 3 * len + 1;
  char tmp[len_size];
  tmp[len_size] = '\0';
  uint64_t n = 0;
  for (uint64_t i = 0; i < len && n < len_size; ++i)
  {
    n += snprintf(tmp + n, len_size - n, "\\%02x", (uint8_t)str[i]);
  }
  return string(tmp, n);
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

static const double kDoublePrecisionCheckMax = DBL_MAX / 1.000000000000001;

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
  size_t endchar;
  uint64_t num = std::stoull(value.c_str(), &endchar);
  return num;
}

int32_t StringParseInt(const string &value)
{
  size_t endchar;
  int32_t num = std::stoi(value.c_str(), &endchar);
  return num;
}

double StringParseDouble(const string &value)
{
  return std::stod(value);
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

namespace
{ // namespace anonymous
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
} // namespace

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

// string ops

bool StartsWith(const string &str, const string &prefix)
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

bool EndsWith(const string &str, const string &suffix)
{
  if (suffix.length() > str.length())
  {
    return false;
  }
  return (str.substr(str.length() - suffix.length()) == suffix);
}

bool StripSuffix(string *str, const string &suffix)
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

bool StripPrefix(string *str, const string &prefix)
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
  for (size_t i = 0; i < pairs.size(); ++i)
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
  std::vector<std::string>::const_iterator it = elems.begin();
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

string &StringToLower(string &ori)
{
  std::transform(ori.begin(), ori.end(), ori.begin(), ::tolower);
  return ori;
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

bool StringeConsumeSuffix(StringPiece *s, StringPiece expected)
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

} // namespace util
} // namespace mycc