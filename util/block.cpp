
#include "block.h"
#include <algorithm>
#include "coding_util.h"
#include "crc_util.h"
#include "env_util.h"
#include "math_util.h"

namespace mycc
{
namespace util
{

void BlockHandle::EncodeTo(string *dst) const
{
  // Sanity check that all fields have been set
  assert(offset_ != ~static_cast<uint64_t>(0));
  assert(size_ != ~static_cast<uint64_t>(0));
  PutVarint64(dst, offset_);
  PutVarint64(dst, size_);
}

Status BlockHandle::DecodeFrom(StringPiece *input)
{
  if (GetVarint64(input, &offset_) && GetVarint64(input, &size_))
  {
    return Status::OK();
  }
  else
  {
    return Status::Error("bad block handle");
  }
}

void Footer::EncodeTo(string *dst) const
{
#ifndef NDEBUG
  const uint64_t original_size = dst->size();
#endif
  metaindex_handle_.EncodeTo(dst);
  index_handle_.EncodeTo(dst);
  dst->resize(2 * BlockHandle::kMaxEncodedLength); // Padding
  PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber & 0xffffffffu));
  PutFixed32(dst, static_cast<uint32_t>(kTableMagicNumber >> 32));
  assert(dst->size() == original_size + kEncodedLength);
}

Status Footer::DecodeFrom(StringPiece *input)
{
  const char *magic_ptr = input->data() + kEncodedLength - 8;
  const uint32_t magic_lo = DecodeFixed32(magic_ptr);
  const uint32_t magic_hi = DecodeFixed32(magic_ptr + 4);
  const uint64_t magic =
      ((static_cast<uint64_t>(magic_hi) << 32) | (static_cast<uint64_t>(magic_lo)));
  if (magic != kTableMagicNumber)
  {
    return Status::Error("not an sstable (bad magic number)");
  }

  Status result = metaindex_handle_.DecodeFrom(input);
  if (result.ok())
  {
    result = index_handle_.DecodeFrom(input);
  }
  if (result.ok())
  {
    // We skip over any leftover data (just padding for now) in "input"
    const char *end = magic_ptr + 8;
    *input = StringPiece(end, input->data() + input->size() - end);
  }
  return result;
}

Status ReadBlock(RandomAccessFile *file, const BlockHandle &handle,
                 BlockContents *result)
{
  result->data = StringPiece();
  result->cachable = false;
  result->heap_allocated = false;

  // Read the block contents as well as the type/crc footer.
  // See table_builder.cc for the code that built this structure.
  uint64_t n = static_cast<uint64_t>(handle.size());
  char *buf = new char[n + kBlockTrailerSize];
  StringPiece contents;
  Status s = file->read(handle.offset(), n + kBlockTrailerSize, &contents, buf);
  if (!s.ok())
  {
    delete[] buf;
    return s;
  }
  if (contents.size() != n + kBlockTrailerSize)
  {
    delete[] buf;
    return Status::Error("truncated block read");
  }

  // Check the crc of the type and the block contents
  const char *data = contents.data(); // Pointer to where Read put the data
  // This checksum verification is optional.  We leave it on for now
  const bool verify_checksum = true;
  if (verify_checksum)
  {
    const uint32_t crc = Crc32Unmask(DecodeFixed32(data + n + 1));
    const uint32_t actual = Crc32Value(data, n + 1);
    if (actual != crc)
    {
      delete[] buf;
      s = Status::Error("block checksum mismatch");
      return s;
    }
  }

  switch (data[n])
  {
  case kNoCompression:
    if (data != buf)
    {
      // File implementation gave us pointer to some other data.
      // Use it directly under the assumption that it will be live
      // while the file is open.
      delete[] buf;
      result->data = StringPiece(data, n);
      result->heap_allocated = false;
      result->cachable = false; // Do not double-cache
    }
    else
    {
      result->data = StringPiece(buf, n);
      result->heap_allocated = true;
      result->cachable = true;
    }
    // Ok
    break;
  default:
    delete[] buf;
    return Status::Error("bad block type");
  }

  return Status::OK();
}

inline uint32_t Block::NumRestarts() const
{
  assert(size_ >= sizeof(uint32_t));
  return DecodeFixed32(data_ + size_ - sizeof(uint32_t));
}

Block::Block(const BlockContents &contents)
    : data_(contents.data.data()),
      size_(contents.data.size()),
      owned_(contents.heap_allocated)
{
  if (size_ < sizeof(uint32_t))
  {
    size_ = 0; // Error marker
  }
  else
  {
    uint64_t max_restarts_allowed = (size_ - sizeof(uint32_t)) / sizeof(uint32_t);
    if (NumRestarts() > max_restarts_allowed)
    {
      // The size is too small for NumRestarts()
      size_ = 0;
    }
    else
    {
      restart_offset_ = size_ - (1 + NumRestarts()) * sizeof(uint32_t);
    }
  }
}

Block::~Block()
{
  if (owned_)
  {
    delete[] data_;
  }
}

// Helper routine: decode the next block entry starting at "p",
// storing the number of shared key bytes, non_shared key bytes,
// and the length of the value in "*shared", "*non_shared", and
// "*value_length", respectively.  Will not dereference past "limit".
//
// If any errors are detected, returns NULL.  Otherwise, returns a
// pointer to the key delta (just past the three decoded values).
static inline const char *DecodeEntry(const char *p, const char *limit,
                                      uint32_t *shared, uint32_t *non_shared,
                                      uint32_t *value_length)
{
  if (limit - p < 3)
    return NULL;
  *shared = reinterpret_cast<const unsigned char *>(p)[0];
  *non_shared = reinterpret_cast<const unsigned char *>(p)[1];
  *value_length = reinterpret_cast<const unsigned char *>(p)[2];
  if ((*shared | *non_shared | *value_length) < 128)
  {
    // Fast path: all three values are encoded in one byte each
    p += 3;
  }
  else
  {
    if ((p = GetVarint32Ptr(p, limit, shared)) == NULL)
      return NULL;
    if ((p = GetVarint32Ptr(p, limit, non_shared)) == NULL)
      return NULL;
    if ((p = GetVarint32Ptr(p, limit, value_length)) == NULL)
      return NULL;
  }

  if (static_cast<uint32_t>(limit - p) < (*non_shared + *value_length))
  {
    return NULL;
  }
  return p;
}

class Block::Iter : public Iterator
{
private:
  const char *const data_;      // underlying block contents
  uint32_t const restarts_;     // Offset of restart array (list of fixed32)
  uint32_t const num_restarts_; // Number of uint32_t entries in restart array

  // current_ is offset in data_ of current entry.  >= restarts_ if !Valid
  uint32_t current_;
  uint32_t restart_index_; // Index of restart block in which current_ falls
  string key_;
  StringPiece value_;
  Status status_;

  inline int Compare(const StringPiece &a, const StringPiece &b) const
  {
    return a.compare(b);
  }

  // Return the offset in data_ just past the end of the current entry.
  inline uint32_t NextEntryOffset() const
  {
    return (value_.data() + value_.size()) - data_;
  }

  uint32_t GetRestartPoint(uint32_t index)
  {
    assert(index < num_restarts_);
    return DecodeFixed32(data_ + restarts_ + index * sizeof(uint32_t));
  }

  void SeekToRestartPoint(uint32_t index)
  {
    key_.clear();
    restart_index_ = index;
    // current_ will be fixed by ParseNextKey();

    // ParseNextKey() starts at the end of value_, so set value_ accordingly
    uint32_t offset = GetRestartPoint(index);
    value_ = StringPiece(data_ + offset, 0);
  }

public:
  Iter(const char *data, uint32_t restarts, uint32_t num_restarts)
      : data_(data),
        restarts_(restarts),
        num_restarts_(num_restarts),
        current_(restarts_),
        restart_index_(num_restarts_)
  {
    assert(num_restarts_ > 0);
  }

  virtual bool Valid() const { return current_ < restarts_; }
  virtual Status status() const { return status_; }
  virtual StringPiece key() const
  {
    assert(Valid());
    return key_;
  }
  virtual StringPiece value() const
  {
    assert(Valid());
    return value_;
  }

  virtual void Next()
  {
    assert(Valid());
    ParseNextKey();
  }

  virtual void Seek(const StringPiece &target)
  {
    // Binary search in restart array to find the last restart point
    // with a key < target
    uint32_t left = 0;
    uint32_t right = num_restarts_ - 1;
    while (left < right)
    {
      uint32_t mid = (left + right + 1) / 2;
      uint32_t region_offset = GetRestartPoint(mid);
      uint32_t shared, non_shared, value_length;
      const char *key_ptr =
          DecodeEntry(data_ + region_offset, data_ + restarts_, &shared,
                      &non_shared, &value_length);
      if (key_ptr == NULL || (shared != 0))
      {
        CorruptionError();
        return;
      }
      StringPiece mid_key(key_ptr, non_shared);
      if (Compare(mid_key, target) < 0)
      {
        // Key at "mid" is smaller than "target".  Therefore all
        // blocks before "mid" are uninteresting.
        left = mid;
      }
      else
      {
        // Key at "mid" is >= "target".  Therefore all blocks at or
        // after "mid" are uninteresting.
        right = mid - 1;
      }
    }

    // Linear search (within restart block) for first key >= target
    SeekToRestartPoint(left);
    while (true)
    {
      if (!ParseNextKey())
      {
        return;
      }
      if (Compare(key_, target) >= 0)
      {
        return;
      }
    }
  }

  virtual void SeekToFirst()
  {
    SeekToRestartPoint(0);
    ParseNextKey();
  }

private:
  void CorruptionError()
  {
    current_ = restarts_;
    restart_index_ = num_restarts_;
    status_ = Status::Error("bad entry in block");
    key_.clear();
    value_.clear();
  }

  bool ParseNextKey()
  {
    current_ = NextEntryOffset();
    const char *p = data_ + current_;
    const char *limit = data_ + restarts_; // Restarts come right after data
    if (p >= limit)
    {
      // No more entries to return.  Mark as invalid.
      current_ = restarts_;
      restart_index_ = num_restarts_;
      return false;
    }

    // Decode next entry
    uint32_t shared, non_shared, value_length;
    p = DecodeEntry(p, limit, &shared, &non_shared, &value_length);
    if (p == NULL || key_.size() < shared)
    {
      CorruptionError();
      return false;
    }
    else
    {
      key_.resize(shared);
      key_.append(p, non_shared);
      value_ = StringPiece(p + non_shared, value_length);
      while (restart_index_ + 1 < num_restarts_ &&
             GetRestartPoint(restart_index_ + 1) < current_)
      {
        ++restart_index_;
      }
      return true;
    }
  }
};

Iterator *Block::NewIterator()
{
  if (size_ < sizeof(uint32_t))
  {
    return NewErrorIterator(Status::Error("bad block contents"));
  }
  const uint32_t num_restarts = NumRestarts();
  if (num_restarts == 0)
  {
    return NewEmptyIterator();
  }
  else
  {
    return new Iter(data_, restart_offset_, num_restarts);
  }
}

BlockBuilder::BlockBuilder(const BlockOption *options)
    : options_(options), restarts_(), counter_(0), finished_(false)
{
  assert(options->block_restart_interval >= 1);
  restarts_.push_back(0); // First restart point is at offset 0
}

void BlockBuilder::Reset()
{
  buffer_.clear();
  restarts_.clear();
  restarts_.push_back(0); // First restart point is at offset 0
  counter_ = 0;
  finished_ = false;
  last_key_.clear();
}

uint64_t BlockBuilder::CurrentSizeEstimate() const
{
  return (buffer_.size() +                      // Raw data buffer
          restarts_.size() * sizeof(uint32_t) + // Restart array
          sizeof(uint32_t));                    // Restart array length
}

StringPiece BlockBuilder::Finish()
{
  // Append restart array
  for (uint64_t i = 0; i < restarts_.size(); i++)
  {
    PutFixed32(&buffer_, restarts_[i]);
  }
  PutFixed32(&buffer_, restarts_.size());
  finished_ = true;
  return StringPiece(buffer_);
}

void BlockBuilder::Add(const StringPiece &key, const StringPiece &value)
{
  StringPiece last_key_piece(last_key_);
  assert(!finished_);
  assert(counter_ <= options_->block_restart_interval);
  assert(buffer_.empty() // No values yet?
         || key.compare(last_key_piece) > 0);
  uint64_t shared = 0;
  if (counter_ < options_->block_restart_interval)
  {
    // See how much sharing to do with previous string
    const uint64_t min_length = MATH_MIN(last_key_piece.size(), key.size());
    while ((shared < min_length) && (last_key_piece[shared] == key[shared]))
    {
      shared++;
    }
  }
  else
  {
    // Restart compression
    restarts_.push_back(buffer_.size());
    counter_ = 0;
  }
  const uint64_t non_shared = key.size() - shared;

  // Add "<shared><non_shared><value_size>" to buffer_
  PutVarint32(&buffer_, shared);
  PutVarint32(&buffer_, non_shared);
  PutVarint32(&buffer_, value.size());

  // Add string delta to buffer_ followed by value
  buffer_.append(key.data() + shared, non_shared);
  buffer_.append(value.data(), value.size());

  // Update state
  last_key_.resize(shared);
  last_key_.append(key.data() + shared, non_shared);
  assert(StringPiece(last_key_) == key);
  counter_++;
}

} // namespace util
} // namespace mycc