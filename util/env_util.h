
#ifndef MYCC_UTIL_ENV_UTIL_H_
#define MYCC_UTIL_ENV_UTIL_H_

#include <stdarg.h>
#include <stdlib.h>
#include <functional>
#include <memory>
#include <vector>
#include "macros_util.h"
#include "status.h"
#include "stringpiece.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

class SequentialFile;
class RandomAccessFile;
class WritableFile;
class RandomRWFile;
class MemoryMappedFileBuffer;
class Directory;
class Thread;
class ThreadPool;

// Options while opening a file to read/write
struct EnvOptions
{
  // construct with default Options
  EnvOptions() {}

  // If true, then use mmap to read data
  bool use_mmap_reads = false;

  // If true, then use mmap to write data
  bool use_mmap_writes = false;

  // If true, then use O_DIRECT for reading data
  bool use_direct_reads = false;

  // If true, then use O_DIRECT for writing data
  bool use_direct_writes = false;

  // If false, fallocate() calls are bypassed
  bool allow_fallocate = false;

  // If true, set the FD_CLOEXEC on open fd.
  bool set_fd_cloexec = false;

  // Allows OS to incrementally sync files to disk while they are being
  // written, in the background. Issue one request for every bytes_per_sync
  // written. 0 turns it off.
  // Default: 0
  uint64_t bytes_per_sync = 0;

  // If true, we will preallocate the file with FALLOC_FL_KEEP_SIZE flag, which
  // means that file size won't change as part of preallocation.
  // If false, preallocation will also change the file size. This option will
  // improve the performance in workloads where you sync the data on every
  // write. By default, we set it to true for MANIFEST writes and false for
  // WAL writes
  bool fallocate_with_keep_size = false;

  uint64_t writable_file_max_buffer_size = 1024 * 1024;
};

struct FileAttributes
{
  // File name
  string name;

  // Size of file in bytes
  uint64_t size_bytes;
};

struct FileStatistics
{
  // protection
  uint64_t mode;
  // user ID of owner
  uint64_t uid;
  // group ID of owner
  uint64_t gid;
  // The length of the file or -1 if finding file length is not supported.
  int64_t length = -1;
  // The last modified time in nanoseconds.
  int64_t mtime_nsec = 0;
  // True if the file is a directory, otherwise false.
  bool is_directory = false;

  FileStatistics() {}
  FileStatistics(int64_t length, int64_t mtime_nsec, bool is_directory)
      : mode(0), uid(0), gid(0), length(length), mtime_nsec(mtime_nsec), is_directory(is_directory) {}
  ~FileStatistics() {}
};

struct FileLock
{
  int32_t fd;
  string name;
};

/// \brief Options to configure a Thread.
///
/// Note that the options are all hints, and the
/// underlying implementation may choose to ignore it.
struct ThreadOptions
{
  /// Thread stack size to use (in bytes).
  uint64_t stack_size = 0; // 0: use system default value
  /// Guard area size to use near thread stacks to use (in bytes)
  uint64_t guard_size = 0; // 0: use system default value
};

class Env
{
public:
  // Priority for scheduling job in thread pool
  enum Priority
  {
    BOTTOM,
    LOW,
    HIGH,
    TOTAL
  };

  // Priority for requesting bytes in rate limiter scheduler
  enum IOPriority
  {
    IO_LOW = 0,
    IO_HIGH = 1,
    IO_TOTAL = 2
  };

  // These values match Linux definition
  // https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/include/uapi/linux/fcntl.h#n56
  enum WriteLifeTimeHint
  {
    WLTH_NOT_SET = 0, // No hint information set
    WLTH_NONE,        // No hints about write life time
    WLTH_SHORT,       // Data written has a short life time
    WLTH_MEDIUM,      // Data written has a medium life time
    WLTH_LONG,        // Data written has a long life time
    WLTH_EXTREME,     // Data written has an extremely long life time
  };

  static const uint64_t kMaxPathLength = 10240;
  static const uint64_t kDefaultPageSize = 4 * 1024;

  // Return a default environment suitable for the current operating
  // system.  Sophisticated users may wish to provide their own Env
  // implementation instead of relying on this default environment.
  //
  // The result of Default() belongs to system and must never be deleted.
  static Env *Default();

  // Returns the number of micro-seconds since some fixed point in time.
  static uint64_t NowMicros();

  // Returns the number of nano-seconds since some fixed point in time.
  static uint64_t NowNanos();

  static uint64_t NowChronoNanos();

  static void SleepForMicros(int32_t micros);

  // Get the number of seconds since the Epoch, 1970-01-01 00:00:00 (UTC).
  // Only overwrites *unix_time on success.
  static Status GetCurrentTimeEpoch(int64_t *unix_time);

  // Converts seconds-since-Jan-01-1970 to a printable string
  static string TimeToString(uint64_t time);

  /// path ops

  // Return true if path is absolute.
  static bool IsAbsolutePath(StringPiece path);

  // Returns the part of the path before the final "/".  If there is a single
  // leading "/" in the path, the result will be the leading "/".  If there is
  // no "/" in the path, the result is the empty prefix of the input.
  static StringPiece Dirname(StringPiece path);

  // Returns the part of the path after the final "/".  If there is no
  // "/" in the path, the result is the same as the input.
  static StringPiece Basename(StringPiece path);

  // Returns the part of the basename of path after the final ".".  If
  // there is no "." in the basename, the result is empty.
  static StringPiece PathExtension(StringPiece path);

  // Collapse duplicate "/"s, resolve ".." and "." path elements, remove
  // trailing "/".
  //
  // NOTE: This respects relative vs. absolute paths, but does not
  // invoke any system calls (getcwd(2)) in order to resolve relative
  // paths with respect to the actual working directory.  That is, this is purely
  // string manipulation, completely independent of process state.
  static string CleanPath(StringPiece path);

  // Obtains the base name from a full path.
  static string StripBasename(const string &full_path);

  static bool SplitPath(const string &path,
                        std::vector<string> *element,
                        bool *isdir);

  static std::pair<StringPiece, StringPiece> SplitBasename(StringPiece path);

  static std::pair<StringPiece, StringPiece> SplitPath(StringPiece path);

  // Join multiple paths together, without introducing unnecessary path
  // separators.
  // For example:
  //
  //  Arguments                  | JoinPath
  //  ---------------------------+----------
  //  '/foo', 'bar'              | /foo/bar
  //  '/foo/', 'bar'             | /foo/bar
  //  '/foo', '/bar'             | /foo/bar
  //
  // Usage:
  // string path = JoinPath("/mydir", filename);
  // string path = JoinPath(FLAGS_test_srcdir, filename);
  // string path = JoinPath("/full", "path", "to", "filename);

  static string JoinInitPath(std::initializer_list<StringPiece> paths);

  template <typename... T>
  static string JoinPath(const T &... args)
  {
    return JoinInitPath({args...});
  }

  // Create a brand new sequentially-readable file with the specified name.
  // On success, stores a pointer to the new file in *result and returns OK.
  // On failure stores nullptr in *result and returns non-OK.  If the file does
  // not exist, returns a non-OK status.
  //
  // The returned file will only be accessed by one thread at a time.
  static Status NewSequentialFile(const string &fname,
                                  std::unique_ptr<SequentialFile> *result,
                                  const EnvOptions &options);

  // Create a brand new random access read-only file with the
  // specified name.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores nullptr in *result and
  // returns non-OK.  If the file does not exist, returns a non-OK
  // status.
  //
  // The returned file may be concurrently accessed by multiple threads.
  static Status NewRandomAccessFile(const string &fname,
                                    std::unique_ptr<RandomAccessFile> *result,
                                    const EnvOptions &options);

  // Create an object that writes to a new file with the specified
  // name.  Deletes any existing file with the same name and creates a
  // new file.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure stores nullptr in *result and
  // returns non-OK.
  // reopen arg means append.
  // The returned file will only be accessed by one thread at a time.
  static Status OpenWritableFile(const string &fname,
                                 std::unique_ptr<WritableFile> *result,
                                 const EnvOptions &options,
                                 bool reopen = false);

  // AppendWritableFile
  static Status ReopenWritableFile(const string &fname,
                                   std::unique_ptr<WritableFile> *result,
                                   const EnvOptions &options);

  static Status NewWritableFile(const string &fname,
                                std::unique_ptr<WritableFile> *result,
                                const EnvOptions &options);

  // Open `fname` for random read and write, if file doesn't exist the file
  // will be created.  On success, stores a pointer to the new file in
  // *result and returns OK.  On failure returns non-OK.
  //
  // The returned file will only be accessed by one thread at a time.
  static Status NewRandomRWFile(const string &fname,
                                std::unique_ptr<RandomRWFile> *result,
                                const EnvOptions &options);

  // Opens `fname` as a memory-mapped file for read and write (in-place updates
  // only, i.e., no appends). On success, stores a raw buffer covering the whole
  // file in `*result`. The file must exist prior to this call.
  static Status NewMemoryMappedFileBuffer(
      const string &fname,
      std::unique_ptr<MemoryMappedFileBuffer> *result);

  // Create an object that represents a directory. Will fail if directory
  // doesn't exist. If the directory exists, it will open the directory
  // and create a new Directory object.
  //
  // On success, stores a pointer to the new Directory in
  // *result and returns OK. On failure stores nullptr in *result and
  // returns non-OK.
  static Status NewDirectory(const string &name,
                             std::unique_ptr<Directory> *result);

  // Returns OK if the named file exists.
  static Status FileExists(const string &fname);

  // Returns true if the named directory exists and is a directory.
  static bool IsDirectory(const string &dname);

  // Obtains statistics for the given path.
  static Status Stat(const string &fname, FileStatistics *stat);

  // Store the size of fname in *file_size.
  static Status GetFileSize(const string &fname, uint64_t *file_size);

  // Store the last modification time of fname in *file_mtime.
  static Status GetFileModificationTime(const string &fname,
                                        uint64_t *file_mtime);

  // Store in *result the names of the children of the specified directory.
  // The names are relative to "dir".
  // Original contents of *results are dropped.
  static Status GetDirChildren(const string &dir,
                               std::vector<string> *result);

  static Status GetDirChildrenRecursively(const string &dir,
                                          std::vector<string> *result);

  // Store in *result the attributes of the children of the specified directory.
  // In case the implementation lists the directory prior to iterating the files
  // and files are concurrently deleted, the deleted files will be omitted from
  // result.
  // The name attributes are relative to "dir".
  // Original contents of *results are dropped.
  static Status GetChildrenFileAttributes(const string &dir,
                                          std::vector<FileAttributes> *result);

  /// \brief Returns true if the path matches the given pattern. The wildcards
  /// allowed in pattern are described in FileSystem::GetMatchingPaths.
  static bool MatchPath(const string &path, const string &pattern);

  // Delete the named file.
  static Status DeleteFile(const string &fname);

  // Create the specified directory. Returns error if directory exists.
  static Status CreateDir(const string &dirname);

  // Creates directory if missing. Return Ok if it exists, or successful in Creating.
  static Status CreateDirIfMissing(const string &dirname);

  /// \brief Creates the specified directory and all the necessary
  /// subdirectories. Typical return codes.
  ///  * OK - successfully created the directory and sub directories, even if
  ///         they were already created.
  ///  * PERMISSION_DENIED - dirname or some subdirectory is not writable.
  static Status CreateDirRecursively(const string &dirname);

  // Ensure all directories in path exist
  static Status CreatePath(const string &path);

  // Delete the specified directory.
  static Status DeleteDir(const string &dirname);

  /// \brief Deletes the specified directory and all subdirectories and files
  /// underneath it. undeleted_files and undeleted_dirs stores the number of
  /// files and directories that weren't deleted (unspecified if the return
  /// status is not OK).
  /// REQUIRES: undeleted_files, undeleted_dirs to be not null.
  /// Typical return codes
  ///  * OK - dirname exists and we were able to delete everything underneath.
  static Status DeleteDirRecursively(const string &dirname, int64_t *undeleted_files,
                                     int64_t *undeleted_dirs);

  // Rename file src to target.
  static Status RenameFile(const string &src, const string &target);

  // Hard Link file src to target.
  static Status LinkFile(const string &src, const string &target);

  static Status AreFilesSame(const string &first,
                             const string &second, bool *res);

  // Lock the specified file.  Used to prevent concurrent access to
  // the same db by multiple processes.  On failure, stores nullptr in
  // *lock and returns non-OK.
  //
  // On success, stores a pointer to the object that represents the
  // acquired lock in *lock and returns OK.  The caller should call
  // UnlockFile(*lock) to release the lock.  If the process exits,
  // the lock will be automatically released.
  //
  // If somebody else already holds the lock, finishes immediately
  // with a failure.  I.e., this call does not wait for existing locks
  // to go away.
  //
  // May create the named file if it does not already exist.
  static Status LockFile(const string &fname, FileLock **lock);

  // Release the lock acquired by a previous successful call to LockFile.
  // REQUIRES: lock was returned by a successful LockFile() call
  // REQUIRES: lock has not already been unlocked.
  static Status UnlockFile(FileLock *lock);

  static uint64_t Du(const string &path);

  // A utility routine: write "data" to the named file.
  static Status WriteStringToFile(const StringPiece &data,
                                  const string &fname,
                                  bool should_sync = false);

  // A utility routine: read contents of named file into *data
  static Status ReadFileToString(const string &fname,
                                 string *data);

  // Generates a unique id that can be used to identify a db
  static string GenerateUniqueId();

  static string PriorityToString(Priority priority);

  // Returns the ID of the current thread.
  static uint64_t GetThreadID();

  static uint64_t GetStdThreadId();

  // Start a new thread, invoking "function(arg)" within the new thread.
  // When "function(arg)" returns, the thread will be destroyed.
  static void StartNewPthread(void (*function)(void* arg), void* arg);

  /// Returns a new thread that is running fn() and is identified
  /// (for debugging/performance-analysis) by "name".
  /// Caller takes ownership of the result and must delete it eventually
  /// (the deletion will block until fn() stops running).
  static Thread *StartNewThread(const ThreadOptions &thread_options,
                                const string &name,
                                std::function<void()> fn);

  // NewThreadPool() is a function that could be used to create a ThreadPool
  // with `num_threads` background threads.
  static ThreadPool *NewThreadPool(int32_t num_threads);

  ///////////////////////////////////////////////////////////////////////////

  // The number of background worker threads of a specific thread pool
  // for this environment. 'LOW' is the default pool.
  // default number: 1
  virtual void SetBackgroundThreads(int32_t number, Priority pri = LOW){};
  virtual int32_t GetBackgroundThreads(Priority pri = LOW) { return 0; };

  // Enlarge number of background worker threads of a specific thread pool
  // for this environment if it is smaller than specified. 'LOW' is the default
  // pool.
  virtual void IncBackgroundThreadsIfNeeded(int32_t number, Priority pri){};

  // Lower IO priority for threads from the specified pool.
  virtual void LowerThreadPoolIOPriority(Priority pool = LOW) {}

  // Lower CPU priority for threads from the specified pool.
  virtual void LowerThreadPoolCPUPriority(Priority pool = LOW) {}

  // Arrange to run "(*function)(arg)" once in a background thread, in
  // the thread pool specified by pri. By default, jobs go to the 'LOW'
  // priority thread pool.
  // "function" may run in an unspecified thread.  Multiple functions
  // added to the same Env may run concurrently in different threads.
  // I.e., the caller may not assume that background work items are
  // serialized.
  // When the UnSchedule function is called, the unschedFunction
  // registered at the time of Schedule is invoked with arg as a parameter.
  virtual void Schedule(void (*function)(void *arg), void *arg,
                        Priority pri = LOW, void *tag = nullptr,
                        void (*unschedFunction)(void *arg) = nullptr){};

  // Arrange to remove jobs for given arg from the queue_ if they are not
  // already scheduled. Caller is expected to have exclusive lock on arg.
  virtual int32_t UnSchedule(void *arg, Priority pri) { return 0; }

  // Start a new thread, invoking "function(arg)" within the new thread.
  // When "function(arg)" returns, the thread will be destroyed.
  virtual void StartThread(void (*function)(void *arg), void *arg){};

  // Wait for all threads started by StartThread to terminate.
  virtual void WaitForJoin() {}

  // Get thread pool queue length for specific thread pool.
  virtual uint32_t GetThreadPoolQueueLen(Priority pri = LOW) const
  {
    return 0;
  }

  Env() {}
  virtual ~Env() {}
};

////////////////////////////////////////////////////////////////////////////////////////////
/// io interface

// A file abstraction for reading sequentially through a file
class SequentialFile
{
public:
  SequentialFile() {}
  virtual ~SequentialFile(){};

  // Read up to "n" bytes from the file.  "scratch[0..n-1]" may be
  // written by this routine.  Sets "*result" to the data that was
  // read (including if fewer than "n" bytes were successfully read).
  // May set "*result" to point at data in "scratch[0..n-1]", so
  // "scratch[0..n-1]" must be live when "*result" is used.
  // If an error was encountered, returns a non-OK status.
  //
  // REQUIRES: External synchronization
  virtual Status read(uint64_t n, StringPiece *result, char *scratch) = 0;

  // Skip "n" bytes from the file. This is guaranteed to be no
  // slower that reading the same data, but may be faster.
  //
  // If end of file is reached, skipping will stop at the end of the
  // file, and Skip will return OK.
  //
  // REQUIRES: External synchronization
  virtual Status skip(uint64_t n) = 0;

  // Indicates the upper layers if the current SequentialFile implementation
  // uses direct IO.
  virtual bool use_direct_io() const { return false; }

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
  virtual uint64_t getRequiredBufferAlignment() const { return Env::kDefaultPageSize; }

  virtual Status readLine(char *buf, int32_t n) = 0;

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  virtual Status invalidateCache(uint64_t offset, uint64_t length)
  {
    return Status::NotSupported("InvalidateCache not supported.");
  }

  // Positioned Read for direct I/O
  // If Direct I/O enabled, offset, n, and scratch should be properly aligned
  virtual Status positionedRead(uint64_t offset, uint64_t n,
                                StringPiece *result, char *scratch)
  {
    return Status::NotSupported("PositionRead not supported.");
  }

  // Returns the number of bytes from the beginning of the file.
  virtual Status getCurrentPos(int64_t *curpos) = 0;

  // Returns fd, do not use it to modify file
  virtual int32_t getFD() = 0;
};

// A file abstraction for randomly reading the contents of a file.
class RandomAccessFile
{
public:
  enum AccessPattern
  {
    NORMAL,
    RANDOM,
    SEQUENTIAL,
    WILLNEED,
    DONTNEED
  };

  RandomAccessFile() {}
  virtual ~RandomAccessFile(){};

  // Read up to "n" bytes from the file starting at "offset".
  // "scratch[0..n-1]" may be written by this routine.  Sets "*result"
  // to the data that was read (including if fewer than "n" bytes were
  // successfully read).  May set "*result" to point at data in
  // "scratch[0..n-1]", so "scratch[0..n-1]" must be live when
  // "*result" is used.  If an error was encountered, returns a non-OK
  // status.
  //
  // Safe for concurrent use by multiple threads.
  virtual Status read(uint64_t offset, uint64_t n, StringPiece *result,
                      char *scratch) const = 0;

  // Readahead the file starting from offset by n bytes for caching.
  virtual Status prefetch(uint64_t offset, uint64_t n)
  {
    return Status::OK();
  }

  // Used by the file_reader_writer to decide if the ReadAhead wrapper
  // should simply forward the call and do not enact buffering or locking.
  virtual bool shouldForwardRawRequest() const
  {
    return false;
  }

  // For cases when read-ahead is implemented in the platform dependent
  // layer
  virtual void enableReadAhead() {}

  // Tries to get an unique ID for this file that will be the same each time
  // the file is opened (and will stay the same while the file is open).
  // Furthermore, it tries to make this ID at most "max_size" bytes. If such an
  // ID can be created this function returns the length of the ID and places it
  // in "id"; otherwise, this function returns 0, in which case "id"
  // may not have been modified.
  //
  // This function guarantees, for IDs from a given environment, two unique ids
  // cannot be made equal to eachother by adding arbitrary bytes to one of
  // them. That is, no unique ID is the prefix of another.
  //
  // This function guarantees that the returned ID will not be interpretable as
  // a single varint.
  //
  // Note: these IDs are only valid for the duration of the process.
  virtual uint64_t getUniqueId(char *id, uint64_t max_size) const
  {
    return 0; // Default implementation to prevent issues with backwards compatibility.
  };

  virtual void hint(AccessPattern pattern) {}

  // Indicates the upper layers if the current RandomAccessFile implementation
  // uses direct IO.
  virtual bool use_direct_io() const { return false; }

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
  virtual uint64_t getRequiredBufferAlignment() const { return Env::kDefaultPageSize; }

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  virtual Status invalidateCache(uint64_t offset, uint64_t length)
  {
    return Status::NotSupported("InvalidateCache not supported.");
  }

  // Returns fd, do not use it to modify file
  virtual int32_t getFD() = 0;
};

// A file abstraction for sequential writing.  The implementation
// must provide buffering since callers may append small fragments
// at a time to the file.
class WritableFile
{
public:
  WritableFile()
      : last_preallocated_block_(0),
        preallocation_block_size_(0),
        io_priority_(Env::IO_TOTAL),
        write_hint_(Env::WLTH_NOT_SET)
  {
  }
  virtual ~WritableFile(){};

  // Append data to the end of the file
  // Note: A WriteabelFile object must support either Append or
  // PositionedAppend, so the users cannot mix the two.
  virtual Status append(const StringPiece &data) = 0;

  // PositionedAppend data to the specified offset. The new EOF after append
  // must be larger than the previous EOF. This is to be used when writes are
  // not backed by OS buffers and hence has to always start from the start of
  // the sector. The implementation thus needs to also rewrite the last
  // partial sector.
  // Note: PositionAppend does not guarantee moving the file offset after the
  // write. A WriteabelFile object must support either Append or
  // PositionedAppend, so the users cannot mix the two.
  //
  // PositionedAppend() can only happen on the page/sector boundaries. For that
  // reason, if the last write was an incomplete sector we still need to rewind
  // back to the nearest sector/page and rewrite the portion of it with whatever
  // we need to add. We need to keep where we stop writing.
  //
  // PositionedAppend() can only write whole sectors. For that reason we have to
  // pad with zeros for the last write and trim the file when closing according
  // to the position we keep in the previous step.
  //
  // PositionedAppend() requires aligned buffer to be passed in. The alignment
  // required is queried via GetRequiredBufferAlignment()
  virtual Status positionedAppend(const StringPiece &data, uint64_t offset)
  {
    return Status::NotSupported("PositionedAppend not supported");
  }

  // Truncate is necessary to trim the file to the correct size
  // before closing. It is not always possible to keep track of the file
  // size due to whole pages writes. The behavior is undefined if called
  // with other writes to follow.
  virtual Status truncate(uint64_t size)
  {
    return Status::OK();
  }
  virtual Status close() = 0;
  virtual Status flush() = 0;
  virtual Status sync() = 0; // sync data

  /*
   * Sync data and/or metadata as well.
   * By default, sync only data.
   * Override this method for environments where we need to sync
   * metadata as well.
   */
  virtual Status fsync()
  {
    return sync();
  }

  // true if Sync() and Fsync() are safe to call concurrently with Append()
  // and Flush().
  virtual bool isSyncThreadSafe() const
  {
    return false;
  }

  // Indicates the upper layers if the current WritableFile implementation
  // uses direct IO.
  virtual bool use_direct_io() const { return false; }

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
  virtual uint64_t getRequiredBufferAlignment() const { return Env::kDefaultPageSize; }

  /*
   * Change the priority in rate limiter if rate limiting is enabled.
   * If rate limiting is not enabled, this call has no effect.
   */
  virtual void setIOPriority(Env::IOPriority pri)
  {
    io_priority_ = pri;
  }

  virtual Env::IOPriority getIOPriority() { return io_priority_; }

  virtual void setWriteLifeTimeHint(Env::WriteLifeTimeHint hint)
  {
    write_hint_ = hint;
  }

  virtual Env::WriteLifeTimeHint getWriteLifeTimeHint() { return write_hint_; }

  /*
   * Get the size of valid data in the file.
   */
  virtual uint64_t getFileSize()
  {
    return 0;
  }

  /*
   * Get and set the default pre-allocation block size for writes to
   * this file.  If non-zero, then Allocate will be used to extend the
   * underlying storage of a file (generally via fallocate) if the Env
   * instance supports it.
   */
  virtual void setPreallocationBlockSize(uint64_t size)
  {
    preallocation_block_size_ = size;
  }

  virtual void getPreallocationStatus(uint64_t *block_size,
                                      uint64_t *last_allocated_block)
  {
    *last_allocated_block = last_preallocated_block_;
    *block_size = preallocation_block_size_;
  }

  // For documentation, refer to RandomAccessFile::GetUniqueId()
  virtual uint64_t getUniqueId(char *id, uint64_t max_size) const
  {
    return 0; // Default implementation to prevent issues with backwards
  }

  // Remove any kind of caching of data from the offset to offset+length
  // of this file. If the length is 0, then it refers to the end of file.
  // If the system is not caching the file contents, then this is a noop.
  // This call has no effect on dirty pages in the cache.
  virtual Status invalidateCache(uint64_t offset, uint64_t length)
  {
    return Status::NotSupported("InvalidateCache not supported.");
  }

  // Sync a file range with disk.
  // offset is the starting byte of the file range to be synchronized.
  // nbytes specifies the length of the range to be synchronized.
  // This asks the OS to initiate flushing the cached data to disk,
  // without waiting for completion.
  // Default implementation does nothing.
  virtual Status rangeSync(uint64_t offset, uint64_t nbytes) { return Status::OK(); }

  // PrepareWrite performs any necessary preparation for a write
  // before the write actually occurs.  This allows for pre-allocation
  // of space on devices where it can result in less file
  // fragmentation and/or less waste from over-zealous filesystem
  // pre-allocation.
  virtual void prepareWrite(uint64_t offset, uint64_t len)
  {
    if (preallocation_block_size_ == 0)
    {
      return;
    }
    // If this write would cross one or more preallocation blocks,
    // determine what the last preallocation block necessary to
    // cover this write would be and Allocate to that point.
    const auto block_size = preallocation_block_size_;
    uint64_t new_last_preallocated_block =
        (offset + len + block_size - 1) / block_size;
    if (new_last_preallocated_block > last_preallocated_block_)
    {
      uint64_t num_spanned_blocks =
          new_last_preallocated_block - last_preallocated_block_;
      allocate(block_size * last_preallocated_block_,
               block_size * num_spanned_blocks);
      last_preallocated_block_ = new_last_preallocated_block;
    }
  }

  // Pre-allocates space for a file.
  virtual Status allocate(uint64_t offset, uint64_t len)
  {
    return Status::OK();
  }

  // Returns fd, do not use it to modify file
  virtual int32_t getFD() = 0;

protected:
  uint64_t preallocation_block_size() { return preallocation_block_size_; }

private:
  DISALLOW_COPY_AND_ASSIGN(WritableFile);

  uint64_t last_preallocated_block_;
  uint64_t preallocation_block_size_;

protected:
  Env::IOPriority io_priority_;
  Env::WriteLifeTimeHint write_hint_;
};

// A file abstraction for random reading and writing.
class RandomRWFile
{
public:
  RandomRWFile() {}
  virtual ~RandomRWFile() {}

  // Indicates if the class makes use of direct I/O
  // If false you must pass aligned buffer to Write()
  virtual bool use_direct_io() const { return false; }

  // Use the returned alignment value to allocate
  // aligned buffer for Direct I/O
  virtual uint64_t getRequiredBufferAlignment() const { return Env::kDefaultPageSize; }

  // Write bytes in `data` at  offset `offset`, Returns Status::OK() on success.
  // Pass aligned buffer when UseOSBuffer() returns false.
  virtual Status write(uint64_t offset, const StringPiece &data) = 0;

  // Read up to `n` bytes starting from offset `offset` and store them in
  // result, provided `scratch` size should be at least `n`.
  // Returns Status::OK() on success.
  virtual Status read(uint64_t offset, uint64_t n, StringPiece *result,
                      char *scratch) const = 0;

  virtual Status flush() = 0;

  virtual Status sync() = 0;

  virtual Status fsync() { return sync(); }

  virtual Status close() = 0;

  // Returns fd, do not use it to modify file
  virtual int32_t getFD() = 0;

private:
  DISALLOW_COPY_AND_ASSIGN(RandomRWFile);
};

// MemoryMappedFileBuffer object represents a memory-mapped file's raw buffer.
// Subclasses should release the mapping upon destruction.
class MemoryMappedFileBuffer
{
public:
  MemoryMappedFileBuffer(void *_base, uint64_t _length)
      : base_(_base), length_(_length) {}

  virtual ~MemoryMappedFileBuffer(){};

  void *getBase() const { return base_; }
  uint64_t getLen() const { return length_; }

protected:
  void *base_;
  const uint64_t length_;

  DISALLOW_COPY_AND_ASSIGN(MemoryMappedFileBuffer);
};

// Directory object represents collection of files and implements
// filesystem operations that can be executed on directories.
class Directory
{
public:
  virtual ~Directory() {}
  // Fsync directory. Can be called concurrently from multiple threads.
  virtual Status fsync() = 0;

  virtual uint64_t getUniqueId(char *id, size_t max_size) const
  {
    return 0;
  }
};

/// Represents a thread used to run a function.
class Thread
{
public:
  Thread() {}

  /// Blocks until the thread of control stops running.
  virtual ~Thread(){};

private:
  DISALLOW_COPY_AND_ASSIGN(Thread);
};

/*
 * ThreadPool is a component that will spawn N background threads that will
 * be used to execute scheduled work, The number of background threads could
 * be modified by calling SetBackgroundThreads().
 * */
class ThreadPool
{
public:
  virtual ~ThreadPool() {}

  // Wait for all threads to finish.
  // Discard those threads that did not start
  // executing
  virtual void joinAllThreads() = 0;

  // Set the number of background threads that will be executing the
  // scheduled jobs.
  virtual void setBackgroundThreads(int32_t num) = 0;
  virtual int32_t getBackgroundThreads() = 0;

  // Get the number of jobs scheduled in the ThreadPool queue.
  virtual uint32_t getQueueLen() const = 0;

  // Waits for all jobs to complete those
  // that already started running and those that did not
  // start yet. This ensures that everything that was thrown
  // on the TP runs even though
  // we may not have specified enough threads for the amount
  // of jobs
  virtual void waitForJobsAndJoinAllThreads() = 0;

  // Submit a fire and forget jobs
  // This allows to submit the same job multiple times
  virtual void submitJob(const std::function<void()> &) = 0;
  // This moves the function in for efficiency
  virtual void submitJob(std::function<void()> &&) = 0;
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ENV_UTIL_H_