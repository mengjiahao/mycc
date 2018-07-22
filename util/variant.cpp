
#include "variant.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <iomanip>
#include <sstream>

namespace mycc
{
namespace util
{

const VariantVector VariantVectorNull;
const VariantMap VariantMapNull;
const VariantMapInt32Key VariantMapInt32KeyNull;
const Variant Variant::Null;

static const uint32_t MAX_ITOA_BUFFER_SIZE = 256;

static double atof(const char *str)
{
  if (str == nullptr)
  {
    return 0.0;
  }

  char buf[MAX_ITOA_BUFFER_SIZE];
  strncpy(buf, str, MAX_ITOA_BUFFER_SIZE);

  // strip string, only remain 7 numbers after '.'
  char *dot = strchr(buf, '.');
  if (dot != nullptr && dot - buf + 8 < MAX_ITOA_BUFFER_SIZE)
  {
    dot[8] = '\0';
  }

  return ::atof(buf);
}

Variant::Variant()
    : _type(Type::NONE)
{
  memset(&_field, 0, sizeof(_field));
}

Variant::Variant(unsigned char v)
    : _type(Type::BYTE)
{
  _field.byteVal = v;
}

Variant::Variant(int32_t v)
    : _type(Type::INT32)
{
  _field.int32Val = v;
}

Variant::Variant(uint32_t v)
    : _type(Type::UINT32)
{
  _field.unsigned32Val = v;
}

Variant::Variant(float v)
    : _type(Type::FLOAT)
{
  _field.floatVal = v;
}

Variant::Variant(double v)
    : _type(Type::DOUBLE)
{
  _field.doubleVal = v;
}

Variant::Variant(bool v)
    : _type(Type::BOOLEAN)
{
  _field.boolVal = v;
}

Variant::Variant(const char *v)
    : _type(Type::STRING)
{
  _field.strVal = new (std::nothrow) string();
  if (v)
  {
    *_field.strVal = v;
  }
}

Variant::Variant(const string &v)
    : _type(Type::STRING)
{
  _field.strVal = new (std::nothrow) string();
  *_field.strVal = v;
}

Variant::Variant(const VariantVector &v)
    : _type(Type::VECTOR)
{
  _field.vectorVal = new (std::nothrow) VariantVector();
  *_field.vectorVal = v;
}

Variant::Variant(VariantVector &&v)
    : _type(Type::VECTOR)
{
  _field.vectorVal = new (std::nothrow) VariantVector();
  *_field.vectorVal = std::move(v);
}

Variant::Variant(const VariantMap &v)
    : _type(Type::MAP)
{
  _field.mapVal = new (std::nothrow) VariantMap();
  *_field.mapVal = v;
}

Variant::Variant(VariantMap &&v)
    : _type(Type::MAP)
{
  _field.mapVal = new (std::nothrow) VariantMap();
  *_field.mapVal = std::move(v);
}

Variant::Variant(const VariantMapInt32Key &v)
    : _type(Type::INT32_KEY_MAP)
{
  _field.int32KeyMapVal = new (std::nothrow) VariantMapInt32Key();
  *_field.int32KeyMapVal = v;
}

Variant::Variant(VariantMapInt32Key &&v)
    : _type(Type::INT32_KEY_MAP)
{
  _field.int32KeyMapVal = new (std::nothrow) VariantMapInt32Key();
  *_field.int32KeyMapVal = std::move(v);
}

Variant::Variant(const Variant &other)
    : _type(Type::NONE)
{
  *this = other;
}

Variant::Variant(Variant &&other)
    : _type(Type::NONE)
{
  *this = std::move(other);
}

Variant::~Variant()
{
  clear();
}

Variant &Variant::operator=(const Variant &other)
{
  if (this != &other)
  {
    reset(other._type);

    switch (other._type)
    {
    case Type::BYTE:
      _field.byteVal = other._field.byteVal;
      break;
    case Type::INT32:
      _field.int32Val = other._field.int32Val;
      break;
    case Type::UINT32:
      _field.unsigned32Val = other._field.unsigned32Val;
      break;
    case Type::FLOAT:
      _field.floatVal = other._field.floatVal;
      break;
    case Type::DOUBLE:
      _field.doubleVal = other._field.doubleVal;
      break;
    case Type::BOOLEAN:
      _field.boolVal = other._field.boolVal;
      break;
    case Type::STRING:
      if (_field.strVal == nullptr)
      {
        _field.strVal = new string();
      }
      *_field.strVal = *other._field.strVal;
      break;
    case Type::VECTOR:
      if (_field.vectorVal == nullptr)
      {
        _field.vectorVal = new (std::nothrow) VariantVector();
      }
      *_field.vectorVal = *other._field.vectorVal;
      break;
    case Type::MAP:
      if (_field.mapVal == nullptr)
      {
        _field.mapVal = new (std::nothrow) VariantMap();
      }
      *_field.mapVal = *other._field.mapVal;
      break;
    case Type::INT32_KEY_MAP:
      if (_field.int32KeyMapVal == nullptr)  
      {
        _field.int32KeyMapVal = new (std::nothrow) VariantMapInt32Key();
      }
      *_field.int32KeyMapVal = *other._field.int32KeyMapVal;
      break;
    default:
      break;
    }
  }
  return *this;
}

Variant &Variant::operator=(Variant &&other)
{
  if (this != &other)
  {
    clear();
    switch (other._type)
    {
    case Type::BYTE:
      _field.byteVal = other._field.byteVal;
      break;
    case Type::INT32:
      _field.int32Val = other._field.int32Val;
      break;
    case Type::UINT32:
      _field.unsigned32Val = other._field.unsigned32Val;
      break;
    case Type::FLOAT:
      _field.floatVal = other._field.floatVal;
      break;
    case Type::DOUBLE:
      _field.doubleVal = other._field.doubleVal;
      break;
    case Type::BOOLEAN:
      _field.boolVal = other._field.boolVal;
      break;
    case Type::STRING:
      _field.strVal = other._field.strVal;
      break;
    case Type::VECTOR:
      _field.vectorVal = other._field.vectorVal;
      break;
    case Type::MAP:
      _field.mapVal = other._field.mapVal;
      break;
    case Type::INT32_KEY_MAP:
      _field.int32KeyMapVal = other._field.int32KeyMapVal;
      break;
    default:
      break;
    }
    _type = other._type;

    memset(&other._field, 0, sizeof(other._field));
    other._type = Type::NONE;
  }

  return *this;
}

Variant &Variant::operator=(unsigned char v)
{
  reset(Type::BYTE);
  _field.byteVal = v;
  return *this;
}

Variant &Variant::operator=(int32_t v)
{
  reset(Type::INT32); 
  _field.int32Val = v;
  return *this;
}

Variant &Variant::operator=(uint32_t v)
{
  reset(Type::UINT32); 
  _field.unsigned32Val = v;
  return *this;
}

Variant &Variant::operator=(float v)
{
  reset(Type::FLOAT);
  _field.floatVal = v;
  return *this;
}

Variant &Variant::operator=(double v)
{
  reset(Type::DOUBLE);
  _field.doubleVal = v;
  return *this;
}

Variant &Variant::operator=(bool v)
{
  reset(Type::BOOLEAN);
  _field.boolVal = v;
  return *this;
}

Variant &Variant::operator=(const char *v)
{
  reset(Type::STRING);
  *_field.strVal = v ? v : "";
  return *this;
}

Variant &Variant::operator=(const string &v)
{
  reset(Type::STRING);
  *_field.strVal = v;
  return *this;
}

Variant &Variant::operator=(const VariantVector &v)
{
  reset(Type::VECTOR);
  *_field.vectorVal = v;
  return *this;
}

Variant &Variant::operator=(VariantVector &&v)
{
  reset(Type::VECTOR);
  *_field.vectorVal = std::move(v);
  return *this;
}

Variant &Variant::operator=(const VariantMap &v)
{
  reset(Type::MAP);
  *_field.mapVal = v;
  return *this;
}

Variant &Variant::operator=(VariantMap &&v)
{
  reset(Type::MAP);
  *_field.mapVal = std::move(v);
  return *this;
}

Variant &Variant::operator=(const VariantMapInt32Key &v) 
{
  reset(Type::INT32_KEY_MAP);
  *_field.int32KeyMapVal = v;
  return *this;
}

Variant &Variant::operator=(VariantMapInt32Key &&v)
{
  reset(Type::INT32_KEY_MAP);
  *_field.int32KeyMapVal = std::move(v);
  return *this;
}

bool Variant::operator!=(const Variant &v)
{
  return !(*this == v);
}
bool Variant::operator!=(const Variant &v) const
{
  return !(*this == v);
}

bool Variant::operator==(const Variant &v)
{
  const auto &t = *this;
  return t == v;
}
bool Variant::operator==(const Variant &v) const
{
  if (this == &v)
    return true;
  if (v._type != this->_type)
    return false;
  if (this->isNull())
    return true;
  switch (_type)
  {
  case Type::BYTE:
    return v._field.byteVal == this->_field.byteVal;
  case Type::INT32:
    return v._field.int32Val == this->_field.int32Val;
  case Type::UINT32:
    return v._field.unsigned32Val == this->_field.unsigned32Val;
  case Type::BOOLEAN:
    return v._field.boolVal == this->_field.boolVal;
  case Type::STRING:
    return *v._field.strVal == *this->_field.strVal;
  case Type::FLOAT:
    return ::abs(v._field.floatVal - this->_field.floatVal) <= FLT_EPSILON;
  case Type::DOUBLE:
    return ::abs(v._field.doubleVal - this->_field.doubleVal) <= DBL_EPSILON;
  case Type::VECTOR:
  {
    const auto &v1 = *(this->_field.vectorVal);
    const auto &v2 = *(v._field.vectorVal);
    const auto size = v1.size();
    if (size == v2.size())
    {
      for (size_t i = 0; i < size; i++)
      {
        if (v1[i] != v2[i])
          return false;
      }
      return true;
    }
    return false;
  }
  case Type::MAP:
  {
    const auto &map1 = *(this->_field.mapVal);
    const auto &map2 = *(v._field.mapVal);
    for (const auto &kvp : map1)
    {
      auto it = map2.find(kvp.first);
      if (it == map2.end() || it->second != kvp.second)
      {
        return false;
      }
    }
    return true;
  }
  case Type::INT32_KEY_MAP:
  {
    const auto &map1 = *(this->_field.int32KeyMapVal);
    const auto &map2 = *(v._field.int32KeyMapVal);
    for (const auto &kvp : map1)
    {
      auto it = map2.find(kvp.first);
      if (it == map2.end() || it->second != kvp.second)
      {
        return false;
      }
    }
    return true;
  }
  default:
    break;
  };

  return false;
}

/// Convert value to a specified type
unsigned char Variant::asByte() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");

  if (_type == Type::BYTE)
  {
    return _field.byteVal;
  }

  if (_type == Type::INT32)
  {
    return static_cast<unsigned char>(_field.int32Val);
  }

  if (_type == Type::UINT32)
  {
    return static_cast<unsigned char>(_field.unsigned32Val);
  }

  if (_type == Type::STRING)
  {
    return static_cast<unsigned char>(atoi(_field.strVal->c_str()));
  }

  if (_type == Type::FLOAT)
  {
    return static_cast<unsigned char>(_field.floatVal);
  }

  if (_type == Type::DOUBLE)
  {
    return static_cast<unsigned char>(_field.doubleVal);
  }

  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal ? 1 : 0;
  }

  return 0;
}

int32_t Variant::asInt32() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");
  if (_type == Type::INT32)
  {
    return _field.int32Val;
  }

  if (_type == Type::UINT32)
  {
    //CCASSERT(_field.unsigned32Val < INT_MAX, "Can only convert values < INT_MAX");
    return (int32_t)_field.unsigned32Val;
  }

  if (_type == Type::BYTE)
  {
    return _field.byteVal;
  }

  if (_type == Type::STRING)
  {
    return atoi(_field.strVal->c_str());
  }

  if (_type == Type::FLOAT)
  {
    return static_cast<int32_t>(_field.floatVal);
  }

  if (_type == Type::DOUBLE)
  {
    return static_cast<int32_t>(_field.doubleVal);
  }

  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal ? 1 : 0;
  }

  return 0;
}

uint32_t Variant::asUnsignedInt32() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");
  if (_type == Type::UINT32)
  {
    return _field.unsigned32Val;
  }

  if (_type == Type::INT32)
  {
    //CCASSERT(_field.int32Val >= 0, "Only values >= 0 can be converted to unsigned");
    return static_cast<uint32_t>(_field.int32Val);
  }

  if (_type == Type::BYTE)
  {
    return static_cast<uint32_t>(_field.byteVal);
  }

  if (_type == Type::STRING)
  {
    // NOTE: strtoul is required (need to augment on unsupported platforms)
    return static_cast<uint32_t>(strtoul(_field.strVal->c_str(), nullptr, 10));
  }

  if (_type == Type::FLOAT)
  {
    return static_cast<uint32_t>(_field.floatVal);
  }

  if (_type == Type::DOUBLE)
  {
    return static_cast<uint32_t>(_field.doubleVal);
  }

  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal ? 1u : 0u;
  }

  return 0u;
}

float Variant::asFloat() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");
  if (_type == Type::FLOAT)
  {
    return _field.floatVal;
  }

  if (_type == Type::BYTE)
  {
    return static_cast<float>(_field.byteVal);
  }

  if (_type == Type::STRING)
  {
    return atof(_field.strVal->c_str());
  }

  if (_type == Type::INT32)
  {
    return static_cast<float>(_field.int32Val);
  }

  if (_type == Type::UINT32)
  {
    return static_cast<float>(_field.unsigned32Val);
  }

  if (_type == Type::DOUBLE)
  {
    return static_cast<float>(_field.doubleVal);
  }

  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal ? 1.0f : 0.0f;
  }

  return 0.0f;
}

double Variant::asDouble() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");
  if (_type == Type::DOUBLE)
  {
    return _field.doubleVal;
  }

  if (_type == Type::BYTE)
  {
    return static_cast<double>(_field.byteVal);
  }

  if (_type == Type::STRING)
  {
    return static_cast<double>(atof(_field.strVal->c_str()));
  }

  if (_type == Type::INT32)
  {
    return static_cast<double>(_field.int32Val);
  }

  if (_type == Type::UINT32)
  {
    return static_cast<double>(_field.unsigned32Val);
  }

  if (_type == Type::FLOAT)
  {
    return static_cast<double>(_field.floatVal);
  }

  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal ? 1.0 : 0.0;
  }

  return 0.0;
}

bool Variant::asBool() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");
  if (_type == Type::BOOLEAN)
  {
    return _field.boolVal;
  }

  if (_type == Type::BYTE)
  {
    return _field.byteVal == 0 ? false : true;
  }

  if (_type == Type::STRING)
  {
    return (*_field.strVal == "0" || *_field.strVal == "false") ? false : true;
  }

  if (_type == Type::INT32)
  {
    return _field.int32Val == 0 ? false : true;
  }

  if (_type == Type::UINT32)
  {
    return _field.unsigned32Val == 0 ? false : true;
  }

  if (_type == Type::FLOAT)
  {
    return _field.floatVal == 0.0f ? false : true;
  }

  if (_type == Type::DOUBLE)
  {
    return _field.doubleVal == 0.0 ? false : true;
  }

  return false;
}

string Variant::asString() const
{
  //CCASSERT(_type != Type::VECTOR && _type != Type::MAP && _type != Type::INT32_KEY_MAP, "Only base type (bool, string, float, double, int32_t) could be converted");

  if (_type == Type::STRING)
  {
    return *_field.strVal;
  }

  std::stringstream ret;

  switch (_type)
  {
  case Type::BYTE:
    ret << _field.byteVal;
    break;
  case Type::INT32:
    ret << _field.int32Val;
    break;
  case Type::UINT32:
    ret << _field.unsigned32Val;
    break;
  case Type::FLOAT:
    ret << std::fixed << std::setprecision(7) << _field.floatVal;
    break;
  case Type::DOUBLE:
    ret << std::fixed << std::setprecision(16) << _field.doubleVal;
    break;
  case Type::BOOLEAN:
    ret << (_field.boolVal ? "true" : "false");
    break;
  default:
    break;
  }
  return ret.str();
}

VariantVector &Variant::asVariantVector()
{
  //CCASSERT(_type == Type::VECTOR, "The value type isn't Type::VECTOR");
  return *_field.vectorVal;
}

const VariantVector &Variant::asVariantVector() const
{
  //CCASSERT(_type == Type::VECTOR, "The value type isn't Type::VECTOR");
  return *_field.vectorVal;
}

VariantMap &Variant::asVariantMap()
{
  //CCASSERT(_type == Type::MAP, "The value type isn't Type::MAP");
  return *_field.mapVal;
}

const VariantMap &Variant::asVariantMap() const
{
  //CCASSERT(_type == Type::MAP, "The value type isn't Type::MAP");
  return *_field.mapVal;
}

VariantMapInt32Key &Variant::asInt32KeyMap() 
{
  //CCASSERT(_type == Type::INT32_KEY_MAP, "The value type isn't Type::INT32_KEY_MAP");
  return *_field.int32KeyMapVal;
}

const VariantMapInt32Key &Variant::asInt32KeyMap() const
{
  //CCASSERT(_type == Type::INT32_KEY_MAP, "The value type isn't Type::INT32_KEY_MAP");
  return *_field.int32KeyMapVal;
}

static string getTabs(int32_t depth)
{
  string tabWidth;

  for (int32_t i = 0; i < depth; ++i)
  {
    tabWidth += "\t";
  }

  return tabWidth;
}

static string visit(const Variant &v, int32_t depth);

static string visitVector(const VariantVector &v, int32_t depth)
{
  std::stringstream ret;

  if (depth > 0)
    ret << "\n";

  ret << getTabs(depth) << "[\n";

  int32_t i = 0;
  for (const auto &child : v)
  {
    ret << getTabs(depth + 1) << i << ": " << visit(child, depth + 1);
    ++i;
  }

  ret << getTabs(depth) << "]\n";

  return ret.str();
}

template <class T>
static string visitMap(const T &v, int32_t depth)
{
  std::stringstream ret;

  if (depth > 0)
    ret << "\n";

  ret << getTabs(depth) << "{\n";

  for (auto &iter : v)
  {
    ret << getTabs(depth + 1) << iter.first << ": ";
    ret << visit(iter.second, depth + 1);
  }

  ret << getTabs(depth) << "}\n";

  return ret.str();
}

static string visit(const Variant &v, int32_t depth)
{
  std::stringstream ret;

  switch (v.getType())
  {
  case Variant::Type::NONE:
  case Variant::Type::BYTE:
  case Variant::Type::INT32:
  case Variant::Type::UINT32:
  case Variant::Type::FLOAT:
  case Variant::Type::DOUBLE:
  case Variant::Type::BOOLEAN:
  case Variant::Type::STRING:
    ret << v.asString() << "\n";
    break;
  case Variant::Type::VECTOR:
    ret << visitVector(v.asVariantVector(), depth);
    break;
  case Variant::Type::MAP:
    ret << visitMap(v.asVariantMap(), depth);
    break;
  case Variant::Type::INT32_KEY_MAP:
    ret << visitMap(v.asInt32KeyMap(), depth);
    break;
  default:
    //CCASSERT(false, "Invalid type!");
    break;
  }

  return ret.str();
}

string Variant::getDescription() const
{
  string ret("\n");
  ret += visit(*this, 0);
  return ret;
}

void Variant::clear()
{
  // Free memory the old value allocated
  switch (_type)
  {
  case Type::BYTE:
    _field.byteVal = 0;
    break;
  case Type::INT32:
    _field.int32Val = 0;
    break;
  case Type::UINT32:
    _field.unsigned32Val = 0u;
    break;
  case Type::FLOAT:
    _field.floatVal = 0.0f;
    break;
  case Type::DOUBLE:
    _field.doubleVal = 0.0;
    break;
  case Type::BOOLEAN:
    _field.boolVal = false;
    break;
  case Type::STRING:
    BASE_SAFE_DELETE(_field.strVal);
    break;
  case Type::VECTOR:
    BASE_SAFE_DELETE(_field.vectorVal);
    break;
  case Type::MAP:
    BASE_SAFE_DELETE(_field.mapVal);
    break;
  case Type::INT32_KEY_MAP:
    BASE_SAFE_DELETE(_field.int32KeyMapVal);
    break;
  default:
    break;
  }

  _type = Type::NONE;
}

void Variant::reset(Type type)
{
  if (_type == type)
    return;

  clear();

  // Allocate memory for the new value
  switch (type)
  {
  case Type::STRING:
    _field.strVal = new (std::nothrow) string();
    break;
  case Type::VECTOR:
    _field.vectorVal = new (std::nothrow) VariantVector();
    break;
  case Type::MAP:
    _field.mapVal = new (std::nothrow) VariantMap();
    break;
  case Type::INT32_KEY_MAP:
    _field.int32KeyMapVal = new (std::nothrow) VariantMapInt32Key();
    break;
  default:
    break;
  }

  _type = type;
}

} // namespace util
} // namespace mycc