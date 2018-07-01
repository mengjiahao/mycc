#ifndef MYCC_UTIL_BLOCK_H_
#define MYCC_UTIL_BLOCK_H_

#include <vector>
#include "status.h"
#include "iterator.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class RandomAccessFile;
class Block;

// DB contents are stored in a set of blocks, each of which holds a
// sequence of key,value pairs.  Each block may be compressed before
// being stored in a file.  The following enum describes which
// compression method (if any) is used to compress a block.
enum BlockCompressionType 
{
  // NOTE: do not change the values of existing entries, as these are
  // part of the persistent format on disk.
  kNoCompression = 0x0,
  kSnappyCompression = 0x1
};

// BlockOption to control the behavior of a table (passed to Table::Open)
struct BlockOption
{
  // Approximate size of user data packed per block.  Note that the
  // block size specified here corresponds to uncompressed data.  The
  // actual size of the unit read from disk may be smaller if
  // compression is enabled.  This parameter can be changed dynamically.
  uint64_t block_size = 262144;

  // Number of keys between restart points for delta encoding of keys.
  // This parameter can be changed dynamically.  Most clients should
  // leave this parameter alone.
  int32_t block_restart_interval = 16;

  // Compress blocks using the specified compression algorithm.  This
  // parameter can be changed dynamically.
  //
  // Default: kSnappyCompression, which gives lightweight but fast
  // compression.
  //
  // Typical speeds of kSnappyCompression on an Intel(R) Core(TM)2 2.4GHz:
  //    ~200-500MB/s compression
  //    ~400-800MB/s decompression
  // Note that these speeds are significantly faster than most
  // persistent storage speeds, and therefore it is typically never
  // worth switching to kNoCompression.  Even if the input data is
  // incompressible, the kSnappyCompression implementation will
  // efficiently detect that and will switch to uncompressed mode.
  BlockCompressionType compression = kSnappyCompression;
};

// BlockHandle is a pointer to the extent of a file that stores a data
// block or a meta block.
class BlockHandle
{
public:
  BlockHandle();

  // The offset of the block in the file.
  uint64_t offset() const { return offset_; }
  void set_offset(uint64_t offset) { offset_ = offset; }

  // The size of the stored block
  uint64_t size() const { return size_; }
  void set_size(uint64_t size) { size_ = size; }

  void EncodeTo(string *dst) const;
  Status DecodeFrom(StringPiece *input);

  // Maximum encoding length of a BlockHandle
  enum
  {
    kMaxEncodedLength = 10 + 10
  };

private:
  uint64_t offset_;
  uint64_t size_;
};

// Footer encapsulates the fixed information stored at the tail
// end of every table file.
class Footer
{
public:
  Footer() {}

  // The block handle for the metaindex block of the table
  const BlockHandle &metaindex_handle() const { return metaindex_handle_; }
  void set_metaindex_handle(const BlockHandle &h) { metaindex_handle_ = h; }

  // The block handle for the index block of the table
  const BlockHandle &index_handle() const { return index_handle_; }
  void set_index_handle(const BlockHandle &h) { index_handle_ = h; }

  void EncodeTo(string *dst) const;
  Status DecodeFrom(StringPiece *input);

  // Encoded length of a Footer.  Note that the serialization of a
  // Footer will always occupy exactly this many bytes.  It consists
  // of two block handles and a magic number.
  enum
  {
    kEncodedLength = 2 * BlockHandle::kMaxEncodedLength + 8
  };

private:
  BlockHandle metaindex_handle_;
  BlockHandle index_handle_;
};

// kTableMagicNumber was picked by running
//    echo http://code.google.com/p/leveldb/ | sha1sum
// and taking the leading 64 bits.
static const uint64_t kTableMagicNumber = 0xdb4775248b80fb57ull;

// 1-byte type + 32-bit crc
static const uint64_t kBlockTrailerSize = 5;

struct BlockContents
{
  StringPiece data;    // Actual contents of data
  bool cachable;       // True iff data can be cached
  bool heap_allocated; // True iff caller should delete[] data.data()
};

// Read the block identified by "handle" from "file".  On failure
// return non-OK.  On success fill *result and return OK.
extern Status ReadBlock(RandomAccessFile *file, const BlockHandle &handle,
                        BlockContents *result);

// Implementation details follow.  Clients should ignore,

inline BlockHandle::BlockHandle()
    : offset_(~static_cast<uint64_t>(0)), size_(~static_cast<uint64_t>(0)) {}

// Block
class Block
{
public:
  // Initialize the block with the specified contents.
  explicit Block(const BlockContents &contents);

  ~Block();

  uint64_t size() const { return size_; }
  Iterator *NewIterator();

private:
  uint32_t NumRestarts() const;

  const char *data_;
  uint64_t size_;
  uint32_t restart_offset_; // Offset in data_ of restart array
  bool owned_;              // Block owns data_[]

  class Iter;

  DISALLOW_COPY_AND_ASSIGN(Block);
};

class BlockBuilder
{
public:
  explicit BlockBuilder(const BlockOption *options);

  // Reset the contents as if the BlockBuilder was just constructed.
  void Reset();

  // REQUIRES: Finish() has not been called since the last call to Reset().
  // REQUIRES: key is larger than any previously added key
  void Add(const StringPiece &key, const StringPiece &value);

  // Finish building the block and return a slice that refers to the
  // block contents.  The returned slice will remain valid for the
  // lifetime of this builder or until Reset() is called.
  StringPiece Finish();

  // Returns an estimate of the current (uncompressed) size of the block
  // we are building.
  size_t CurrentSizeEstimate() const;

  // Return true iff no entries have been added since the last Reset()
  bool empty() const { return buffer_.empty(); }

private:
  const BlockOption *options_;
  string buffer_;                  // Destination buffer
  std::vector<uint32_t> restarts_; // Restart points
  int counter_;                    // Number of entries emitted since restart
  bool finished_;                  // Has Finish() been called?
  string last_key_;

  DISALLOW_COPY_AND_ASSIGN(BlockBuilder);
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_BLOCK_H_