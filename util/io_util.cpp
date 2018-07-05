
#include "io_util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include "coding_util.h"
#include "math_util.h"
#include "string_util.h"

#if defined(OS_LINUX)
#include <linux/fs.h>
#endif

namespace mycc
{
namespace util
{

namespace
{ // anonymous namesapce

Status IOError(const string &context, int err_number)
{
  return Status::IOError(context, strerror(err_number));
}

// file_name can be left empty if it is not unkown.
Status IOError(const string &context, const string &file_name,
               int err_number)
{
  return Status::IOError(context + ": " + file_name, strerror(err_number));
}

bool IsSectorAligned(const uint64_t off, uint64_t sector_size)
{
  return off % sector_size == 0;
}

bool IsSectorAligned(const void *ptr, uint64_t sector_size)
{
  return uintptr_t(ptr) % sector_size == 0;
}

// A wrapper for fadvise, if the platform doesn't support fadvise, it will simply return 0.
int Fadvise(int fd, off_t offset, uint64_t len, int32_t advice)
{
#if defined(OS_LINUX)
  return ::posix_fadvise(fd, offset, len, advice);
#else
  (void)fd;
  (void)offset;
  (void)len;
  (void)advice;
  return 0; // simply do nothing.
#endif
}

uint64_t GetLogicalBufferSize(int32_t __attribute__((__unused__)) fd)
{
  return Env::kDefaultPageSize;
}

uint64_t GetUniqueIdFromFile(int32_t fd, char *id, uint64_t max_size)
{
#if defined(OS_LINUX)
  if (max_size < kMaxVarint64Length * 3)
  {
    return 0;
  }

  struct stat buf;
  int result = ::fstat(fd, &buf);
  assert(result != -1);
  if (result == -1)
  {
    return 0;
  }

  int64_t version = 0;
  result = ::ioctl(fd, FS_IOC_GETVERSION, &version);
  if (result == -1)
  {
    return 0;
  }
  uint64_t uversion = (uint64_t)version;

  char *rid = id;
  rid = EncodeVarint64(rid, buf.st_dev);
  rid = EncodeVarint64(rid, buf.st_ino);
  rid = EncodeVarint64(rid, uversion);
  assert(rid >= id);
  return static_cast<uint64_t>(rid - id);
#else
  return 0;
#endif
}

} // namespace

/// posix io class impl

/*
 * PosixSequentialFile
 */
PosixSequentialFile::PosixSequentialFile(const string &fname, FILE *file,
                                         int fd, const EnvOptions &options)
    : filename_(fname),
      file_(file),
      fd_(fd),
      use_direct_io_(options.use_direct_reads),
      logical_sector_size_(GetLogicalBufferSize(fd_))
{
  assert(!options.use_direct_reads || !options.use_mmap_reads);
}

PosixSequentialFile::~PosixSequentialFile()
{
  if (!use_direct_io())
  {
    assert(file_);
    ::fclose(file_);
  }
  else
  {
    assert(fd_);
    ::close(fd_);
  }
}

Status PosixSequentialFile::Read(uint64_t n, StringPiece *result, char *scratch)
{
  assert(result != nullptr && !use_direct_io());
  Status s;
  uint64_t r = 0;
  do
  {
    r = ::fread_unlocked(scratch, 1, n, file_);
  } while (r == 0 && ::ferror(file_) && errno == EINTR);
  *result = StringPiece(scratch, r);
  if (r < n)
  {
    if (::feof(file_))
    {
      // We leave status as ok if we hit the end of the file
      // We also clear the error so that the reads can continue
      // if a new data is written to the file
      ::clearerr(file_);
    }
    else
    {
      // A partial read with an error: return a non-ok status
      s = IOError("While reading file sequentially", filename_, errno);
    }
  }
  return s;
}

Status PosixSequentialFile::PositionedRead(uint64_t offset, uint64_t n,
                                           StringPiece *result, char *scratch)
{
  if (use_direct_io())
  {
    assert(IsSectorAligned(offset, GetRequiredBufferAlignment()));
    assert(IsSectorAligned(n, GetRequiredBufferAlignment()));
    assert(IsSectorAligned(scratch, GetRequiredBufferAlignment()));
  }
  Status s;
  int64_t r = -1;
  uint64_t left = n;
  char *ptr = scratch;
  assert(use_direct_io());
  while (left > 0)
  {
    r = ::pread(fd_, ptr, left, static_cast<off_t>(offset));
    if (r <= 0)
    {
      if (r == -1 && errno == EINTR)
      {
        continue;
      }
      break;
    }
    ptr += r;
    offset += r;
    left -= r;
    if (r % static_cast<int64_t>(GetRequiredBufferAlignment()) != 0)
    {
      // Bytes reads don't fill sectors. Should only happen at the end
      // of the file.
      break;
    }
  }
  if (r < 0)
  {
    // An error: return a non-ok status
    s = IOError(
        "While pread " + ToString(n) + " bytes from offset " + ToString(offset),
        filename_, errno);
  }
  *result = StringPiece(scratch, (r < 0) ? 0 : n - left);
  return s;
}

Status PosixSequentialFile::Skip(uint64_t n)
{
  if (::fseek(file_, static_cast<int64_t>(n), SEEK_CUR))
  {
    return IOError("While fseek to skip " + ToString(n) + " bytes", filename_,
                   errno);
  }
  return Status::OK();
}

Status PosixSequentialFile::ReadLine(char *buf, int32_t n)
{
  if (nullptr == ::fgets(buf, n, file_))
  {
    return IOError("While fgets " + ToString(n) + " bytes", filename_,
                   errno);
  }
  return Status::OK();
}

Status PosixSequentialFile::InvalidateCache(uint64_t offset, uint64_t length)
{
  if (use_direct_io())
  {
    return Status::OK();
  }
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret != 0)
  {
    return IOError("While fadvise NotNeeded offset " + ToString(offset) +
                       " len " + ToString(length),
                   filename_, errno);
  }
  return Status::OK();
}

Status PosixSequentialFile::GetCurrentPos(int64_t *curpos)
{
  int64_t pos = ::ftell(file_);
  if (-1 == pos)
  {
    return IOError("While ftell ", filename_, errno);
  }
  *curpos = pos;
  return Status::OK();
}

int PosixSequentialFile::GetFD()
{
  return fd_;
}

/*
 * PosixRandomAccessFile
 *
 * pread() based random-access
 */
PosixRandomAccessFile::PosixRandomAccessFile(const string &fname, int fd,
                                             const EnvOptions &options)
    : filename_(fname),
      fd_(fd),
      use_direct_io_(options.use_direct_reads),
      logical_sector_size_(GetLogicalBufferSize(fd_))
{
  assert(!options.use_direct_reads || !options.use_mmap_reads);
  assert(!options.use_mmap_reads || sizeof(void *) < 8);
}

PosixRandomAccessFile::~PosixRandomAccessFile() { ::close(fd_); }

Status PosixRandomAccessFile::Read(uint64_t offset, uint64_t n, StringPiece *result,
                                   char *scratch) const
{
  if (use_direct_io())
  {
    assert(IsSectorAligned(offset, GetRequiredBufferAlignment()));
    assert(IsSectorAligned(n, GetRequiredBufferAlignment()));
    assert(IsSectorAligned(scratch, GetRequiredBufferAlignment()));
  }
  Status s;
  int64_t r = -1;
  uint64_t left = n;
  char *ptr = scratch;
  while (left > 0)
  {
    r = ::pread(fd_, ptr, left, static_cast<off_t>(offset));

    if (r <= 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      break;
    }
    ptr += r;
    offset += r;
    left -= r;
    if (use_direct_io() &&
        r % static_cast<int64_t>(GetRequiredBufferAlignment()) != 0)
    {
      // Bytes reads don't fill sectors. Should only happen at the end
      // of the file.
      break;
    }
  }

  *result = StringPiece(scratch, (r < 0) ? 0 : n - left);
  if (r < 0)
  {
    // An error: return a non-ok status
    s = IOError(
        "While pread offset " + ToString(offset) + " len " + ToString(n),
        filename_, errno);
  }
  return s;
}

Status PosixRandomAccessFile::Prefetch(uint64_t offset, uint64_t n)
{
  Status s;
  if (!use_direct_io())
  {
    int64_t r = 0;
#ifdef OS_LINUX
    r = ::readahead(fd_, offset, n);
#endif
    if (r == -1)
    {
      s = IOError("While prefetching offset " + ToString(offset) + " len " +
                      ToString(n),
                  filename_, errno);
    }
  }
  return s;
}

uint64_t PosixRandomAccessFile::GetUniqueId(char *id, uint64_t max_size) const
{
  return GetUniqueIdFromFile(fd_, id, max_size);
}

void PosixRandomAccessFile::Hint(AccessPattern pattern)
{
  if (use_direct_io())
  {
    return;
  }
  switch (pattern)
  {
  case NORMAL:
    Fadvise(fd_, 0, 0, POSIX_FADV_NORMAL);
    break;
  case RANDOM:
    Fadvise(fd_, 0, 0, POSIX_FADV_RANDOM);
    break;
  case SEQUENTIAL:
    Fadvise(fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
    break;
  case WILLNEED:
    Fadvise(fd_, 0, 0, POSIX_FADV_WILLNEED);
    break;
  case DONTNEED:
    Fadvise(fd_, 0, 0, POSIX_FADV_DONTNEED);
    break;
  default:
    assert(false);
    break;
  }
}

Status PosixRandomAccessFile::InvalidateCache(uint64_t offset, uint64_t length)
{
  if (use_direct_io())
  {
    return Status::OK();
  }
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0)
  {
    return Status::OK();
  }
  return IOError("While fadvise NotNeeded offset " + ToString(offset) +
                     " len " + ToString(length),
                 filename_, errno);
}

int PosixRandomAccessFile::GetFD()
{
  return fd_;
}

/*
 * PosixMmapReadableFile
 *
 * mmap() based random-access
 */
// base[0,length-1] contains the mmapped contents of the file.
PosixMmapReadableFile::PosixMmapReadableFile(const int fd,
                                             const string &fname,
                                             void *base, uint64_t length,
                                             const EnvOptions &options)
    : fd_(fd), filename_(fname), mmapped_region_(base), length_(length)
{
#ifdef NDEBUG
  (void)options;
#endif
  fd_ = fd_ + 0; // suppress the warning for used variables
  assert(options.use_mmap_reads);
  assert(!options.use_direct_reads);
}

PosixMmapReadableFile::~PosixMmapReadableFile()
{
  int ret = ::munmap(mmapped_region_, length_);
  if (ret != 0)
  {
    fprintf(stderr, "failed to munmap %p length %" PRIu64 " \n",
            mmapped_region_, length_);
  }
  ::close(fd_);
}

Status PosixMmapReadableFile::Read(uint64_t offset, uint64_t n, StringPiece *result,
                                   char *scratch) const
{
  Status s;
  if (offset > length_)
  {
    *result = StringPiece();
    return IOError("While mmap read offset " + ToString(offset) +
                       " larger than file length " + ToString(length_),
                   filename_, EINVAL);
  }
  else if (offset + n > length_)
  {
    n = static_cast<uint64_t>(length_ - offset);
  }
  *result = StringPiece(reinterpret_cast<char *>(mmapped_region_) + offset, n);
  return s;
}

Status PosixMmapReadableFile::InvalidateCache(uint64_t offset, uint64_t length)
{
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0)
  {
    return Status::OK();
  }
  return IOError("While fadvise not needed. Offset " + ToString(offset) +
                     " len" + ToString(length),
                 filename_, errno);
}

int PosixMmapReadableFile::GetFD()
{
  return fd_;
}

/*
 * PosixWritableFile
 *
 * Use posix write to write data to a file.
 */
PosixWritableFile::PosixWritableFile(const string &fname, int fd,
                                     const EnvOptions &options)
    : filename_(fname),
      use_direct_io_(options.use_direct_writes),
      fd_(fd),
      filesize_(0),
      logical_sector_size_(GetLogicalBufferSize(fd_))
{
  assert(!options.use_mmap_writes);
}

PosixWritableFile::~PosixWritableFile()
{
  if (fd_ >= 0)
  {
    Close();
  }
}

Status PosixWritableFile::Append(const StringPiece &data)
{
  if (use_direct_io())
  {
    assert(IsSectorAligned(data.size(), GetRequiredBufferAlignment()));
    assert(IsSectorAligned(data.data(), GetRequiredBufferAlignment()));
  }
  const char *src = data.data();
  uint64_t left = data.size();
  while (left != 0)
  {
    int64_t done = ::write(fd_, src, left);
    if (done < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return IOError("While appending to file", filename_, errno);
    }
    left -= done;
    src += done;
  }
  filesize_ += data.size();
  return Status::OK();
}

Status PosixWritableFile::PositionedAppend(const StringPiece &data, uint64_t offset)
{
  if (use_direct_io())
  {
    assert(IsSectorAligned(offset, GetRequiredBufferAlignment()));
    assert(IsSectorAligned(data.size(), GetRequiredBufferAlignment()));
    assert(IsSectorAligned(data.data(), GetRequiredBufferAlignment()));
  }
  assert(offset <= std::numeric_limits<off_t>::max());
  const char *src = data.data();
  uint64_t left = data.size();
  while (left != 0)
  {
    int64_t done = ::pwrite(fd_, src, left, static_cast<off_t>(offset));
    if (done < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      return IOError("While pwrite to file at offset " + ToString(offset),
                     filename_, errno);
    }
    left -= done;
    offset += done;
    src += done;
  }
  filesize_ = offset + data.size();
  return Status::OK();
}

Status PosixWritableFile::Truncate(uint64_t size)
{
  Status s;
  int r = ::ftruncate(fd_, size);
  if (r < 0)
  {
    s = IOError("While ftruncate file to size " + ToString(size), filename_,
                errno);
  }
  else
  {
    filesize_ = size;
  }
  return s;
}

Status PosixWritableFile::Close()
{
  Status s;
  uint64_t block_size;
  uint64_t last_allocated_block;
  GetPreallocationStatus(&block_size, &last_allocated_block);
  if (last_allocated_block > 0)
  {
    // trim the extra space preallocated at the end of the file
    // NOTE(ljin): we probably don't want to surface failure as an IOError,
    // but it will be nice to log these errors.
    int32_t dummy __attribute__((__unused__));
    dummy = ::ftruncate(fd_, filesize_);
  }

  if (::close(fd_) < 0)
  {
    s = IOError("While closing file after writing", filename_, errno);
  }
  fd_ = -1;
  return s;
}

// write out the cached data to the OS cache
Status PosixWritableFile::Flush() { return Status::OK(); }

Status PosixWritableFile::Sync()
{
  if (::fdatasync(fd_) < 0)
  {
    return IOError("While fdatasync", filename_, errno);
  }
  return Status::OK();
}

Status PosixWritableFile::Fsync()
{
  if (::fsync(fd_) < 0)
  {
    return IOError("While fsync", filename_, errno);
  }
  return Status::OK();
}

bool PosixWritableFile::IsSyncThreadSafe() const { return true; }

uint64_t PosixWritableFile::GetFileSize() { return filesize_; }

void PosixWritableFile::SetWriteLifeTimeHint(Env::WriteLifeTimeHint hint)
{
  //if (::fcntl(fd_, F_SET_RW_HINT, &hint) == 0)
  //{
  //  write_hint_ = hint;
  //}
  (void)hint;
}

Status PosixWritableFile::InvalidateCache(uint64_t offset, uint64_t length)
{
  if (use_direct_io())
  {
    return Status::OK();
  }
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0)
  {
    return Status::OK();
  }
  return IOError("While fadvise NotNeeded", filename_, errno);
}

Status PosixWritableFile::RangeSync(uint64_t offset, uint64_t nbytes)
{
  assert(offset <= std::numeric_limits<off_t>::max());
  assert(nbytes <= std::numeric_limits<off_t>::max());
  if (::sync_file_range(fd_, static_cast<off_t>(offset),
                        static_cast<off_t>(nbytes), SYNC_FILE_RANGE_WRITE) == 0)
  {
    return Status::OK();
  }
  else
  {
    return IOError("While sync_file_range offset " + ToString(offset) +
                       " bytes " + ToString(nbytes),
                   filename_, errno);
  }
}

uint64_t PosixWritableFile::GetUniqueId(char *id, uint64_t max_size) const
{
  return GetUniqueIdFromFile(fd_, id, max_size);
}

int PosixWritableFile::GetFD()
{
  return fd_;
}

/*
 * PosixMmapFile
 *
 * We preallocate up to an extra megabyte and use memcpy to append new
 * data to the file.  This is safe since we either properly close the
 * file before reading from it, or for log files, the reading code
 * knows enough to skip zero suffixes.
 */
Status PosixMmapFile::UnmapCurrentRegion()
{
  if (base_ != nullptr)
  {
    int munmap_status = ::munmap(base_, limit_ - base_);
    if (munmap_status != 0)
    {
      return IOError(filename_, munmap_status);
    }
    file_offset_ += limit_ - base_;
    base_ = nullptr;
    limit_ = nullptr;
    last_sync_ = nullptr;
    dst_ = nullptr;

    // Increase the amount we map the next time, but capped at 1MB
    if (map_size_ < (1 << 20))
    {
      map_size_ *= 2;
    }
  }
  return Status::OK();
}

Status PosixMmapFile::MapNewRegion()
{
  assert(base_ == nullptr);
  int alloc_status = ::ftruncate(fd_, file_offset_ + map_size_);
  if (alloc_status)
  {
    return IOError("Error ftruncate space to file : ", filename_,
                   errno);
  }

  void *ptr = ::mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_,
                     file_offset_);
  if (ptr == MAP_FAILED)
  {
    return IOError("MMap failed on " + filename_, errno);
  }

  base_ = reinterpret_cast<char *>(ptr);
  limit_ = base_ + map_size_;
  dst_ = base_;
  last_sync_ = base_;
  return Status::OK();
}

Status PosixMmapFile::Msync()
{
  if (dst_ == last_sync_)
  {
    return Status::OK();
  }
  // Find the beginnings of the pages that contain the first and last
  // bytes to be synced.
  uint64_t p1 = TruncateToPageBoundary(last_sync_ - base_);
  uint64_t p2 = TruncateToPageBoundary(dst_ - base_ - 1);
  last_sync_ = dst_;
  if (::msync(base_ + p1, p2 - p1 + page_size_, MS_SYNC) < 0)
  {
    return IOError(filename_, errno);
  }
  return Status::OK();
}

PosixMmapFile::PosixMmapFile(const string &fname, int fd, uint64_t page_size,
                             const EnvOptions &options)
    : filename_(fname),
      fd_(fd),
      page_size_(page_size),
      map_size_(Roundup(kMmapBoundSize, page_size)),
      base_(nullptr),
      limit_(nullptr),
      dst_(nullptr),
      last_sync_(nullptr),
      file_offset_(0)
{
  (void)options;
  assert((page_size & (page_size - 1)) == 0);
  assert(options.use_mmap_writes);
  assert(!options.use_direct_writes);
}

PosixMmapFile::~PosixMmapFile()
{
  if (fd_ >= 0)
  {
    Close();
  }
}

Status PosixMmapFile::Append(const StringPiece &data)
{
  const char *src = data.data();
  uint64_t left = data.size();
  while (left > 0)
  {
    assert(base_ <= dst_);
    assert(dst_ <= limit_);
    uint64_t avail = limit_ - dst_;
    if (avail == 0)
    {
      Status s = UnmapCurrentRegion();
      if (!s.ok())
      {
        return s;
      }
      s = MapNewRegion();
      if (!s.ok())
      {
        return s;
      }
    }

    uint64_t n = (left <= avail) ? left : avail;
    assert(dst_);
    memcpy(dst_, src, n);
    dst_ += n;
    src += n;
    left -= n;
  }
  return Status::OK();
}

Status PosixMmapFile::Close()
{
  Status s;
  uint64_t unused = limit_ - dst_;

  s = UnmapCurrentRegion();
  if (!s.ok())
  {
    s = IOError("While closing mmapped file", filename_, errno);
  }
  else if (unused > 0)
  {
    // Trim the extra space at the end of the file
    if (::ftruncate(fd_, file_offset_ - unused) < 0)
    {
      s = IOError("While ftruncating mmaped file", filename_, errno);
    }
  }

  if (::close(fd_) < 0)
  {
    if (s.ok())
    {
      s = IOError("While closing mmapped file", filename_, errno);
    }
  }

  fd_ = -1;
  base_ = nullptr;
  limit_ = nullptr;
  return s;
}

Status PosixMmapFile::Flush() { return Status::OK(); }

Status PosixMmapFile::Sync()
{
  if (::fdatasync(fd_) < 0)
  {
    return IOError("While fdatasync mmapped file", filename_, errno);
  }

  return Msync();
}

/**
 * Flush data as well as metadata to stable storage.
 */
Status PosixMmapFile::Fsync()
{
  if (::fsync(fd_) < 0)
  {
    return IOError("While fsync mmaped file", filename_, errno);
  }

  return Msync();
}

/**
 * Get the size of valid data in the file. This will not match the
 * size that is returned from the filesystem because we use mmap
 * to extend file by map_size every time.
 */
uint64_t PosixMmapFile::GetFileSize()
{
  uint64_t used = dst_ - base_;
  return file_offset_ + used;
}

Status PosixMmapFile::InvalidateCache(uint64_t offset, uint64_t length)
{
  // free OS pages
  int ret = Fadvise(fd_, offset, length, POSIX_FADV_DONTNEED);
  if (ret == 0)
  {
    return Status::OK();
  }
  return IOError("While fadvise NotNeeded mmapped file", filename_, errno);
}

int PosixMmapFile::GetFD()
{
  return fd_;
}

/*
 * PosixRandomRWFile
 */

PosixRandomRWFile::PosixRandomRWFile(const string &fname, int fd,
                                     const EnvOptions &options)
    : filename_(fname), fd_(fd) {}

PosixRandomRWFile::~PosixRandomRWFile()
{
  if (fd_ >= 0)
  {
    Close();
  }
}

Status PosixRandomRWFile::Write(uint64_t offset, const StringPiece &data)
{
  const char *src = data.data();
  uint64_t left = data.size();
  while (left != 0)
  {
    int64_t done = ::pwrite(fd_, src, left, offset);
    if (done < 0)
    {
      // error while writing to file
      if (errno == EINTR)
      {
        // write was interrupted, try again.
        continue;
      }
      return IOError(
          "While write random read/write file at offset " + ToString(offset),
          filename_, errno);
    }

    // Wrote `done` bytes
    left -= done;
    offset += done;
    src += done;
  }

  return Status::OK();
}

Status PosixRandomRWFile::Read(uint64_t offset, uint64_t n, StringPiece *result,
                               char *scratch) const
{
  uint64_t left = n;
  char *ptr = scratch;
  while (left > 0)
  {
    int64_t done = ::pread(fd_, ptr, left, offset);
    if (done < 0)
    {
      // error while reading from file
      if (errno == EINTR)
      {
        // read was interrupted, try again.
        continue;
      }
      return IOError("While reading random read/write file offset " +
                         ToString(offset) + " len " + ToString(n),
                     filename_, errno);
    }
    else if (done == 0)
    {
      // Nothing more to read
      break;
    }

    // Read `done` bytes
    ptr += done;
    offset += done;
    left -= done;
  }

  *result = StringPiece(scratch, n - left);
  return Status::OK();
}

Status PosixRandomRWFile::Flush() { return Status::OK(); }

Status PosixRandomRWFile::Sync()
{
  if (::fdatasync(fd_) < 0)
  {
    return IOError("While fdatasync random read/write file", filename_, errno);
  }
  return Status::OK();
}

Status PosixRandomRWFile::Fsync()
{
  if (::fsync(fd_) < 0)
  {
    return IOError("While fsync random read/write file", filename_, errno);
  }
  return Status::OK();
}

Status PosixRandomRWFile::Close()
{
  if (::close(fd_) < 0)
  {
    return IOError("While close random read/write file", filename_, errno);
  }
  fd_ = -1;
  return Status::OK();
}

int PosixRandomRWFile::GetFD()
{
  return fd_;
}

/*
 * PosixMemoryMappedFileBuffer
 */

PosixMemoryMappedFileBuffer::~PosixMemoryMappedFileBuffer()
{
  ::munmap(this->base_, length_);
}

/*
 * PosixDirectory
 */

PosixDirectory::~PosixDirectory() { ::close(fd_); }

Status PosixDirectory::Fsync()
{
  if (::fsync(fd_) == -1)
  {
    return IOError("While fsync", "a directory", errno);
  }
  return Status::OK();
}

} // namespace util
} // namespace mycc