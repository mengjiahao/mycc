
#include "bloom.h"
#include <assert.h>
#include "atomic_util.h"
#include "coding_util.h"
#include "env_util.h"
#include "hash_util.h"
#include "math_util.h"
#include "string_util.h"

namespace mycc
{
namespace util
{

namespace
{ // namespace anonymous

bool OrBit(uint8_t *ptr, uint8_t mask, void *ret)
{
  uint8_t old_v = *ptr;
  *ptr = old_v | mask;
  return true;
}

bool OrBitConcurrently(uint8_t *ptr, uint8_t mask, void *ret)
{
  if ((mask & AtomicLoadN<uint8_t>(ptr, MEMORY_ORDER_ATOMIC_RELAXED)) != mask)
  {
    AtomicOrFetch<uint8_t>(ptr, mask, MEMORY_ORDER_ATOMIC_RELAXED);
  }
  return true;
}

bool CheckBit(uint8_t *ptr, uint8_t mask, void *ret)
{
  if (((*ptr) & mask) == 0)
    return false;
  return true;
}

bool CheckBitConcurrently(uint8_t *ptr, uint8_t mask, void *ret)
{
  if ((AtomicLoadN<uint8_t>(ptr, MEMORY_ORDER_ATOMIC_RELAXED) & mask) == 0)
    return false;
  return true;
}

} // namespace

uint64_t BloomFilter::BloomHash(const StringPiece &key)
{
  return Hash64(key.data(), key.size(), 0xbc9f1d34);
}

string BloomFilter::toString()
{
  string result;
  uint64_t bytes = getTotalBytes();
  uint64_t bits1_count = count1Bits();
  StringFormatTo(&result, "BloomFilter@%p { data: %p, total_bits=%" PRIu64 ", num_probes=%u, bytes=%" PRIu64 ", bits1_count=%" PRIu64 " }",
                 this, data_, total_bits_, num_probes_, bytes, bits1_count);
  return result;
}

void BloomFilter::initialize(uint64_t total_bits, uint32_t num_probes)
{
  // total_bits_ must >= total_bits
  total_bits_ = Roundup(total_bits, 64); // make sure is byte roundup
  num_probes_ = num_probes;

  uint64_t bytes = getTotalBytes();
  char *raw = static_cast<char *>(calloc(bytes, sizeof(uint8_t)));

  if (nullptr != data_)
  {
    free(data_);
    data_ = nullptr;
  }
  data_ = reinterpret_cast<uint8_t *>(raw);
}

void BloomFilter::destroy()
{
  total_bits_ = 0;
  num_probes_ = 1;
  if (nullptr != data_)
  {
    free(data_);
    data_ = nullptr;
  }
}

void BloomFilter::clear()
{
  uint64_t bytes = getTotalBytes();
  memset(data_, 0, bytes * sizeof(uint8_t));
}

void BloomFilter::add(const StringPiece &key)
{
  uint64_t hash = hash_func_(key);
  operByteHash(hash, OrBit);
}

void BloomFilter::addConcurrently(const StringPiece &key)
{
  uint64_t hash = hash_func_(key);
  operByteHash(hash, OrBitConcurrently);
}

bool BloomFilter::mayContain(const StringPiece &key)
{
  bool ret = false;
  uint64_t hash = hash_func_(key);
  ret = operByteHash(hash, CheckBit);
  return ret;
}

bool BloomFilter::mayContainConcurrently(const StringPiece &key)
{
  bool ret = false;
  uint64_t hash = hash_func_(key);
  ret = operByteHash(hash, CheckBitConcurrently);
  return ret;
}

bool BloomFilter::operByteHash(uint64_t h, ByteOperFunc byteOperFunc)
{
  assert(isInitialized());

  const uint64_t delta = (h >> 17) | (h << 15); // Rotate right 17 bits
  for (uint32_t i = 0; i < num_probes_; ++i)
  {
    const uint64_t bitpos = h % total_bits_;
    if (!byteOperFunc(&data_[bitpos >> 3], (1 << (bitpos & 7)), nullptr))
      return false;
    h += delta;
  }
  return true;
}

uint64_t BloomFilter::count1Bits()
{
  uint64_t count = 0;
  uint64_t bytes = getTotalBytes();
  for (uint64_t i = 0; i < bytes; ++i)
  {
    count += Count1Bits(data_[i]);
  }
  return count;
}

Status BloomFilter::load(const string &filename)
{
  Status s;

  std::unique_ptr<SequentialFile> load_file;
  EnvOptions env_options;
  s = env_->NewSequentialFile(filename, &load_file, env_options);
  if (!s.ok())
  {
    return s;
  }

  StringPiece slice;
  char buf[kDumpFileHeaderSize];
  s = load_file->read(kDumpFileHeaderSize, &slice, buf);
  if (!s.ok())
  {
    return s;
  }

  uint32_t file_magic = DecodeFixed32(buf);
  if (kDumpFileMagic != file_magic)
  {
    return Status::IOError("BloomFilter dump file magic invalid.");
  }
  uint32_t num_probes = DecodeFixed32(buf + 4);
  uint64_t total_bits = DecodeFixed64(buf + 8);
  uint64_t total_bytes = total_bits / 8;
  initialize(total_bits, num_probes);

  s = load_file->read(total_bytes, &slice, reinterpret_cast<char *>(data_));
  if (!s.ok())
  {
    return s;
  }
  return Status::OK();
}

Status BloomFilter::dump(const string &filename)
{
  Status s;

  std::unique_ptr<WritableFile> dump_file;
  EnvOptions env_options;
  s = env_->NewWritableFile(filename, &dump_file, env_options);
  if (!s.ok())
  {
    return s;
  }

  char buf[kDumpFileHeaderSize];
  EncodeFixed32(buf, kDumpFileMagic);
  EncodeFixed32(buf + 4, num_probes_);
  EncodeFixed64(buf + 8, total_bits_);
  dump_file->append(StringPiece(buf, kDumpFileHeaderSize));

  uint64_t total_bytes = total_bits_ / 8;
  StringPiece slice(reinterpret_cast<const char *>(&data_[0]), total_bytes);
  s = dump_file->append(slice);

  if (!s.ok())
  {
    env_->DeleteFile(filename);
    return s;
  }
  return Status::OK();
}

} // namespace util
} // namespace mycc
