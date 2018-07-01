#ifndef MYCC_UTIL_FILE_UTIL_H_
#define MYCC_UTIL_FILE_UTIL_H_

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
ssize_t safe_read(int fd, void *buf, size_t count);
ssize_t safe_write(int fd, const void *buf, size_t count);
ssize_t safe_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t safe_pwrite(int fd, const void *buf, size_t count, off_t offset);

/*
 * Similar to the above (non-exact version) and below (exact version).
 * See splice(2) for parameter descriptions.
 */
ssize_t safe_splice(int fd_in, off_t *off_in, int fd_out, off_t *off_out,
                    size_t len, unsigned int flags);
ssize_t safe_splice_exact(int fd_in, off_t *off_in, int fd_out,
                          off_t *off_out, size_t len, unsigned int flags);

/*
 * Same as the above functions, but return -EDOM unless exactly the requested
 * number of bytes can be read.
 */
ssize_t safe_read_exact(int fd, void *buf, size_t count);
ssize_t safe_pread_exact(int fd, void *buf, size_t count, off_t offset);

/*
 * Safe functions to read and write an entire file.
 */
int safe_write_file(const char *base, const char *file,
                    const char *val, size_t vallen);
int safe_read_file(const char *base, const char *file,
                   char *val, size_t vallen);

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
  int save_bin(const void *buf, size_t count);

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