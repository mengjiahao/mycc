
#ifndef MYCC_UTIL_IO_UTIL_H_
#define MYCC_UTIL_IO_UTIL_H_

#include <errno.h>
#include <unistd.h>
#include <atomic>
#include "env_util.h"

namespace mycc
{
namespace util
{

// For non linux platform, the following macros are used only as place
// holder.
#if !(defined OS_LINUX)
#define POSIX_FADV_NORMAL 0     /* [MC1] no further special treatment */
#define POSIX_FADV_RANDOM 1     /* [MC1] expect random page refs */
#define POSIX_FADV_SEQUENTIAL 2 /* [MC1] expect sequential page refs */
#define POSIX_FADV_WILLNEED 3   /* [MC1] will need these pages */
#define POSIX_FADV_DONTNEED 4   /* [MC1] dont need these pages */
#endif

/// posix io class

class PosixSequentialFile : public SequentialFile
{
private:
  string filename_;
  FILE *file_;
  int fd_;
  bool use_direct_io_;
  uint64_t logical_sector_size_;

public:
  PosixSequentialFile(const string &fname, FILE *file, int fd,
                      const EnvOptions &options);
  virtual ~PosixSequentialFile();

  virtual Status Read(uint64_t n, StringPiece *result, char *scratch) override;
  virtual Status PositionedRead(uint64_t offset, uint64_t n, StringPiece *result,
                                char *scratch) override;
  virtual Status Skip(uint64_t n) override;
  virtual Status ReadLine(char *buf, int32_t n) override;
  virtual Status InvalidateCache(uint64_t offset, uint64_t length) override;
  virtual uint64_t GetRequiredBufferAlignment() const override
  {
    return logical_sector_size_;
  }
  virtual Status GetCurrentPos(int64_t *curpos) override;
  virtual int GetFD() override;
};

class PosixRandomAccessFile : public RandomAccessFile
{
protected:
  string filename_;
  int fd_;
  bool use_direct_io_;
  uint64_t logical_sector_size_;

public:
  PosixRandomAccessFile(const string &fname, int fd,
                        const EnvOptions &options);
  virtual ~PosixRandomAccessFile();

  virtual Status Read(uint64_t offset, uint64_t n, StringPiece *result,
                      char *scratch) const override;
  virtual Status Prefetch(uint64_t offset, uint64_t n) override;
  virtual uint64_t GetUniqueId(char *id, uint64_t max_size) const override;
  virtual void Hint(AccessPattern pattern) override;
  Status InvalidateCache(uint64_t offset, uint64_t length) override;
  virtual bool use_direct_io() const override { return use_direct_io_; }
  virtual uint64_t GetRequiredBufferAlignment() const override
  {
    return logical_sector_size_;
  }
  virtual int GetFD() override;
};

// mmap() based random-access
class PosixMmapReadableFile : public RandomAccessFile
{
private:
  int fd_;
  string filename_;
  void *mmapped_region_;
  uint64_t length_;

public:
  PosixMmapReadableFile(const int fd, const string &fname, void *base,
                        uint64_t length, const EnvOptions &options);
  virtual ~PosixMmapReadableFile();
  virtual Status Read(uint64_t offset, uint64_t n, StringPiece *result,
                      char *scratch) const override;
  virtual Status InvalidateCache(uint64_t offset, uint64_t length) override;
  virtual int GetFD() override;
};

class PosixWritableFile : public WritableFile
{
protected:
  const string filename_;
  const bool use_direct_io_;
  int fd_;
  uint64_t filesize_;
  uint64_t logical_sector_size_;

public:
  explicit PosixWritableFile(const string &fname, int fd,
                             const EnvOptions &options);
  virtual ~PosixWritableFile();

  // Need to implement this so the file is truncated correctly with direct I/O
  virtual Status Truncate(uint64_t size) override;
  virtual Status Close() override;
  virtual Status Append(const StringPiece &data) override;
  virtual Status PositionedAppend(const StringPiece &data, uint64_t offset) override;
  virtual Status Flush() override;
  virtual Status Sync() override;
  virtual Status Fsync() override;
  virtual bool IsSyncThreadSafe() const override;
  virtual bool use_direct_io() const override { return use_direct_io_; }
  virtual void SetWriteLifeTimeHint(Env::WriteLifeTimeHint hint) override;
  virtual uint64_t GetFileSize() override;
  virtual Status InvalidateCache(uint64_t offset, uint64_t length) override;
  virtual uint64_t GetRequiredBufferAlignment() const override
  {
    return logical_sector_size_;
  }
  virtual Status Allocate(uint64_t offset, uint64_t len) override
  {
    return Status::NotSupported("Fallocate not supported.");
  }
  virtual Status RangeSync(uint64_t offset, uint64_t nbytes) override;
  virtual uint64_t GetUniqueId(char *id, uint64_t max_size) const override;
  virtual int GetFD() override;
};

class PosixMmapFile : public WritableFile
{
private:
  static const uint64_t kMmapBoundSize = 65536;

  string filename_;
  int fd_;
  uint64_t page_size_;
  uint64_t map_size_;    // How much extra memory to map at a time
  char *base_;           // The mapped region
  char *limit_;          // Limit of the mapped region
  char *dst_;            // Where to write next  (in range [base_,limit_])
  char *last_sync_;      // Where have we synced up to
  uint64_t file_offset_; // Offset of base_ in file

  uint64_t TruncateToPageBoundary(uint64_t s)
  {
    s -= (s & (page_size_ - 1));
    assert((s % page_size_) == 0);
    return s;
  }

  Status MapNewRegion();
  Status UnmapCurrentRegion();
  Status Msync();

public:
  PosixMmapFile(const string &fname, int fd, uint64_t page_size,
                const EnvOptions &options);
  ~PosixMmapFile();

  // Means Close() will properly take care of truncate
  // and it does not need any additional information
  virtual Status Truncate(uint64_t size) override { return Status::OK(); }
  virtual Status Close() override;
  virtual Status Append(const StringPiece &data) override;
  virtual Status Flush() override;
  virtual Status Sync() override;
  virtual Status Fsync() override;
  virtual uint64_t GetFileSize() override;
  virtual Status InvalidateCache(uint64_t offset, uint64_t length) override;
  virtual int32_t GetFD() override;
};

class PosixRandomRWFile : public RandomRWFile
{
public:
  explicit PosixRandomRWFile(const string &fname, int fd,
                             const EnvOptions &options);
  virtual ~PosixRandomRWFile();

  virtual Status Write(uint64_t offset, const StringPiece &data) override;

  virtual Status Read(uint64_t offset, uint64_t n, StringPiece *result,
                      char *scratch) const override;

  virtual Status Flush() override;
  virtual Status Sync() override;
  virtual Status Fsync() override;
  virtual Status Close() override;

  virtual int GetFD() override;

private:
  const string filename_;
  int fd_;
};

struct PosixMemoryMappedFileBuffer : public MemoryMappedFileBuffer
{
  PosixMemoryMappedFileBuffer(void *_base, size_t _length)
      : MemoryMappedFileBuffer(_base, _length) {}
  virtual ~PosixMemoryMappedFileBuffer();
};

class PosixDirectory : public Directory
{
public:
  explicit PosixDirectory(int fd) : fd_(fd) {}
  ~PosixDirectory();
  virtual Status Fsync() override;

private:
  int fd_;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_IO_UTIL_H_