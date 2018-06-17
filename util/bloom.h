
#ifndef MYCC_UTIL_BLOOM_H_
#define MYCC_UTIL_BLOOM_H_

#include <malloc.h>
#include <functional>
#include <memory>
#include "status.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class BloomFilter
{
public:
  static uint64_t BloomHash(const StringPiece &key);

  BloomFilter()
      : hash_func_(BloomHash), total_bits_(0), num_probes_(0), data_(nullptr) {}
  ~BloomFilter() { destroy(); }

  string toString();
  void initialize(uint64_t bit_size, uint32_t num_probes = 1);
  void destroy();
  void clear();
  bool isInitialized() const { return 0 < total_bits_ && 0 < num_probes_; }
  const uint8_t *getData() { return data_; }
  uint64_t getTotalBytes() { return (total_bits_ / 8); }
  void add(const StringPiece &key);
  // @thread-safe
  void addConcurrently(const StringPiece &key);
  bool mayContain(const StringPiece &key);
  // @thread-safe
  bool mayContainConcurrently(const StringPiece &key);
  uint64_t count1Bits();
  // No need to initialize before load.
  Status load(const string &filename);
  Status dump(const string &filename);

private:
  typedef bool (*ByteOperFunc)(uint8_t *ptr, uint8_t mask, void *ret);
  bool operByteHash(uint64_t hash, ByteOperFunc byteOperFunc);

private:
  // |magic|num_probes_|total_bits_|data_|
  static const uint32_t kDumpFileMagic = 0x46420000; // little-endian 00BF
  static const uint64_t kDumpFileHeaderSize = 4 + 4 + 8;

  typedef uint64_t (*HashFunc)(const StringPiece &key);
  HashFunc hash_func_;
  uint64_t total_bits_;
  uint32_t num_probes_;
  uint8_t *data_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BLOOM_H_
