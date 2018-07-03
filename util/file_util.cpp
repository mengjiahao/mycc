
#include "file_util.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>  // IPPROTO_TCP
#include <netinet/tcp.h> // TCP_NODELAY
#include <limits.h>
#include <stdarg.h> // va_list
#include <stdio.h>  // snprintf, vdprintf
#include <stdlib.h> // mkstemp
#include <string.h> // strlen
#include <sys/file.h>
#include <sys/socket.h> // setsockopt
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h> // close
#include <fstream>
#include <iostream>
#include <new> // placement new
#include <sstream>

#include <linux/limits.h> // PATH_MAX

namespace mycc
{
namespace util
{

int make_fd_non_blocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return flags;
  }
  if (flags & O_NONBLOCK)
  {
    return 0;
  }
  return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int make_fd_blocking(int fd)
{
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0)
  {
    return flags;
  }
  if (flags & O_NONBLOCK)
  {
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
  }
  return 0;
}

int make_fd_close_on_exec(int fd)
{
  return fcntl(fd, F_SETFD, FD_CLOEXEC);
}

int make_fd_no_delay(int socket)
{
  int flag = 1;
  return setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag));
}

bool pipe_cloexec(int pipefd[2])
{
  int ret;
  ret = pipe(pipefd);
  if (ret != 0)
    return false;

  /*
   * The old-fashioned, race-condition prone way that we have to fall
   * back on if O_CLOEXEC does not exist.
   */
  ret = fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
  if (ret == -1)
  {
    ret = -errno;
    goto out;
  }

  ret = fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
  if (ret == -1)
  {
    ret = -errno;
    goto out;
  }

  return true;

out:
  close(pipefd[0]);
  close(pipefd[1]);

  return (0 == ret);
}

bool pipe2_cloexec(int pipefd[2])
{
  int ret;
  ret = pipe2(pipefd, O_CLOEXEC);
  return (0 == ret);
}

ssize_t safe_read(int fd, void *buf, uint64_t count)
{
  uint64_t cnt = 0;

  while (cnt < count)
  {
    ssize_t r = read(fd, buf, count - cnt);
    if (r <= 0)
    {
      if (r == 0)
      {
        // EOF
        return cnt;
      }
      if (errno == EINTR)
        continue;
      return -errno;
    }
    cnt += r;
    buf = (char *)buf + r;
  }
  return cnt;
}

ssize_t safe_read_exact(int fd, void *buf, uint64_t count)
{
  ssize_t ret = safe_read(fd, buf, count);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != count)
    return -EDOM;
  return 0;
}

ssize_t safe_write(int fd, const void *buf, uint64_t count)
{
  while (count > 0)
  {
    ssize_t r = write(fd, buf, count);
    if (r < 0)
    {
      if (errno == EINTR)
        continue;
      return -errno;
    }
    count -= r;
    buf = (char *)buf + r;
  }
  return 0;
}

ssize_t safe_pread(int fd, void *buf, uint64_t count, int64_t offset)
{
  uint64_t cnt = 0;
  char *b = (char *)buf;

  while (cnt < count)
  {
    ssize_t r = pread(fd, b + cnt, count - cnt, offset + cnt);
    if (r <= 0)
    {
      if (r == 0)
      {
        // EOF
        return cnt;
      }
      if (errno == EINTR)
        continue;
      return -errno;
    }

    cnt += r;
  }
  return cnt;
}

ssize_t safe_pread_exact(int fd, void *buf, uint64_t count, int64_t offset)
{
  ssize_t ret = safe_pread(fd, buf, count, offset);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != count)
    return -EDOM;
  return 0;
}

ssize_t safe_pwrite(int fd, const void *buf, uint64_t count, int64_t offset)
{
  while (count > 0)
  {
    ssize_t r = pwrite(fd, buf, count, offset);
    if (r < 0)
    {
      if (errno == EINTR)
        continue;
      return -errno;
    }
    count -= r;
    buf = (char *)buf + r;
    offset += r;
  }
  return 0;
}

ssize_t safe_splice(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                    uint64_t len, unsigned int flags)
{
  uint64_t cnt = 0;

  while (cnt < len)
  {
    ssize_t r = splice(fd_in, off_in, fd_out, off_out, len - cnt, flags);
    if (r <= 0)
    {
      if (r == 0)
      {
        // EOF
        return cnt;
      }
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN)
        break;
      return -errno;
    }
    cnt += r;
  }
  return cnt;
}

ssize_t safe_splice_exact(int fd_in, int64_t *off_in, int fd_out,
                          int64_t *off_out, uint64_t len, unsigned int flags)
{
  ssize_t ret = safe_splice(fd_in, off_in, fd_out, off_out, len, flags);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != len)
    return -EDOM;
  return 0;
}

int safe_write_file(const char *base, const char *file,
                    const char *val, uint64_t vallen)
{
  int ret;
  char fn[PATH_MAX];
  char tmp[PATH_MAX];
  int fd;

  // does the file already have correct content?
  char oldval[80];
  ret = safe_read_file(base, file, oldval, sizeof(oldval));
  if (ret == (int)vallen && memcmp(oldval, val, vallen) == 0)
    return 0; // yes.

  snprintf(fn, sizeof(fn), "%s/%s", base, file);
  snprintf(tmp, sizeof(tmp), "%s/%s.tmp", base, file);
  fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd < 0)
  {
    ret = errno;
    return -ret;
  }
  ret = safe_write(fd, val, vallen);
  if (ret)
  {
    close(fd);
    return ret;
  }

  ret = fsync(fd);
  if (ret < 0)
    ret = -errno;
  close(fd);
  if (ret < 0)
  {
    unlink(tmp);
    return ret;
  }
  ret = rename(tmp, fn);
  if (ret < 0)
  {
    ret = -errno;
    unlink(tmp);
    return ret;
  }

  fd = open(base, O_RDONLY);
  if (fd < 0)
  {
    ret = -errno;
    return ret;
  }
  ret = fsync(fd);
  if (ret < 0)
    ret = -errno;
  close(fd);

  return ret;
}

int safe_read_file(const char *base, const char *file,
                   char *val, uint64_t vallen)
{
  char fn[PATH_MAX];
  int fd, len;

  snprintf(fn, sizeof(fn), "%s/%s", base, file);
  fd = open(fn, O_RDONLY);
  if (fd < 0)
  {
    return -errno;
  }
  len = safe_read(fd, val, vallen);
  if (len < 0)
  {
    close(fd);
    return len;
  }
  // close sometimes returns errors, but only after write()
  close(fd);
  return len;
}

int make_dir(const string &path)
{
  if (path.empty())
  {
    return -1;
  }

  if (access(path.c_str(), F_OK | W_OK) == 0)
  {
    return 0;
  }

  if (mkdir(path.c_str(), 0755) != 0)
  {
    return -1;
  }
  return 0;
}

int make_dir_p(const string &path)
{
  if (path.empty())
  {
    return -1;
  }
  if (path.size() > PATH_MAX)
  {
    return -1;
  }

  int len = path.length();
  char tmp[PATH_MAX] = {0};
  snprintf(tmp, sizeof(tmp), "%s", path.c_str());

  for (int i = 1; i < len; i++)
  {
    if (tmp[i] != '/')
    {
      continue;
    }

    tmp[i] = '\0';
    if (make_dir(tmp) != 0)
    {
      return -1;
    }
    tmp[i] = '/';
  }

  return make_dir(path);
}

bool GetFileContent(const string &file_name, string *content)
{
  std::ifstream fin(file_name);
  if (!fin)
  {
    return false;
  }

  std::stringstream str_stream;
  str_stream << fin.rdbuf();
  *content = str_stream.str();
  return true;
}

bool CopyFileContent(const string &from, const string &to)
{
  std::ifstream src(from, std::ios::binary);
  if (!src)
  {
    return false;
  }

  std::ofstream dst(to, std::ios::binary);
  if (!dst)
  {
    return false;
  }

  dst << src.rdbuf();
  return true;
}

TCMmap::TCMmap(bool bOwner)
    : _bOwner(bOwner), _pAddr(NULL), _iLength(0), _bCreate(false)
{
}

TCMmap::~TCMmap()
{
  if (_bOwner)
  {
    munmap();
  }
}

void TCMmap::mmap(uint64_t length, int prot, int flags, int fd, int64_t offset)
{
  if (_bOwner)
  {
    munmap();
  }
  _pAddr = ::mmap(NULL, length, prot, flags, fd, offset);
  if (_pAddr == (void *)-1)
  {
    _pAddr = NULL;
    //throw TC_Mmap_Exception("[TCMmap::mmap] mmap error", errno);
    return;
  }
  _iLength = length;
  _bCreate = false;
}

void TCMmap::mmap(const char *file, uint64_t length)
{
  assert(length > 0);
  if (_bOwner)
  {
    munmap();
  }

  int fd = open(file, O_CREAT | O_EXCL | O_RDWR, 0666);
  if (fd == -1)
  {
    if (errno != EEXIST)
    {
      //throw TC_Mmap_Exception("[TCMmap::mmap] fopen file '" + string(file) + "' error", errno);
      return;
    }
    else
    {
      fd = open(file, O_CREAT | O_RDWR, 0666);
      if (fd == -1)
      {
        //throw TC_Mmap_Exception("[TCMmap::mmap] fopen file '" + string(file) + "' error", errno);
        return;
      }
      _bCreate = false;
    }
  }
  else
  {
    _bCreate = true;
  }

  lseek(fd, length - 1, SEEK_SET);
  write(fd, "\0", 1);

  _pAddr = ::mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (_pAddr == (void *)-1)
  {
    _pAddr = NULL;
    close(fd);
    //throw TC_Mmap_Exception("[TCMmap::mmap] mmap file '" + string(file) + "' error", errno);
    return;
  }
  _iLength = length;
  if (fd != -1)
  {
    close(fd);
  }
}

void TCMmap::munmap()
{
  if (_pAddr == NULL)
  {
    return;
  }

  int ret = ::munmap(_pAddr, _iLength);
  if (ret == -1)
  {
    //throw TC_Mmap_Exception("[TCMmap::munmap] munmap error", errno);
    return;
  }

  _pAddr = NULL;
  _iLength = 0;
  _bCreate = false;
}

void TCMmap::msync(bool bSync)
{
  int ret = 0;
  if (bSync)
  {
    ret = ::msync(_pAddr, _iLength, MS_SYNC | MS_INVALIDATE);
  }
  else
  {
    ret = ::msync(_pAddr, _iLength, MS_ASYNC | MS_INVALIDATE);
  }
  if (ret != 0)
  {
    //throw TC_Mmap_Exception("[TCMmap::msync] msync error", errno);
    return;
  }
}

TCFifo::TCFifo(bool bOwner) : _bOwner(bOwner), _enRW(EM_READ), _fd(-1)
{
}

TCFifo::~TCFifo()
{
  if (_bOwner)
    close();
}

void TCFifo::close()
{
  if (_fd >= 0)
    ::close(_fd);

  _fd = -1;
}

int TCFifo::open(const string &sPathName, ENUM_RW_SET enRW, mode_t mode)
{
  _enRW = enRW;
  _sPathName = sPathName;

  if (_enRW != EM_READ && _enRW != EM_WRITE)
  {
    return -1;
  }

  if (::mkfifo(_sPathName.c_str(), mode) == -1 && errno != EEXIST)
  {
    return -1;
  }

  if (_enRW == EM_READ && (_fd = ::open(_sPathName.c_str(), O_NONBLOCK | O_RDONLY, 0664)) < 0)
  {
    return -1;
  }

  if (_enRW == EM_WRITE && (_fd = ::open(_sPathName.c_str(), O_NONBLOCK | O_WRONLY, 0664)) < 0)
  {
    return -1;
  }

  return 0;
}

int TCFifo::read(char *szBuff, const uint64_t sizeMax)
{
  return ::read(_fd, szBuff, sizeMax);
}

int TCFifo::write(const char *szBuff, const uint64_t sizeBuffLen)
{
  if (sizeBuffLen == 0)
    return 0;

  return ::write(_fd, szBuff, sizeBuffLen);
}

TCFileMutex::TCFileMutex()
{
  _fd = -1;
}

TCFileMutex::~TCFileMutex()
{
  unlock();
}

void TCFileMutex::init(const string &filename)
{
  if (filename.empty())
  {
    //throw TC_FileMutex_Exception("[TCFileMutex::init] filename is empty");
    return;
  }

  if (_fd > 0)
  {
    close(_fd);
  }
  _fd = open(filename.c_str(), O_RDWR | O_CREAT, 0660);
  if (_fd < 0)
  {
    //throw TC_FileMutex_Exception("[TCFileMutex::init] open '" + filename + "' error", errno);
    return;
  }
}

int TCFileMutex::rlock()
{
  assert(_fd > 0);

  return lock(_fd, F_SETLKW, F_RDLCK, 0, 0, 0);
}

int TCFileMutex::unrlock()
{
  return unlock();
}

bool TCFileMutex::tryrlock()
{
  return hasLock(_fd, F_RDLCK, 0, 0, 0);
}

int TCFileMutex::wlock()
{
  assert(_fd > 0);

  return lock(_fd, F_SETLKW, F_WRLCK, 0, 0, 0);
}

int TCFileMutex::unwlock()
{
  return unlock();
}

bool TCFileMutex::trywlock()
{
  return hasLock(_fd, F_WRLCK, 0, 0, 0);
}

int TCFileMutex::unlock()
{
  return lock(_fd, F_SETLK, F_UNLCK, 0, 0, 0);
}

int TCFileMutex::lock(int fd, int cmd, int type, int64_t offset, int whence, int64_t len)
{
  struct flock lock;
  lock.l_type = type;
  lock.l_start = offset;
  lock.l_whence = whence;
  lock.l_len = len;

  return fcntl(fd, cmd, &lock);
}

bool TCFileMutex::hasLock(int fd, int type, int64_t offset, int whence, int64_t len)
{
  struct flock lock;
  lock.l_type = type;
  lock.l_start = offset;
  lock.l_whence = whence;
  lock.l_len = len;

  if (fcntl(fd, F_GETLK, &lock) == -1)
  {
    //throw TC_FileMutex_Exception("[TCFileMutex::hasLock] fcntl error", errno);
    return false;
  }
  if (lock.l_type == F_UNLCK)
  {
    return false;
  }
  return true;
}

static const FileWatcher::Timestamp NON_EXIST_TS =
    static_cast<FileWatcher::Timestamp>(-1);

FileWatcher::FileWatcher() : _last_ts(NON_EXIST_TS)
{
}

int FileWatcher::init(const char *file_path)
{
  if (init_from_not_exist(file_path) != 0)
  {
    return -1;
  }
  check_and_consume(NULL);
  return 0;
}

int FileWatcher::init_from_not_exist(const char *file_path)
{
  if (NULL == file_path)
  {
    return -1;
  }
  if (!_file_path.empty())
  {
    return -1;
  }
  _file_path = file_path;
  return 0;
}

FileWatcher::Change FileWatcher::check(Timestamp *new_timestamp) const
{
  struct stat tmp_st;
  const int ret = stat(_file_path.c_str(), &tmp_st);
  if (ret < 0)
  {
    *new_timestamp = NON_EXIST_TS;
    if (NON_EXIST_TS != _last_ts)
    {
      return DELETED;
    }
    else
    {
      return UNCHANGED;
    }
  }
  else
  {
    // Use microsecond timestamps which can be used for:
    //   2^63 / 1000000 / 3600 / 24 / 365 = 292471 years
    const Timestamp cur_ts =
        tmp_st.st_mtim.tv_sec * 1000000L + tmp_st.st_mtim.tv_nsec / 1000L;
    *new_timestamp = cur_ts;
    if (NON_EXIST_TS != _last_ts)
    {
      if (cur_ts != _last_ts)
      {
        return UPDATED;
      }
      else
      {
        return UNCHANGED;
      }
    }
    else
    {
      return CREATED;
    }
  }
}

FileWatcher::Change FileWatcher::check_and_consume(Timestamp *last_timestamp)
{
  Timestamp new_timestamp;
  Change e = check(&new_timestamp);
  if (last_timestamp)
  {
    *last_timestamp = _last_ts;
  }
  if (e != UNCHANGED)
  {
    _last_ts = new_timestamp;
  }
  return e;
}

void FileWatcher::restore(Timestamp timestamp)
{
  _last_ts = timestamp;
}

// Initializing array. Needs to be macro.
#define BASE_FILES_TEMP_FILE_PATTERN "temp_file_XXXXXX";

TempFile::TempFile() : _ever_opened(0)
{
  char temp_name[] = BASE_FILES_TEMP_FILE_PATTERN;
  _fd = mkstemp(temp_name);
  if (_fd >= 0)
  {
    _ever_opened = 1;
    snprintf(_fname, sizeof(_fname), "%s", temp_name);
  }
  else
  {
    *_fname = '\0';
  }
}

TempFile::TempFile(const char *ext)
{
  if (NULL == ext || '\0' == *ext)
  {
    new (this) TempFile();
    return;
  }

  *_fname = '\0';
  _fd = -1;
  _ever_opened = 0;

  // Make a temp file to occupy the filename without ext.
  char temp_name[] = BASE_FILES_TEMP_FILE_PATTERN;
  const int tmp_fd = mkstemp(temp_name);
  if (tmp_fd < 0)
  {
    return;
  }

  // Open the temp_file_XXXXXX.ext
  snprintf(_fname, sizeof(_fname), "%s.%s", temp_name, ext);

  _fd = open(_fname, O_CREAT | O_WRONLY | O_TRUNC | O_EXCL, 0600);
  if (_fd < 0)
  {
    *_fname = '\0';
  }
  else
  {
    _ever_opened = 1;
  }

  // Close and remove temp_file_XXXXXX anyway.
  close(tmp_fd);
  unlink(temp_name);
}

int TempFile::_reopen_if_necessary()
{
  if (_fd < 0)
  {
    _fd = open(_fname, O_CREAT | O_WRONLY | O_TRUNC, 0600);
  }
  return _fd;
}

TempFile::~TempFile()
{
  if (_fd >= 0)
  {
    close(_fd);
    _fd = -1;
  }
  if (_ever_opened)
  {
    // Only remove temp file when _fd >= 0, otherwise we may
    // remove another temporary file (occupied the same name)
    unlink(_fname);
  }
}

int TempFile::save(const char *content)
{
  return save_bin(content, strlen(content));
}

int TempFile::save_format(const char *fmt, ...)
{
  if (_reopen_if_necessary() < 0)
  {
    return -1;
  }
  va_list ap;
  va_start(ap, fmt);
  const int32_t rc = vdprintf(_fd, fmt, ap);
  va_end(ap);

  close(_fd);
  _fd = -1;
  // TODO: is this right?
  return (rc < 0 ? -1 : 0);
}

// Write until all buffer was written or an error except EINTR.
// Returns:
//    -1   error happened, errno is set
// count   all written
static int64_t temp_file_write_all(int fd, const void *buf, uint64_t count)
{
  uint64_t off = 0;
  for (;;)
  {
    int64_t nw = write(fd, (char *)buf + off, count - off);
    if (nw == (int64_t)(count - off))
    { // including count==0
      return count;
    }
    if (nw >= 0)
    {
      off += nw;
    }
    else if (errno != EINTR)
    {
      return -1;
    }
  }
}

int TempFile::save_bin(const void *buf, uint64_t count)
{
  if (_reopen_if_necessary() < 0)
  {
    return -1;
  }

  const int64_t len = temp_file_write_all(_fd, buf, count);

  close(_fd);
  _fd = -1;
  if (len < 0)
  {
    return -1;
  }
  else if ((uint64_t)len != count)
  {
    errno = ENOSPC;
    return -1;
  }
  return 0;
}

} // namespace util
} // namespace mycc
