
#include "ini_reader.h"
#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <utility>
#include "error_util.h"
#include "string_util.h"

namespace mycc
{
namespace util
{

int32_t INIReader::Parse(const string &filename)
{
  FILE *file = fopen(filename.c_str(), "r");
  if (NULL == file)
  {
    PRINT_ERROR("open %s failed(%d:%s)",
                filename.c_str(), errno, strerror(errno));
    return -1;
  }

  Clear();
  int32_t ret = ParseFile(file);
  fclose(file);

  return ret;
}

void INIReader::Clear()
{
  m_sections.clear();
  m_fields.clear();
}

int32_t INIReader::ParseFile(FILE *file)
{
  static const int32_t MAX_BUFF_LEN = 2048;
  char buff[MAX_BUFF_LEN] = {0};

  int32_t line_no = 0;
  string utf8bom;
  utf8bom.push_back(0xEF);
  utf8bom.push_back(0xBB);
  utf8bom.push_back(0xBF);
  std::map<string, string> *fields_map = NULL;
  while (fgets(buff, MAX_BUFF_LEN, file) != NULL)
  {
    line_no++;
    string line(buff);

    // 0. support UTF-8 BOM
    if (1 == line_no && line.find_first_of(utf8bom) == 0)
    {
      line.erase(0, 3);
    }

    // 1. remove comment
    for (uint64_t i = 0; i < line.length(); ++i)
    {
      if (';' == line[i] || '#' == line[i])
      {
        line.erase(i);
        break;
      }
    }

    // 2. remove prefix suffix spaces
    StringTrim(line);
    // 3. remove blank line
    if (line.empty())
    {
      continue;
    }

    // section
    if (line[0] == '[' && line[line.length() - 1] == ']')
    {
      string section(line.substr(1, line.length() - 2));
      StringTrim(section);
      if (section.empty())
      {
        return line_no;
      }
      m_sections.insert(section);
      fields_map = &(m_fields[section]);
      continue;
    }

    if (NULL == fields_map)
    {
      continue;
    }

    // fileds
    uint64_t pos = line.find('=');
    if (string::npos == pos)
    {
      continue;
    }
    string key = line.substr(0, pos);
    string value = line.substr(pos + 1);
    StringTrim(key);
    StringTrim(value);
    if (key.empty() || value.empty())
    {
      continue;
    }

    (*fields_map)[key] = value;
  }

  return 0;
}

string INIReader::Get(const string &section, const string &name,
                      const string &default_value)
{

  std::map<string, std::map<string, string>>::iterator it = m_fields.find(section);
  if (m_fields.end() == it)
  {
    return default_value;
  }

  std::map<string, string> &fields_map = it->second;
  std::map<string, string>::iterator cit = fields_map.find(name);
  if (fields_map.end() == cit)
  {
    return default_value;
  }

  return cit->second;
}

int32_t INIReader::GetInt32(const string &section, const string &name, int32_t default_value)
{
  string value = Get(section, name, "");

  const char *begin = value.c_str();
  char *end = NULL;

  int32_t n = strtol(begin, &end, 0);
  return end > begin ? n : default_value;
}

uint32_t INIReader::GetUInt32(const string &section, const string &name, uint32_t default_value)
{
  string value = Get(section, name, "");
  const char *begin = value.c_str();
  char *end = NULL;

  int32_t n = strtol(begin, &end, 0);
  if (end > begin && n >= 0)
  {
    return n;
  }
  return default_value;
}

int64_t INIReader::GetInt64(const string &section, const string &name, int64_t default_value)
{
  string value = Get(section, name, "");
  const char *begin = value.c_str();
  char *end = NULL;

  int64_t n = strtoll(begin, &end, 0);
  return end > begin ? n : default_value;
}

uint64_t INIReader::GetUInt64(const string &section, const string &name, uint64_t default_value)
{
  string value = Get(section, name, "");
  const char *begin = value.c_str();
  char *end = NULL;

  int64_t n = strtoll(begin, &end, 0);
  if (end > begin && n >= 0)
  {
    return n;
  }
  return default_value;
}

double INIReader::GetReal(const string &section, const string &name, double default_value)
{
  string value = Get(section, name, "");
  const char *begin = value.c_str();
  char *end = NULL;
  double n = strtod(begin, &end);
  return end > begin ? n : default_value;
}

bool INIReader::GetBoolean(const string &section, const string &name, bool default_value)
{
  string value = Get(section, name, "");

  std::transform(value.begin(), value.end(), value.begin(), ::tolower);
  if (value == "true" || value == "yes" || value == "on" || value == "1")
  {
    return true;
  }
  else if (value == "false" || value == "no" || value == "off" || value == "0")
  {
    return false;
  }
  else
  {
    return default_value;
  }
}

const std::set<string> &INIReader::GetSections() const
{
  return m_sections;
}

std::set<string> INIReader::GetFields(const string &section)
{
  std::set<string> fields;

  std::map<string, std::map<string, string>>::iterator it = m_fields.find(section);
  if (m_fields.end() == it)
  {
    return fields;
  }

  std::map<string, string> &fields_map = it->second;
  std::map<string, string>::iterator cit = fields_map.begin();
  for (; cit != fields_map.end(); ++cit)
  {
    fields.insert(cit->first);
  }

  return fields;
}

} // namespace util
} // namespace mycc