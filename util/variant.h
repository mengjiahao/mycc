#ifndef MYCC_UTIL_VARIANT_H_
#define MYCC_UTIL_VARIANT_H_

#include <string>
#include <unordered_map>
#include <vector>
#include "types_util.h"

namespace mycc
{
namespace util
{

class Variant;

typedef std::vector<Variant> VariantVector;
typedef std::unordered_map<string, Variant> VariantMap;
typedef std::unordered_map<int32_t, Variant> VariantMapInt32Key;

extern const VariantVector VariantVectorNull;
extern const VariantMap VariantMapNull;
extern const VariantMapInt32Key VariantMapInt32KeyNull;

/*
 * This class is provide as a wrapper of basic types, such as int32_t and bool.
 */
class Variant
{
public:
  /** A predefined Variant that has not value. */
  static const Variant Null;

  /** Default constructor. */
  Variant();

  /** Create a Variant by an unsigned char value. */
  explicit Variant(unsigned char v);

  /** Create a Variant by an integer value. */
  explicit Variant(int32_t v);

  /** Create a Variant by an unsigned value. */
  explicit Variant(uint32_t v);

  /** Create a Variant by a float value. */
  explicit Variant(float v);

  /** Create a Variant by a double value. */
  explicit Variant(double v);

  /** Create a Variant by a bool value. */
  explicit Variant(bool v);

  /** Create a Variant by a char pointer. It will copy the chars internally. */
  explicit Variant(const char *v);

  /** Create a Variant by a string. */
  explicit Variant(const string &v);

  /** Create a Variant by a VariantVector object. */
  explicit Variant(const VariantVector &v);
  /** Create a Variant by a VariantVector object. It will use std::move internally. */
  explicit Variant(VariantVector &&v);

  /** Create a Variant by a VariantMap object. */
  explicit Variant(const VariantMap &v);
  /** Create a Variant by a VariantMap object. It will use std::move internally. */
  explicit Variant(VariantMap &&v);

  /** Create a Variant by a VariantMapIntKey object. */
  explicit Variant(const VariantMapInt32Key &v);
  /** Create a Variant by a VariantMapIntKey object. It will use std::move internally. */
  explicit Variant(VariantMapInt32Key &&v);

  /** Create a Variant by another Variant object. */
  Variant(const Variant &other);
  /** Create a Variant by a Variant object. It will use std::move internally. */
  Variant(Variant &&other);

  /** Destructor. */
  ~Variant();

  /** Assignment operator, assign from Variant to Variant. */
  Variant &operator=(const Variant &other);
  /** Assignment operator, assign from Variant to Variant. It will use std::move internally. */
  Variant &operator=(Variant &&other);

  /** Assignment operator, assign from unsigned char to Variant. */
  Variant &operator=(unsigned char v);
  /** Assignment operator, assign from integer to Variant. */
  Variant &operator=(int32_t v);
  /** Assignment operator, assign from integer to Variant. */
  Variant &operator=(uint32_t v);
  /** Assignment operator, assign from float to Variant. */
  Variant &operator=(float v);
  /** Assignment operator, assign from double to Variant. */
  Variant &operator=(double v);
  /** Assignment operator, assign from bool to Variant. */
  Variant &operator=(bool v);
  /** Assignment operator, assign from char* to Variant. */
  Variant &operator=(const char *v);
  /** Assignment operator, assign from string to Variant. */
  Variant &operator=(const string &v);

  /** Assignment operator, assign from VariantVector to Variant. */
  Variant &operator=(const VariantVector &v);
  /** Assignment operator, assign from VariantVector to Variant. */
  Variant &operator=(VariantVector &&v);

  /** Assignment operator, assign from VariantMap to Variant. */
  Variant &operator=(const VariantMap &v);
  /** Assignment operator, assign from VariantMap to Variant. It will use std::move internally. */
  Variant &operator=(VariantMap &&v);

  /** Assignment operator, assign from VariantMapIntKey to Variant. */
  Variant &operator=(const VariantMapInt32Key &v);
  /** Assignment operator, assign from VariantMapIntKey to Variant. It will use std::move internally. */
  Variant &operator=(VariantMapInt32Key &&v);

  /** != operator overloading */
  bool operator!=(const Variant &v);
  /** != operator overloading */
  bool operator!=(const Variant &v) const;
  /** == operator overloading */
  bool operator==(const Variant &v);
  /** == operator overloading */
  bool operator==(const Variant &v) const;

  /** Gets as a byte value. Will convert to unsigned char if possible, or will trigger assert error. */
  unsigned char asByte() const;
  /** Gets as an integer value. Will convert to integer if possible, or will trigger assert error. */
  int32_t asInt32() const;
  /** Gets as an unsigned value. Will convert to unsigned if possible, or will trigger assert error. */
  uint32_t asUnsignedInt32() const;
  /** Gets as a float value. Will convert to float if possible, or will trigger assert error. */
  float asFloat() const;
  /** Gets as a double value. Will convert to double if possible, or will trigger assert error. */
  double asDouble() const;
  /** Gets as a bool value. Will convert to bool if possible, or will trigger assert error. */
  bool asBool() const;
  /** Gets as a string value. Will convert to string if possible, or will trigger assert error. */
  string asString() const;

  /** Gets as a VariantVector reference. Will convert to VariantVector if possible, or will trigger assert error. */
  VariantVector &asVariantVector();
  /** Gets as a const VariantVector reference. Will convert to VariantVector if possible, or will trigger assert error. */
  const VariantVector &asVariantVector() const;

  /** Gets as a VariantMap reference. Will convert to VariantMap if possible, or will trigger assert error. */
  VariantMap &asVariantMap();
  /** Gets as a const VariantMap reference. Will convert to VariantMap if possible, or will trigger assert error. */
  const VariantMap &asVariantMap() const;

  /** Gets as a VariantMapIntKey reference. Will convert to VariantMapIntKey if possible, or will trigger assert error. */
  VariantMapInt32Key &asInt32KeyMap();
  /** Gets as a const VariantMapIntKey reference. Will convert to VariantMapIntKey if possible, or will trigger assert error. */
  const VariantMapInt32Key &asInt32KeyMap() const;

  /**
     * Checks if the Variant is null.
     * @return True if the Variant is null, false if not.
     */
  bool isNull() const { return _type == Type::NONE; }

  /** Variant type wrapped by Variant. */
  enum class Type
  {
    /// no value is wrapped, an empty Variant
    NONE = 0,
    /// wrap byte
    BYTE,
    /// wrap integer32
    INT32,
    /// wrap unsigned32
    UINT32,
    /// wrap float
    FLOAT,
    /// wrap double
    DOUBLE,
    /// wrap bool
    BOOLEAN,
    /// wrap string
    STRING,
    /// wrap vector
    VECTOR,
    /// wrap VariantMap
    MAP,
    /// wrap VariantMapInt32Key
    INT32_KEY_MAP
  };

  /** Gets the value type. */
  Type getType() const { return _type; }

  /** Gets the description of the class. */
  string getDescription() const;

private:
  void clear();
  void reset(Type type);

  union {
    unsigned char byteVal;
    int32_t int32Val;
    uint32_t unsigned32Val;
    float floatVal;
    double doubleVal;
    bool boolVal;

    string *strVal;
    VariantVector *vectorVal;
    VariantMap *mapVal;
    VariantMapInt32Key *int32KeyMapVal;
  } _field;

  Type _type;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_VARIANT_H_