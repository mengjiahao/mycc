#ifndef MYCC_UTIL_FILE_UTIL_H_
#define MYCC_UTIL_FILE_UTIL_H_

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "types_util.h"

namespace mycc
{
namespace util
{

// Make file descriptor |fd| non-blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_fd_non_blocking(int fd);

// Make file descriptor |fd| blocking
// Returns 0 on success, -1 otherwise and errno is set (by fcntl)
int make_fd_blocking(int fd);

// Make file descriptor |fd| automatically closed during exec()
// Returns 0 on success, -1 when error and errno is set (by fcntl)
int make_fd_close_on_exec(int fd);

// Disable nagling on file descriptor |socket|.
// Returns 0 on success, -1 when error and errno is set (by setsockopt)
int make_fd_no_delay(int socket);

/** Create a pipe and set both ends to have F_CLOEXEC
 *
 * @param pipefd        pipe array, just as in pipe(2)
 * @return              0 on success, errno otherwise 
 */
bool pipe_cloexec(int pipefd[2]);
bool pipe2_cloexec(int pipefd[2]);

/*
 * Safe functions wrapping the raw read() and write() libc functions.
 * These retry on EINTR, and on error return -errno instead of returning
 * -1 and setting errno).
 */
ssize_t safe_read(int fd, void *buf, uint64_t count);
ssize_t safe_write(int fd, const void *buf, uint64_t count);
ssize_t safe_pread(int fd, void *buf, uint64_t count, int64_t offset);
ssize_t safe_pwrite(int fd, const void *buf, uint64_t count, int64_t offset);

/*
 * Similar to the above (non-exact version) and below (exact version).
 * See splice(2) for parameter descriptions.
 */
ssize_t safe_splice(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                    uint64_t len, unsigned int flags);
ssize_t safe_splice_exact(int fd_in, int64_t *off_in, int fd_out,
                          int64_t *off_out, uint64_t len, unsigned int flags);

/*
 * Same as the above functions, but return -EDOM unless exactly the requested
 * number of bytes can be read.
 */
ssize_t safe_read_exact(int fd, void *buf, uint64_t count);
ssize_t safe_pread_exact(int fd, void *buf, uint64_t count, int64_t offset);

/*
 * Safe functions to read and write an entire file.
 */
int safe_write_file(const char *base, const char *file,
                    const char *val, uint64_t vallen);
int safe_read_file(const char *base, const char *file,
                   char *val, uint64_t vallen);

int make_dir(const string &path);

/// @brief 创建多级目录，连同父目录一起创建(同mkdir -p)
/// @param path 要创建的目录
/// @return 0成功，非0失败
int make_dir_p(const string &path);

bool GetFileContent(const string &file_name, string *content);
bool CopyFileContent(const string &from, const string &to);

// RAII file descriptor.
//
// Example:
//    FdGuard fd1(open(...));
//    if (fd1 < 0) {
//        printf("Fail to open\n");
//        return -1;
//    }
//    if (another-error-happened) {
//        printf("Fail to do sth\n");
//        return -1;   // *** closing fd1 automatically ***
//    }
class FdGuard
{
public:
  FdGuard() : _fd(-1) {}
  explicit FdGuard(int fd) : _fd(fd) {}

  ~FdGuard()
  {
    if (_fd >= 0)
    {
      ::close(_fd);
      _fd = -1;
    }
  }

  // Close current fd and replace with another fd
  void reset(int fd)
  {
    if (_fd >= 0)
    {
      ::close(_fd);
      _fd = -1;
    }
    _fd = fd;
  }

  // Set internal fd to -1 and return the value before set.
  int release()
  {
    const int prev_fd = _fd;
    _fd = -1;
    return prev_fd;
  }

  int getfd() const
  {
    return _fd;
  }

  operator int() const { return _fd; }

private:
  // Copying this makes no sense.
  DISALLOW_COPY_AND_ASSIGN(FdGuard);

  int _fd;
};

class TCMmap
{
public:
  // if bOwner, need munmap
  TCMmap(bool bOwner = true);
  ~TCMmap();
  // if fd is -1, flags is MAP_ANONYMOUS
  // prot: PROT_READ, PROT_WRITE, PROT_EXEC, PROT_NONE
  // flags: MAP_SHARED, MAP_PRIVATE, MAP_FIXED
  void mmap(uint64_t length, int prot, int flags, int fd, int64_t offset = 0);
  // PROT_READ|PROT_WRITE, MAP_SHARED
  // size = length + 1, there is a hole
  void mmap(const char *file, uint64_t length);
  void munmap();
  void msync(bool bSync = false);
  void *getPointer() const { return _pAddr; }
  uint64_t getSize() const { return _iLength; }
  // created or already exist
  bool iscreate() const { return _bCreate; }
  void setOwner(bool bOwner) { _bOwner = bOwner; }

protected:
  bool _bOwner;
  void *_pAddr;
  uint64_t _iLength;
  bool _bCreate;
};

class TCFifo
{
public:
  enum ENUM_RW_SET
  {
    EM_WRITE = 1,
    EM_READ = 2
  };

public:
  TCFifo(bool bOwener = true);
  ~TCFifo();

public:
  int open(const string &sPath, ENUM_RW_SET enRW, mode_t mode = 0777);
  void close();
  int fd() const { return _fd; }
  // 0< means read byte, =0 means end, <0 means error
  int read(char *szBuff, const uint64_t sizeMax);
  int write(const char *szBuff, const uint64_t sizeBuffLen);

private:
  string _sPathName;
  bool _bOwner;
  ENUM_RW_SET _enRW;
  int _fd;
};

// process mutex
class TCFileMutex
{
public:
  TCFileMutex();
  virtual ~TCFileMutex();
  void init(const string &filename);
  int rlock();
  int unrlock();
  bool tryrlock();
  int wlock();
  int unwlock();
  bool trywlock();
  int lock() { return wlock(); };
  int unlock();
  bool trylock() { return trywlock(); };

protected:
  // F_RDLCK, F_WRLCK, F_UNLCK
  int lock(int fd, int cmd, int type, int64_t offset, int whence, int64_t len);
  bool hasLock(int fd, int type, int64_t offset, int whence, int64_t len);

private:
  int _fd;
};

class FileOperation
{
public:
  FileOperation(const string &file_name, const int open_flags = O_RDWR | O_LARGEFILE | O_CREAT);
  virtual ~FileOperation();

  int open_file();
  void close_file();
  virtual int flush_file();
  int flush_data();
  int unlink_file();
  int rename_file(const char *new_name);
  inline char *get_file_name() const
  {
    return file_name_;
  }
  virtual int64_t pread_file(char *buf, const int32_t nbytes, const int64_t offset);
  virtual int64_t pwrite_file(const char *buf, const int32_t nbytes, const int64_t offset);
  int write_file(const char *buf, const int32_t nbytes);
  int64_t get_file_size();
  int ftruncate_file(const int64_t length);
  int seek_file(const int64_t offset);
  int32_t current_pos();
  int get_fd() const
  {
    return fd_;
  }

protected:
  FileOperation();
  FileOperation(const FileOperation &);
  int check_file();

protected:
  static const int32_t MAX_DISK_TIMES = 5;
  static const mode_t OPEN_MODE = 0644;

protected:
  int fd_;          // file handle
  int open_flags_;  // open flags
  char *file_name_; // file path name
};

class FileMapper
{
public:
  FileMapper()
  {
    data = NULL;
    size = 0;
    fd = -1;
  }

  ~FileMapper()
  {
    close_file();
  }

  void close_file();
  void sync_file();
  bool open_file(const char *file_name, int64_t create_length = 0);

  void *get_data() const
  {
    return data;
  }

  int64_t get_size() const
  {
    return size;
  }

  int get_modify_time() const
  {
    struct stat buffer;
    if (fd >= 0 && fstat(fd, &buffer) == 0)
    {
      return (int)buffer.st_mtime;
    }
    return 0;
  }

private:
  FileMapper(const FileMapper &);
  FileMapper &operator=(const FileMapper &);

  void *data;
  int64_t size;
  int fd;
};

// Example:
//   FileWatcher fw;
//   fw.init("to_be_watched_file");
//   ....
//   if (fw.check_and_consume() > 0) {
//       // the file is created or updated
//       ......
//   }

class FileWatcher
{
public:
  enum Change
  {
    DELETED = -1,
    UNCHANGED = 0,
    UPDATED = 1,
    CREATED = 2,
  };

  typedef int64_t Timestamp;

  FileWatcher();

  // Watch file at `file_path', must be called before calling other methods.
  // Returns 0 on success, -1 otherwise.
  int init(const char *file_path);
  // Let check_and_consume returns CREATE when file_path already exists.
  int init_from_not_exist(const char *file_path);

  // Check and consume change of the watched file. Write `last_timestamp'
  // if it's not NULL.
  // Returns:
  //   CREATE    the file is created since last call to this method.
  //   UPDATED   the file is modified since last call.
  //   UNCHANGED the file has no change since last call.
  //   DELETED   the file was deleted since last call.
  // Note: If the file is updated too frequently, this method may return
  // UNCHANGED due to precision of stat(2) and the file system. If the file
  // is created and deleted too frequently, the event may not be detected.
  Change check_and_consume(Timestamp *last_timestamp = NULL);

  // Set internal timestamp. User can use this method to make
  // check_and_consume() replay the change.
  void restore(Timestamp timestamp);

  // Get path of watched file
  const char *filepath() const { return _file_path.c_str(); }

private:
  Change check(Timestamp *new_timestamp) const;

  string _file_path;
  Timestamp _last_ts;
};

// Create a temporary file in current directory, which will be deleted when
// corresponding TempFile object destructs, typically for unit testing.
//
// Usage:
//   {
//      TempFile tmpfile;           // A temporay file shall be created
//      tmpfile.save("some text");  // Write into the temporary file
//   }
//   // The temporary file shall be removed due to destruction of tmpfile

class TempFile
{
public:
  // Create a temporary file in current directory. If |ext| is given,
  // filename will be temp_file_XXXXXX.|ext|, temp_file_XXXXXX otherwise.
  // If temporary file cannot be created, all save*() functions will
  // return -1. If |ext| is too long, filename will be truncated.
  TempFile();
  explicit TempFile(const char *ext);

  // The temporary file is removed in destructor.
  ~TempFile();

  // Save |content| to file, overwriting existing file.
  // Returns 0 when successful, -1 otherwise.
  int save(const char *content);

  // Save |fmt| and associated values to file, overwriting existing file.
  // Returns 0 when successful, -1 otherwise.
  int save_format(const char *fmt, ...) __attribute__((format(printf, 2, 3)));

  // Save binary data |buf| (|count| bytes) to file, overwriting existing file.
  // Returns 0 when successful, -1 otherwise.
  int save_bin(const void *buf, uint64_t count);

  // Get name of the temporary file.
  const char *fname() const { return _fname; }

private:
  // TempFile is associated with file, copying makes no sense.
  DISALLOW_COPY_AND_ASSIGN(TempFile);

  int _reopen_if_necessary();

  int _fd; // file descriptor
  int _ever_opened;
  char _fname[24]; // name of the file
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_FILE_UTIL_H_