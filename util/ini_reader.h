
#ifndef MYCC_UTIL_INI_READER_H_
#define MYCC_UTIL_INI_READER_H_

#include <stdio.h>
#include <map>
#include <set>
#include <string>
#include "types_util.h"

namespace mycc
{
namespace util
{

class INIReader
{
public:
  explicit INIReader() {}
  ~INIReader() {}

  int32_t Parse(const string &filename);

  void Clear();

  string Get(const string &section, const string &name,
             const string &default_value);

  // Get an integer (long) value from INI file, returning default_value
  // if not found or not a valid integer (decimal "1234", "-1234",
  // or hex "0x4d2").
  int32_t GetInt32(const string &section, const string &name,
                   int32_t default_value);

  uint32_t GetUInt32(const string &section, const string &name,
                     uint32_t default_value);

  int64_t GetInt64(const string &section, const string &name,
                   int64_t default_value);

  uint64_t GetUInt64(const string &section, const string &name,
                     uint64_t default_value);

  // Get a real (floating point double) value from INI file, returning
  // default_value if not found or not a valid floating point value
  // according to strtod().
  double GetReal(const string &section, const string &name, double default_value);

  // Get a boolean value from INI file, returning default_value
  // if not found or if not a valid true/false value. Valid true
  // values are "true", "yes", "on", "1", and valid false values are
  // "false", "no", "off", "0" (not case sensitive).
  bool GetBoolean(const string &section, const string &name, bool default_value);

  // Returns all the section names from the INI file, in alphabetical order,
  // but in the original casing
  const std::set<string> &GetSections() const;

  // Returns all the field names from a section in the INI file, in
  // alphabetical order, but in the original casing. Returns an
  // empty set if the field name is unknown
  std::set<string> GetFields(const string &section);

  const char *GetLastError() const
  {
    return m_last_error;
  }

private:
  int32_t ParseFile(FILE *file);

private:
  char m_last_error[256];
  std::set<string> m_sections;
  std::map<string, std::map<string, string>> m_fields;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_INI_READER_H_