
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
#include "math_util.h"

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

int64_t safe_read(int fd, void *buf, uint64_t count)
{
  uint64_t cnt = 0;

  while (cnt < count)
  {
    int64_t r = read(fd, buf, count - cnt);
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

int64_t safe_read_exact(int fd, void *buf, uint64_t count)
{
  int64_t ret = safe_read(fd, buf, count);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != count)
    return -EDOM;
  return 0;
}

int64_t safe_write(int fd, const void *buf, uint64_t count)
{
  while (count > 0)
  {
    int64_t r = write(fd, buf, count);
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

int64_t safe_pread(int fd, void *buf, uint64_t count, int64_t offset)
{
  uint64_t cnt = 0;
  char *b = (char *)buf;

  while (cnt < count)
  {
    int64_t r = pread(fd, b + cnt, count - cnt, offset + cnt);
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

int64_t safe_pread_exact(int fd, void *buf, uint64_t count, int64_t offset)
{
  int64_t ret = safe_pread(fd, buf, count, offset);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != count)
    return -EDOM;
  return 0;
}

int64_t safe_pwrite(int fd, const void *buf, uint64_t count, int64_t offset)
{
  while (count > 0)
  {
    int64_t r = pwrite(fd, buf, count, offset);
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

int64_t safe_splice(int fd_in, int64_t *off_in, int fd_out, int64_t *off_out,
                    uint64_t len, unsigned int flags)
{
  uint64_t cnt = 0;

  while (cnt < len)
  {
    int64_t r = splice(fd_in, off_in, fd_out, off_out, len - cnt, flags);
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

int64_t safe_splice_exact(int fd_in, int64_t *off_in, int fd_out,
                          int64_t *off_out, uint64_t len, unsigned int flags)
{
  int64_t ret = safe_splice(fd_in, off_in, fd_out, off_out, len, flags);
  if (ret < 0)
    return ret;
  if ((uint64_t)ret != len)
    return -EDOM;
  return 0;
}

int32_t safe_write_file(const char *base, const char *file,
                        const char *val, uint64_t vallen)
{
  int32_t ret;
  char fn[PATH_MAX];
  char tmp[PATH_MAX];
  int fd;

  // does the file already have correct content?
  char oldval[80];
  ret = safe_read_file(base, file, oldval, sizeof(oldval));
  if (ret == (int32_t)vallen && memcmp(oldval, val, vallen) == 0)
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

int32_t safe_read_file(const char *base, const char *file,
                       char *val, uint64_t vallen)
{
  char fn[PATH_MAX];
  int fd;
  int32_t len;

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

int32_t make_dir(const string &path)
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

int32_t make_dir_p(const string &path)
{
  if (path.empty())
  {
    return -1;
  }
  if (path.size() > PATH_MAX)
  {
    return -1;
  }

  int32_t len = path.length();
  char tmp[PATH_MAX] = {0};
  snprintf(tmp, sizeof(tmp), "%s", path.c_str());

  for (int32_t i = 1; i < len; i++)
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

FileOperation::FileOperation(const string &file_name,
                             const int open_flags)
    : fd_(-1), open_flags_(open_flags)
{
  file_name_ = strdup(file_name.c_str());
}

FileOperation::~FileOperation()
{
  if (fd_ > 0)
  {
    ::close(fd_);
    fd_ = -1;
  }

  if (NULL != file_name_)
  {
    free(file_name_);
    file_name_ = NULL;
  }
}

void FileOperation::close_file()
{
  if (fd_ < 0)
  {
    return;
  }
  ::close(fd_);
  fd_ = -1;
}

int64_t FileOperation::pread_file(char *buf, const int32_t nbytes, const int64_t offset)
{
  int32_t left = nbytes;
  int64_t read_offset = offset;
  int32_t read_len = 0;
  char *p_tmp = buf;

  int i = 0;
  while (left > 0)
  {
    ++i;
    if (i >= MAX_DISK_TIMES)
    {
      break;
    }

    if (check_file() < 0)
      return -errno;

    if ((read_len = ::pread64(fd_, p_tmp, left, read_offset)) < 0)
    {
      read_len = -errno;
      if (EINTR == -read_len || EAGAIN == -read_len)
      {
        continue; /* call pread64() again */
      }
      else if (EBADF == -read_len)
      {
        fd_ = -1;
        continue;
      }
      else
      {
        return read_len;
      }
    }
    else if (0 == read_len)
    {
      break; //reach end
    }

    left -= read_len;
    p_tmp += read_len;
    read_offset += read_len;
  }

  return (p_tmp - buf);
}

int64_t FileOperation::pwrite_file(const char *buf, const int32_t nbytes, const int64_t offset)
{
  int32_t left = nbytes;
  int64_t write_offset = offset;
  int32_t written_len = 0;
  const char *p_tmp = buf;

  int i = 0;
  while (left > 0)
  {
    ++i;
    // disk io time over
    if (i >= MAX_DISK_TIMES)
    {
      break;
    }

    if (check_file() < 0)
      return -errno;

    if ((written_len = ::pwrite64(fd_, p_tmp, left, write_offset)) < 0)
    {
      written_len = -errno;
      if (EINTR == -written_len || EAGAIN == -written_len)
      {
        continue;
      }
      if (EBADF == -written_len)
      {
        fd_ = -1;
        continue;
      }
      else
      {
        return written_len;
      }
    }
    else if (0 == written_len)
    {
      break;
    }

    left -= written_len;
    p_tmp += written_len;
    write_offset += written_len;
  }

  return (p_tmp - buf);
}

int FileOperation::write_file(const char *buf, const int32_t nbytes)
{
  const char *p_tmp = buf;
  int32_t left = nbytes;
  int32_t written_len = 0;
  int i = 0;
  while (left > 0)
  {
    ++i;
    if (i >= MAX_DISK_TIMES)
    {
      break;
    }

    if (check_file() < 0)
      return -errno;

    if ((written_len = ::write(fd_, p_tmp, left)) <= 0)
    {
      written_len = -errno;
      if (EINTR == -written_len || EAGAIN == -written_len)
      {
        continue;
      }
      if (EBADF == -written_len)
      {
        fd_ = -1;
        continue;
      }
      else
      {
        return written_len;
      }
    }

    left -= written_len;
    p_tmp += written_len;
  }

  return p_tmp - buf;
}

int64_t FileOperation::get_file_size()
{
  int fd = check_file();
  if (fd < 0)
    return fd;

  struct stat statbuf;
  if (fstat(fd, &statbuf) != 0)
  {
    return -1;
  }
  return statbuf.st_size;
}

int FileOperation::ftruncate_file(const int64_t length)
{
  int fd = check_file();
  if (fd < 0)
    return fd;

  return ftruncate(fd, length);
}

int FileOperation::seek_file(const int64_t offset)
{
  int fd = check_file();
  if (fd < 0)
    return fd;

  return lseek(fd, offset, SEEK_SET);
}

int32_t FileOperation::current_pos()
{
  int fd = check_file();
  if (fd < 0)
    return fd;

  return lseek(fd, 0, SEEK_CUR);
}

int FileOperation::flush_file()
{
  if (open_flags_ & O_SYNC)
  {
    return 0;
  }

  int fd = check_file();
  if (fd < 0)
    return fd;

  return fsync(fd);
}

int FileOperation::flush_data()
{
  if (open_flags_ & O_SYNC)
  {
    return 0;
  }

  int fd = check_file();
  if (fd < 0)
    return fd;

  return fdatasync(fd);
}

int FileOperation::unlink_file()
{
  close_file();
  return ::unlink(file_name_);
}

int FileOperation::rename_file(const char *new_name)
{
  int ret = -1;
  if (NULL != new_name || new_name[0] != '\0')
  {
    uint64_t new_name_len = strlen(new_name);
    if (strlen(file_name_) != new_name_len || memcmp(new_name, file_name_, new_name_len) != 0)
    {
      if (fd_ > 0)
      {
        fsync(fd_);
        ::close(fd_);
        fd_ = -1;
      }

      if (::rename(file_name_, new_name) == 0)
      {
        ret = 0;
        free(file_name_);
        file_name_ = strdup(new_name);
      }
    }
  }
  return ret;
}

int FileOperation::open_file()
{
  if (fd_ > 0)
  {
    close(fd_);
    fd_ = -1;
  }

  fd_ = ::open(file_name_, open_flags_, OPEN_MODE);
  if (fd_ < 0)
  {
    return -errno;
  }
  return fd_;
}

int FileOperation::check_file()
{
  if (fd_ < 0)
  {
    fd_ = open_file();
  }

  return fd_;
}

void FileMapper::close_file()
{
  if (data)
  {
    munmap(data, size);
    close(fd);
    data = NULL;
    size = 0;
    fd = -1;
  }
}

void FileMapper::sync_file()
{
  if (data != NULL && size > 0)
  {
    msync(data, size, MS_ASYNC);
  }
}

// createLength == 0 means read only
bool FileMapper::open_file(const char *file_name, uint64_t create_length)
{
  int flags = PROT_READ;
  if (create_length > 0)
  {
    fd = open(file_name, O_RDWR | O_LARGEFILE | O_CREAT, 0644);
    flags = PROT_READ | PROT_WRITE;
  }
  else
  {
    fd = open(file_name, O_RDONLY | O_LARGEFILE);
  }

  if (fd < 0)
  {
    //log_error("open file : %s failed, errno: %d", file_name, errno);
    return false;
  }

  if (create_length > 0)
  {
    if (ftruncate(fd, create_length) != 0)
    {
      //log_error("ftruncate file: %s failed", file_name);
      close(fd);
      fd = -1;
      return false;
    }
    size = create_length;
  }
  else
  {
    struct stat stbuff;
    fstat(fd, &stbuff);
    size = stbuff.st_size;
  }

  data = mmap(0, size, flags, MAP_SHARED, fd, 0);
  if (data == MAP_FAILED)
  {
    //log_error("map file: %s failed ,err is %s(%d)", file_name, strerror(errno), errno);
    close(fd);
    fd = -1;
    data = NULL;
    return false;
  }
  return true;
}

FileMapperOperation::FileMapperOperation()
{
  file_name = NULL;
  is_mapped = false;
  map_file = NULL;
  fd = -1;
}

FileMapperOperation::~FileMapperOperation()
{
  if (map_file != NULL)
  {
    delete map_file;
    map_file = NULL;
  }
  close();
  if (file_name != NULL)
  {
    free(file_name);
    file_name = NULL;
  }
}

bool FileMapperOperation::open(char *file_nname, int flag, int mode)
{
  //assert(m_fd < 0);
  if (file_name != NULL)
  {
    free(file_name);
    file_name = NULL;
  }

  file_name = strdup(file_nname);

  fd = ::open(file_name, flag, mode);
  if (fd < 0)
  {
    //log_error("open file [%s] failed: %s", file_name, strerror(errno));
    return false;
  }

  return true;
}

bool FileMapperOperation::close()
{
  if (!is_opened())
  {
    //log_info("file [%s] not opened, need not close", file_name);
    return true;
  }

  if (::close(fd) == -1)
  {
    //log_error("close file [%s] failed: %s", file_name, strerror(errno));
    return false;
  }

  return true;
}

uint64_t FileMapperOperation::get_size()
{
  uint64_t size = -1;
  struct stat s;
  if (fstat(fd, &s) == 0)
    size = s.st_size;
  return size;
}

bool FileMapperOperation::lock(int64_t offset, uint64_t size, bool write)
{
  if (!is_opened())
    return false;

  bool rc = false;
  struct flock lock;
  memset(&lock, 0, sizeof(lock));

  lock.l_start = offset;
  lock.l_len = size;
  lock.l_pid = 0;
  lock.l_whence = SEEK_SET;
  lock.l_type = write ? F_WRLCK : F_RDLCK;

  rc = (fcntl(fd, F_SETLK, &lock) != -1);

  return rc;
}

bool FileMapperOperation::unlock(int64_t offset, uint64_t length)
{
  if (!is_opened())
    return false;

  bool rc = false;
  struct flock lock;
  memset(&lock, 0, sizeof(lock));

  lock.l_start = offset;
  lock.l_len = length;
  lock.l_pid = 0;
  lock.l_whence = SEEK_SET;
  lock.l_type = F_UNLCK;

  rc = (fcntl(fd, F_SETLK, &lock) != -1);

  return rc;
}

bool FileMapperOperation::pread(void *buffer, uint64_t size, int64_t offset)
{
  if (!is_opened())
    return false;

  if (is_mapped && (offset + size) > map_file->get_size())
    map_file->remap();

  if (is_mapped && (offset + size) <= map_file->get_size())
  {
    // use mmap first
    //log_debug("read data from mmap[%s], offset [%lu], size [%lu]",
    //          file_name, offset, size);
    memcpy(buffer, (char *)map_file->get_data() + offset, size);
    return true;
  }

  //log_debug("read from [%s], offset: [%lu], size: [%lu]", file_name,
  //          offset, size);
  return ::pread(fd, buffer, size, offset) == (int64_t)size;
}

bool FileMapperOperation::sync(void)
{
  if (!is_opened())
    return false;

  if (is_mapped)
  {
    return map_file->sync_file();
  }
  else
  {
    return fsync(fd) == 0;
  }
}

int64_t FileMapperOperation::read(void *buffer, uint64_t size, int64_t offset)
{
  if (!is_opened())
    return false;
  if (is_mapped && (offset + size) > map_file->get_size())
    map_file->remap();

  if (is_mapped && (offset + size) <= map_file->get_size())
  {
    // use mmap first
    //log_debug("read data from mmap[%s], offset [%lu], size [%lu]",
    //          file_name, offset, size);
    memcpy(buffer, (char *)map_file->get_data() + offset, size);
    return size;
  }
  //log_debug("read from [%s], offset: [%lu], size: [%lu]", file_name,
  //          offset, size);
  return ::pread(fd, buffer, size, offset);
}

bool FileMapperOperation::write(void *buffer, uint64_t size)
{
  if (!is_opened())
    return false;

  //log_debug("write data into with size of [%lu] at offset [%lu]", size,
  //          get_position());

  int64_t offset = get_position();
  if (is_mapped && (offset + size) > map_file->get_size())
  {
    map_file->remap();
  }

  if (is_mapped && (offset + size) <= map_file->get_size())
  {
    //log_debug("write data use mmap at offset [%lu] with size [%lu]",
    //          offset, size);
    memcpy((char *)map_file->get_data() + offset, buffer, size);
    return true;
  }

  return ::write(fd, buffer, size) == (int64_t)size;
}

bool FileMapperOperation::pwrite(void *buffer, uint64_t size, int64_t offset)
{
  if (!is_opened())
    return false;

  if (offset < 0)
    offset = get_position();

  //log_debug("sizeof(int64_t): %lu, write[%s]: size [%lu] at offset [%lu]",
  //          sizeof(int64_t), file_name, size, offset);

  if (is_mapped && (offset + size) > map_file->get_size())
  {
    map_file->remap();
  }

  if (is_mapped && (offset + size) <= map_file->get_size())
  {
    // use mmap first
    //log_debug("pwrite data use mmap at offset [%lu] with size [%lu]",
    //          offset, size);
    memcpy((char *)map_file->get_data() + offset, buffer, size);
    return true;
  }

  return ::pwrite(fd, buffer, size, offset) == (int64_t)size;
}

bool FileMapperOperation::rename(char *new_name)
{
  if (::rename(file_name, new_name) == 0)
  {
    //log_warn("filename renamed from [%s] to [%s]", file_name, new_name);
    return true;
  }
  return false;
}

bool FileMapperOperation::remove()
{
  close();
  if (::remove(file_name) == 0)
  {
    //log_warn("remove file [%s]", file_name);
    return true;
  }
  return false;
}

bool FileMapperOperation::append_name(char *app_str)
{
  char new_name[256];
  snprintf(new_name, 256, "%s.%s", file_name, app_str);
  new_name[255] = '\0';
  return rename(new_name);
}

bool FileMapperOperation::mmap(uint64_t map_size)
{
  if (map_size == 0)
    return true;

  if (!is_opened())
  {
    //log_warn("file not opened");
    return false;
  }

  if (!is_mapped)
  {
    // do map if not mapped yet
    map_file = new MmapFile(map_size, fd);
    is_mapped = map_file->map_file(true);
  }

  return is_mapped;
}

void *FileMapperOperation::get_map_data()
{
  if (is_mapped)
    return map_file->get_data();

  return NULL;
}

bool FileMapperOperation::truncate(int64_t size)
{
  return ::ftruncate(fd, size) == 0;
}

////////////////////// FileWatcher //////////////////////

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

RollFile::RollFile() : m_path("./log"), m_name("roll.log")
{
  m_last_error[0] = 0;
  m_file_size = 10 * 1024 * 1024;
  m_roll_num = 10;
  m_file = NULL;
}

RollFile::RollFile(const string &path, const string &name)
    : m_path(path), m_name(name)
{
  m_last_error[0] = 0;
  m_file_size = 10 * 1024 * 1024;
  m_roll_num = 10;
  m_file = NULL;
}

RollFile::~RollFile()
{
  if (m_file)
  {
    fclose(m_file);
  }
}

FILE *RollFile::GetFile()
{
  if (m_file != NULL)
  {
    long size = ftell(m_file);
    // ftell fail, restart roll
    if (size < 0 || size > m_file_size)
    {
      Roll();
    }
  }
  else
  {
    if (make_dir_p(m_path) != 0)
    {
      //_PEBBLE_LOG_LAST_ERROR("mkdir %s failed %s", m_path.c_str(), DirUtil::GetLastError());
      return NULL;
    }

    string filename(m_path);
    filename.append("/");
    filename.append(m_name);
    m_file = fopen(filename.c_str(), "a+");
  }
  return m_file;
}

int32_t RollFile::SetFileName(const string &name)
{
  if (name.empty())
  {
    //_PEBBLE_LOG_LAST_ERROR("name is empty");
    return -1;
  }
  m_name = name;
  return 0;
}

int32_t RollFile::SetFilePath(const string &path)
{
  if (path.empty())
  {
    //_PEBBLE_LOG_LAST_ERROR("path is empty");
    return -1;
  }

  int32_t ret = make_dir_p(path);
  if (ret != 0)
  {
    //_PEBBLE_LOG_LAST_ERROR("mkdir %s failed %s", m_path.c_str(), DirUtil::GetLastError());
    return -1;
  }

  m_path.assign(path);
  return 0;
}

int32_t RollFile::SetFileSize(uint32_t file_size)
{
  if (0 == file_size)
  {
    //_PEBBLE_LOG_LAST_ERROR("file size = 0");
    return -1;
  }
  m_file_size = file_size;
  return 0;
}

int32_t RollFile::SetRollNum(uint32_t roll_num)
{
  if (0 == roll_num)
  {
    //_PEBBLE_LOG_LAST_ERROR("roll num = 0");
    return -1;
  }
  m_roll_num = roll_num;
  return 0;
}

void RollFile::Roll()
{
  if (m_file != NULL)
  {
    fclose(m_file);
  }

  uint32_t pos = 0;
  struct stat file_info;
  int64_t min_timestamp = INT64_MAX;
  for (uint32_t i = 1; i < m_roll_num; i++)
  {
    std::ostringstream oss;
    oss << m_path << "/" << m_name << "." << i;

    if (stat(oss.str().c_str(), &file_info) == 0)
    {
      if (file_info.st_mtime < min_timestamp)
      {
        min_timestamp = file_info.st_mtime;
        pos = i;
      }
    }
    else
    {
      pos = i;
      break;
    }
  }

  std::ostringstream oss;
  oss << m_path << "/" << m_name;
  string f0(oss.str());
  if (pos > 0)
  {
    oss << "." << pos;
    string fn(oss.str());
    remove(fn.c_str());             // rm fn
    rename(f0.c_str(), fn.c_str()); // mv f0 fn
  }
  m_file = fopen(f0.c_str(), "w+");
}

void RollFile::Close()
{
  if (m_file)
  {
    fclose(m_file);
    m_file = NULL;
  }
}

void RollFile::Flush()
{
  if (m_file)
  {
    fflush(m_file);
  }
}

ReadSmallFile::ReadSmallFile(const string &filename)
    : fd_(::open(filename.c_str(), O_RDONLY | O_CLOEXEC)),
      err_(0)
{
  buf_[0] = '\0';
  if (fd_ < 0)
  {
    err_ = errno;
  }
}

ReadSmallFile::~ReadSmallFile()
{
  if (fd_ >= 0)
  {
    ::close(fd_); // FIXME: check EINTR
  }
}

// return errno
int ReadSmallFile::readToString(int32_t maxSize,
                                string *content,
                                int64_t *fileSize,
                                int64_t *modifyTime,
                                int64_t *createTime)
{
  static_assert(sizeof(off_t) == 8, "");
  assert(content != NULL);
  int err = err_;
  if (fd_ >= 0)
  {
    content->clear();

    if (fileSize)
    {
      struct stat statbuf;
      if (::fstat(fd_, &statbuf) == 0)
      {
        if (S_ISREG(statbuf.st_mode))
        {
          *fileSize = statbuf.st_size;
          content->reserve(static_cast<int32_t>(MATH_MIN((int64_t)(maxSize), *fileSize)));
        }
        else if (S_ISDIR(statbuf.st_mode))
        {
          err = EISDIR;
        }
        if (modifyTime)
        {
          *modifyTime = statbuf.st_mtime;
        }
        if (createTime)
        {
          *createTime = statbuf.st_ctime;
        }
      }
      else
      {
        err = errno;
      }
    }

    while (content->size() < (uint64_t)(maxSize))
    {
      uint64_t toRead = MATH_MIN((uint64_t)(maxSize)-content->size(), sizeof(buf_));
      ssize_t n = ::read(fd_, buf_, toRead);
      if (n > 0)
      {
        content->append(buf_, n);
      }
      else
      {
        if (n < 0)
        {
          err = errno;
        }
        break;
      }
    }
  }
  return err;
}

int ReadSmallFile::readToBuffer(int32_t *size)
{
  int err = err_;
  if (fd_ >= 0)
  {
    ssize_t n = ::pread(fd_, buf_, sizeof(buf_) - 1, 0);
    if (n >= 0)
    {
      if (size)
      {
        *size = static_cast<int32_t>(n);
      }
      buf_[n] = '\0';
    }
    else
    {
      err = errno;
    }
  }
  return err;
}

} // namespace util
} // namespace mycc
