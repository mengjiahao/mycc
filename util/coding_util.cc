
#include "coding_util.h"

namespace mycc
{
namespace util
{

void EncodeFixed16(char *buf, uint16_t value)
{
  if (port::kLittleEndian)
  {
    memcpy(buf, &value, sizeof(value));
  }
  else
  {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
  }
}

void EncodeFixed32(char *buf, uint32_t value)
{
  if (port::kLittleEndian)
  {
    memcpy(buf, &value, sizeof(value));
  }
  else
  {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
  }
}

void EncodeFixed64(char *buf, uint64_t value)
{
  if (port::kLittleEndian)
  {
    memcpy(buf, &value, sizeof(value));
  }
  else
  {
    buf[0] = value & 0xff;
    buf[1] = (value >> 8) & 0xff;
    buf[2] = (value >> 16) & 0xff;
    buf[3] = (value >> 24) & 0xff;
    buf[4] = (value >> 32) & 0xff;
    buf[5] = (value >> 40) & 0xff;
    buf[6] = (value >> 48) & 0xff;
    buf[7] = (value >> 56) & 0xff;
  }
}

char *EncodeVarint32(char *dst, uint32_t v)
{
  // Operate on characters as unsigneds
  unsigned char *ptr = reinterpret_cast<unsigned char *>(dst);
  static const int32_t B = 128;
  if (v < (1 << 7))
  {
    *(ptr++) = v;
  }
  else if (v < (1 << 14))
  {
    *(ptr++) = v | B;
    *(ptr++) = v >> 7;
  }
  else if (v < (1 << 21))
  {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = v >> 14;
  }
  else if (v < (1 << 28))
  {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = (v >> 14) | B;
    *(ptr++) = v >> 21;
  }
  else
  {
    *(ptr++) = v | B;
    *(ptr++) = (v >> 7) | B;
    *(ptr++) = (v >> 14) | B;
    *(ptr++) = (v >> 21) | B;
    *(ptr++) = v >> 28;
  }
  return reinterpret_cast<char *>(ptr);
}

char *EncodeVarint64(char *dst, uint64_t v)
{
  static const int32_t B = 128;
  unsigned char *ptr = reinterpret_cast<unsigned char *>(dst);
  while (v >= B)
  {
    *(ptr++) = (v & (B - 1)) | B;
    v >>= 7;
  }
  *(ptr++) = static_cast<unsigned char>(v);
  return reinterpret_cast<char *>(ptr);
}

void PutFixed16(string *dst, uint16_t value)
{
  char buf[sizeof(value)];
  EncodeFixed16(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutFixed32(string *dst, uint32_t value)
{
  char buf[sizeof(value)];
  EncodeFixed32(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutFixed64(string *dst, uint64_t value)
{
  char buf[sizeof(value)];
  EncodeFixed64(buf, value);
  dst->append(buf, sizeof(buf));
}

void PutVarint32(string *dst, uint32_t v)
{
  char buf[5];
  char *ptr = EncodeVarint32(buf, v);
  dst->append(buf, ptr - buf);
}

void PutVarint64(string *dst, uint64_t v)
{
  char buf[10];
  char *ptr = EncodeVarint64(buf, v);
  dst->append(buf, ptr - buf);
}

void PutVarint32Varint32(string *dst, uint32_t v1, uint32_t v2)
{
  char buf[10];
  char *ptr = EncodeVarint32(buf, v1);
  ptr = EncodeVarint32(ptr, v2);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

void PutVarint32Varint32Varint32(string *dst, uint32_t v1,
                                 uint32_t v2, uint32_t v3)
{
  char buf[15];
  char *ptr = EncodeVarint32(buf, v1);
  ptr = EncodeVarint32(ptr, v2);
  ptr = EncodeVarint32(ptr, v3);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

void PutVarint64Varint64(string *dst, uint64_t v1, uint64_t v2)
{
  char buf[20];
  char *ptr = EncodeVarint64(buf, v1);
  ptr = EncodeVarint64(ptr, v2);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

void PutVarint32Varint64(string *dst, uint32_t v1, uint64_t v2)
{
  char buf[15];
  char *ptr = EncodeVarint32(buf, v1);
  ptr = EncodeVarint64(ptr, v2);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

void PutVarint32Varint32Varint64(string *dst, uint32_t v1,
                                 uint32_t v2, uint64_t v3)
{
  char buf[20];
  char *ptr = EncodeVarint32(buf, v1);
  ptr = EncodeVarint32(ptr, v2);
  ptr = EncodeVarint64(ptr, v3);
  dst->append(buf, static_cast<size_t>(ptr - buf));
}

void PutLengthPrefixedString(string *dst, const string &value)
{
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}

void PutLengthPrefixedStringPiece(string *dst, const StringPiece &value)
{
  PutVarint32(dst, value.size());
  dst->append(value.data(), value.size());
}

void GetFixed32(string *dst, uint32_t *value)
{
  *value = DecodeFixed32(dst->data());
  dst->erase(0, sizeof(uint32_t));
}

void GetFixed64(string *dst, uint64_t *value)
{
  *value = DecodeFixed64(dst->data());
  dst->erase(0, sizeof(uint64_t));
}

bool GetFixed32(StringPiece *input, uint32_t *value)
{
  if (input->size() < sizeof(uint32_t))
  {
    return false;
  }
  *value = DecodeFixed32(input->data());
  input->remove_prefix(sizeof(uint32_t));
  return true;
}

bool GetFixed64(StringPiece *input, uint64_t *value)
{
  if (input->size() < sizeof(uint64_t))
  {
    return false;
  }
  *value = DecodeFixed64(input->data());
  input->remove_prefix(sizeof(uint64_t));
  return true;
}

bool GetVarint32(string *input, uint32_t *value)
{
  const char *p = input->data();
  const char *limit = p + input->size();
  const char *q = GetVarint32Ptr(p, limit, value);
  if (q == NULL)
  {
    return false;
  }
  else
  {
    (*input).erase(0, q - p);
    return true;
  }
}

bool GetVarint64(string *input, uint64_t *value)
{
  const char *p = input->data();
  const char *limit = p + input->size();
  const char *q = GetVarint64Ptr(p, limit, value);
  if (q == NULL)
  {
    return false;
  }
  else
  {
    (*input).erase(0, q - p);
    return true;
  }
}

bool GetVarint32(StringPiece *input, uint32_t *value)
{
  const char *p = input->data();
  const char *limit = p + input->size();
  const char *q = GetVarint32Ptr(p, limit, value);
  if (q == nullptr)
  {
    return false;
  }
  else
  {
    *input = StringPiece(q, static_cast<size_t>(limit - q));
    return true;
  }
}

bool GetVarint64(StringPiece *input, uint64_t *value)
{
  const char *p = input->data();
  const char *limit = p + input->size();
  const char *q = GetVarint64Ptr(p, limit, value);
  if (q == nullptr)
  {
    return false;
  }
  else
  {
    *input = StringPiece(q, static_cast<size_t>(limit - q));
    return true;
  }
}

bool GetLengthPrefixedStringPiece(StringPiece *input, StringPiece *result)
{
  uint32_t len = 0;
  if (GetVarint32(input, &len) && input->size() >= len)
  {
    *result = StringPiece(input->data(), len);
    input->remove_prefix(len);
    return true;
  }
  else
  {
    return false;
  }
}

StringPiece GetLengthPrefixedStringPiece(const char *data)
{
  uint32_t len = 0;
  // +5: we assume "data" is not corrupted
  // unsigned char is 7 bits, uint32_t is 32 bits, need 5 unsigned char
  auto p = GetVarint32Ptr(data, data + 5 /* limit */, &len);
  return StringPiece(p, len);
}

StringPiece GetStringPieceUntil(StringPiece *slice, char delimiter)
{
  uint32_t len = 0;
  for (len = 0; len < slice->size() && slice->data()[len] != delimiter; ++len)
  {
    // nothing
  }

  StringPiece ret(slice->data(), len);
  slice->remove_prefix(len + ((len < slice->size()) ? 1 : 0));
  return ret;
}

bool GetLengthPrefixedString(string *input, string *result)
{
  uint32_t len;
  if (GetVarint32(input, &len) &&
      input->size() >= len)
  {
    *result = (*input).substr(0, len);
    input->erase(0, len);
    return true;
  }
  else
  {
    return false;
  }
}

const char *GetVarint32PtrFallback(const char *p, const char *limit,
                                   uint32_t *value)
{
  uint32_t result = 0;
  for (uint32_t shift = 0; shift <= 28 && p < limit; shift += 7)
  {
    uint32_t byte = *(reinterpret_cast<const unsigned char *>(p));
    p++;
    if (byte & 128)
    {
      // More bytes are present
      result |= ((byte & 127) << shift);
    }
    else
    {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char *>(p);
    }
  }
  return nullptr;
}

const char *GetVarint32Ptr(const char *p, const char *limit,
                           uint32_t *value)
{
  if (p < limit)
  {
    uint32_t result = *(reinterpret_cast<const unsigned char *>(p));
    if ((result & 128) == 0)
    {
      *value = result;
      return p + 1;
    }
  }
  return GetVarint32PtrFallback(p, limit, value);
}

const char *GetVarint64Ptr(const char *p, const char *limit, uint64_t *value)
{
  uint64_t result = 0;
  for (uint32_t shift = 0; shift <= 63 && p < limit; shift += 7)
  {
    uint64_t byte = *(reinterpret_cast<const unsigned char *>(p));
    p++;
    if (byte & 128)
    {
      // More bytes are present
      result |= ((byte & 127) << shift);
    }
    else
    {
      result |= (byte << shift);
      *value = result;
      return reinterpret_cast<const char *>(p);
    }
  }
  return nullptr;
}

int32_t VarintLength(uint64_t v)
{
  int32_t len = 1;
  while (v >= 128)
  {
    v >>= 7;
    len++;
  }
  return len;
}

uint64_t EndianTransform(uint64_t input, uint64_t size)
{
  char *pos = reinterpret_cast<char *>(&input);
  uint64_t ret_val = 0;
  for (uint64_t i = 0; i < size; ++i)
  {
    ret_val |= (static_cast<uint64_t>(static_cast<unsigned char>(pos[i]))
                << ((size - i - 1) << 3));
  }
  return ret_val;
}

} // namespace util
} // namespace mycc