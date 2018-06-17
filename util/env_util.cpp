
#include "env_util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "io_util.h"
#include "math_util.h"
#include "random_util.h"
#include "string_util.h"
#include "thread_util.h"
#include "threadpool_util.h"

namespace mycc
{
namespace util
{

namespace
{ // anonymous namesapce

struct StartThreadState
{
  void (*user_function)(void *);
  void *arg;
};

Status IOError(const string &context, int32_t err_number)
{
  return Status::IOError(context, strerror(err_number));
}

// file_name can be left empty if it is not unkown.
Status IOError(const string &context, const string &file_name,
               int32_t err_number)
{
  return Status::IOError(context + ": " + file_name, strerror(err_number));
}

void PthreadCall(const char *label, int32_t result)
{
  if (result != 0)
  {
    fprintf(stderr, "pthread %s: %s\n", label, strerror(result));
    abort();
  }
}

void SetFD_CLOEXEC(int32_t fd, const EnvOptions *options)
{
  if ((options == nullptr || options->set_fd_cloexec) && fd > 0)
  {
    ::fcntl(fd, F_SETFD, ::fcntl(fd, F_GETFD) | FD_CLOEXEC);
  }
}

uint64_t Gettid(pthread_t tid)
{
  uint64_t thread_id = 0;
  memcpy(&thread_id, &tid, MATH_MIN(sizeof(thread_id), sizeof(tid)));
  return thread_id;
}

void *StartThreadWrapper(void *arg)
{
  StartThreadState *state = reinterpret_cast<StartThreadState *>(arg);
  state->user_function(state->arg);
  delete state;
  return nullptr;
}

Status DoCreatePath(const char *path, mode_t mode)
{
  struct stat st;
  Status result;

  if (::stat(path, &st) != 0)
  {
    // Directory does not exist. EEXIST for race condition
    if (::mkdir(path, mode) != 0 && errno != EEXIST)
      result = IOError(path, errno);
  }
  else if (!S_ISDIR(st.st_mode))
  {
    errno = ENOTDIR;
    result = IOError(path, errno);
  }
  return result;
}

int32_t LockOrUnlock(int32_t fd, bool lock)
{
  errno = 0;
  struct flock f;
  memset(&f, 0, sizeof(f));
  f.l_type = (lock ? F_WRLCK : F_UNLCK);
  f.l_whence = SEEK_SET;
  f.l_start = 0;
  f.l_len = 0; // Lock/unlock entire file
  return ::fcntl(fd, F_SETLK, &f);
}

} // namespace

uint64_t Env::NowMicros()
{
  struct timeval tv;
  ::gettimeofday(&tv, nullptr);
  return static_cast<uint64_t>(tv.tv_sec) * 1000000 + tv.tv_usec;
}

uint64_t Env::NowNanos()
{
  struct timespec ts;
  ::clock_gettime(CLOCK_MONOTONIC, &ts); // for linux
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000 + ts.tv_nsec;
}

uint64_t Env::NowChronoNanos()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

void Env::SleepForMicros(int32_t micros)
{
  ::usleep(micros);
}

Status Env::GetCurrentTimeEpoch(int64_t *unix_time)
{
  time_t ret = ::time(nullptr);
  if (ret == (time_t)-1)
  {
    return IOError("GetCurrentTimeEpoch", "", errno);
  }
  *unix_time = (int64_t)ret;
  return Status::OK();
}

string Env::TimeToString(uint64_t secondsSince1970)
{
  const time_t seconds = (time_t)secondsSince1970;
  struct tm t;
  int32_t maxsize = 64;
  string dummy;
  dummy.reserve(maxsize);
  dummy.resize(maxsize);
  char *p = &dummy[0];
  ::localtime_r(&seconds, &t);
  ::snprintf(p, maxsize,
             "%04d/%02d/%02d-%02d:%02d:%02d ",
             t.tm_year + 1900,
             t.tm_mon + 1,
             t.tm_mday,
             t.tm_hour,
             t.tm_min,
             t.tm_sec);
  return dummy;
}

/// path

bool Env::IsAbsolutePath(StringPiece path)
{
  return !path.empty() && path[0] == '/';
}

StringPiece Env::Dirname(StringPiece path)
{
  return SplitPath(path).first;
}

StringPiece Env::Basename(StringPiece path)
{
  return SplitPath(path).second;
}

StringPiece Env::PathExtension(StringPiece path)
{
  return SplitBasename(path).second;
}

string Env::CleanPath(StringPiece unclean_path)
{
  string path = unclean_path.toString();
  const char *src = path.c_str();
  string::iterator dst = path.begin();

  // Check for absolute path and determine initial backtrack limit.
  const bool is_absolute_path = *src == '/';
  if (is_absolute_path)
  {
    *dst++ = *src++;
    while (*src == '/')
      ++src;
  }
  string::const_iterator backtrack_limit = dst;

  // Process all parts
  while (*src)
  {
    bool parsed = false;

    if (src[0] == '.')
    {
      //  1dot ".<whateverisnext>", check for END or SEP.
      if (src[1] == '/' || !src[1])
      {
        if (*++src)
        {
          ++src;
        }
        parsed = true;
      }
      else if (src[1] == '.' && (src[2] == '/' || !src[2]))
      {
        // 2dot END or SEP (".." | "../<whateverisnext>").
        src += 2;
        if (dst != backtrack_limit)
        {
          // We can backtrack the previous part
          for (--dst; dst != backtrack_limit && dst[-1] != '/'; --dst)
          {
            // Empty.
          }
        }
        else if (!is_absolute_path)
        {
          // Failed to backtrack and we can't skip it either. Rewind and copy.
          src -= 2;
          *dst++ = *src++;
          *dst++ = *src++;
          if (*src)
          {
            *dst++ = *src;
          }
          // We can never backtrack over a copied "../" part so set new limit.
          backtrack_limit = dst;
        }
        if (*src)
        {
          ++src;
        }
        parsed = true;
      }
    }

    // If not parsed, copy entire part until the next SEP or EOS.
    if (!parsed)
    {
      while (*src && *src != '/')
      {
        *dst++ = *src++;
      }
      if (*src)
      {
        *dst++ = *src++;
      }
    }

    // Skip consecutive SEP occurrences
    while (*src == '/')
    {
      ++src;
    }
  }

  // Calculate and check the length of the cleaned path.
  string::difference_type path_length = dst - path.begin();
  if (path_length != 0)
  {
    // Remove trailing '/' except if it is root path ("/" ==> path_length := 1)
    if (path_length > 1 && path[path_length - 1] == '/')
    {
      --path_length;
    }
    path.resize(path_length);
  }
  else
  {
    // The cleaned path is empty; assign "." as per the spec.
    path.assign(1, '.');
  }
  return path;
}

string Env::StripBasename(const string &full_path)
{
  const char kSeparator = '/';
  uint64_t pos = full_path.rfind(kSeparator);
  if (pos != string::npos)
  {
    return full_path.substr(pos + 1, string::npos);
  }
  else
  {
    return full_path;
  }
}

bool Env::SplitPath(const string &path,
                    std::vector<string> *element,
                    bool *isdir)
{
  if (path.empty() || path[0] != '/' || path.size() > Env::kMaxPathLength)
  {
    return false;
  }
  element->clear();
  uint64_t last_pos = 0;
  for (uint64_t i = 1; i <= path.size(); i++)
  {
    if (i == path.size() || path[i] == '/')
    {
      if (last_pos + 1 < i)
      {
        element->push_back(path.substr(last_pos + 1, i - last_pos - 1));
      }
      last_pos = i;
    }
  }
  if (isdir)
  {
    *isdir = (path[path.size() - 1] == '/');
  }
  return true;
}

// Return the parts of the basename of path, split on the final ".".
// If there is no "." in the basename or "." is the final character in the
// basename, the second value will be empty.
std::pair<StringPiece, StringPiece> Env::SplitBasename(StringPiece path)
{
  path = Basename(path);

  uint64_t pos = path.rfind('.');
  if (pos == StringPiece::npos)
    return std::make_pair(path, StringPiece(path.data() + path.size(), 0));
  return std::make_pair(
      StringPiece(path.data(), pos),
      StringPiece(path.data() + pos + 1, path.size() - (pos + 1)));
}

// If there is no "/" in the path, the first part of the output is the scheme and host, and
// the second is the path. If the only "/" in the path is the first character,
// it is included in the first part of the output.
std::pair<StringPiece, StringPiece> Env::SplitPath(StringPiece path)
{
  uint64_t pos = path.rfind('/'); // for unix
  // Handle the case with no '/' in 'path'.
  if (pos == StringPiece::npos)
    return std::make_pair(StringPiece(), path);

  // Handle the case with a single leading '/' in 'path'.
  if (pos == 0)
    return std::make_pair(
        StringPiece("/"),
        StringPiece(path.data() + 1, path.size() - 1));

  return std::make_pair(
      StringPiece(path.begin(), path.begin() + pos - path.begin()),
      StringPiece(path.data() + pos + 1, path.size() - (pos + 1)));
}

string Env::JoinInitPath(std::initializer_list<StringPiece> paths)
{
  string result;

  for (StringPiece path : paths)
  {
    if (path.empty())
      continue;

    if (result.empty())
    {
      result = path.toString();
      continue;
    }

    if (result[result.size() - 1] == '/')
    {
      if (IsAbsolutePath(path))
      {
        StringAppendPieces(&result, path.substr(1));
      }
      else
      {
        StringAppendPieces(&result, path);
      }
    }
    else
    {
      if (IsAbsolutePath(path))
      {
        StringAppendPieces(&result, path);
      }
      else
      {
        StringAppendPieces(&result, "/", path);
      }
    }
  }

  return result;
}

Status Env::NewSequentialFile(const string &fname,
                              std::unique_ptr<SequentialFile> *result,
                              const EnvOptions &options)
{
  result->reset();
  int32_t fd = -1;
  int32_t flags = O_RDONLY;
  FILE *file = nullptr;

  if (options.use_direct_reads && !options.use_mmap_reads)
  {
    flags |= O_DIRECT;
  }

  do
  {
    fd = ::open(fname.c_str(), flags, 0644);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0)
  {
    return IOError("While opening a file for sequentially reading", fname,
                   errno);
  }

  SetFD_CLOEXEC(fd, &options);

  if (options.use_direct_reads && !options.use_mmap_reads)
  {
    // MACOSX fcntl(fd, F_NOCACHE, 1);
  }
  else
  {
    do
    {
      file = fdopen(fd, "r");
    } while (file == nullptr && errno == EINTR);
    if (file == nullptr)
    {
      ::close(fd);
      return IOError("While opening file for sequentially read", fname,
                     errno);
    }
  }
  result->reset(new PosixSequentialFile(fname, file, fd, options));
  return Status::OK();
}

Status Env::NewRandomAccessFile(const string &fname,
                                std::unique_ptr<RandomAccessFile> *result,
                                const EnvOptions &options)
{
  result->reset();
  Status s;
  int32_t fd;
  int32_t flags = O_RDONLY;
  if (options.use_direct_reads && !options.use_mmap_reads)
  {
    flags |= O_DIRECT;
  }

  do
  {
    fd = ::open(fname.c_str(), flags, 0644);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0)
  {
    return IOError("While open a file for random read", fname, errno);
  }
  SetFD_CLOEXEC(fd, &options);

  if (options.use_mmap_reads && sizeof(void *) >= 8)
  {
    // Use of mmap for random reads has been removed because it
    // kills performance when storage is fast.
    // Use mmap when virtual address-space is plentiful.
    uint64_t size;
    s = GetFileSize(fname, &size);
    if (s.ok())
    {
      void *base = ::mmap(nullptr, size, PROT_READ, MAP_SHARED, fd, 0);
      if (base != MAP_FAILED)
      {
        result->reset(new PosixMmapReadableFile(fd, fname, base,
                                                size, options));
      }
      else
      {
        s = IOError("while mmap file for read", fname, errno);
        ::close(fd);
      }
    }
  }
  else
  {
    if (options.use_direct_reads && !options.use_mmap_reads)
    {
      // MACOSX fcntl(fd, F_NOCACHE, 1);
    }
    result->reset(new PosixRandomAccessFile(fname, fd, options));
  }
  return s;
}

Status Env::OpenWritableFile(const string &fname,
                             std::unique_ptr<WritableFile> *result,
                             const EnvOptions &options,
                             bool reopen)
{
  result->reset();
  Status s;
  int32_t fd = -1;
  int32_t flags = (reopen) ? (O_CREAT | O_APPEND) : (O_CREAT | O_TRUNC);
  // Direct IO mode with O_DIRECT flag or F_NOCAHCE (MAC OSX)
  if (options.use_direct_writes && !options.use_mmap_writes)
  {
    // Note: we should avoid O_APPEND here due to ta the following bug:
    // POSIX requires that opening a file with the O_APPEND flag should
    // have no affect on the location at which pwrite() writes data.
    // However, on Linux, if a file is opened with O_APPEND, pwrite()
    // appends data to the end of the file, regardless of the value of
    // offset.
    // More info here: https://linux.die.net/man/2/pwrite
    flags |= O_WRONLY;
    flags |= O_DIRECT;
  }
  else if (options.use_mmap_writes)
  {
    // non-direct I/O
    flags |= O_RDWR;
  }
  else
  {
    flags |= O_WRONLY;
  }

  do
  {
    fd = ::open(fname.c_str(), flags, 0644);
  } while (fd < 0 && errno == EINTR);

  if (fd < 0)
  {
    s = IOError("While open a file for appending", fname, errno);
    return s;
  }
  SetFD_CLOEXEC(fd, &options);

  uint64_t page_size = ::getpagesize();
  if (options.use_mmap_writes)
  {
    result->reset(new PosixMmapFile(fname, fd, page_size, options));
  }
  else if (options.use_direct_writes && !options.use_mmap_writes)
  {
    result->reset(new PosixWritableFile(fname, fd, options));
  }
  else
  {
    // disable mmap writes
    EnvOptions no_mmap_writes_options = options;
    no_mmap_writes_options.use_mmap_writes = false;
    result->reset(new PosixWritableFile(fname, fd, no_mmap_writes_options));
  }
  return s;
}

Status Env::ReopenWritableFile(const string &fname,
                               std::unique_ptr<WritableFile> *result,
                               const EnvOptions &options)
{
  return OpenWritableFile(fname, result, options, true);
}

Status Env::NewWritableFile(const string &fname,
                            std::unique_ptr<WritableFile> *result,
                            const EnvOptions &options)
{
  return OpenWritableFile(fname, result, options, false);
}

Status Env::NewRandomRWFile(const string &fname,
                            std::unique_ptr<RandomRWFile> *result,
                            const EnvOptions &options)
{
  int32_t fd = -1;
  while (fd < 0)
  {
    fd = ::open(fname.c_str(), O_RDWR, 0644);
    if (fd < 0)
    {
      // Error while opening the file
      if (errno == EINTR)
      {
        continue;
      }
      return IOError("While open file for random read/write", fname, errno);
    }
  }

  SetFD_CLOEXEC(fd, &options);
  result->reset(new PosixRandomRWFile(fname, fd, options));
  return Status::OK();
}

Status Env::NewMemoryMappedFileBuffer(
    const string &fname,
    std::unique_ptr<MemoryMappedFileBuffer> *result)
{
  int32_t fd = -1;
  Status status;
  while (fd < 0)
  {
    fd = ::open(fname.c_str(), O_RDWR, 0644);
    if (fd < 0)
    {
      // Error while opening the file
      if (errno == EINTR)
      {
        continue;
      }
      status =
          IOError("While open file for raw mmap buffer access", fname, errno);
      break;
    }
  }
  uint64_t size;
  if (status.ok())
  {
    status = GetFileSize(fname, &size);
  }
  void *base = nullptr;
  if (status.ok())
  {
    base = ::mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE,
                  MAP_SHARED, fd, 0);
    if (base == MAP_FAILED)
    {
      status = IOError("while mmap file for read", fname, errno);
    }
  }
  if (status.ok())
  {
    result->reset(
        new PosixMemoryMappedFileBuffer(base, static_cast<size_t>(size)));
  }
  if (fd >= 0)
  {
    // don't need to keep it open after mmap has been called
    ::close(fd);
  }
  return status;
}

Status Env::NewDirectory(const string &name,
                         std::unique_ptr<Directory> *result)
{
  result->reset();
  int32_t fd;
  {
    fd = open(name.c_str(), 0);
  }
  if (fd < 0)
  {
    return IOError("While open directory", name, errno);
  }
  else
  {
    result->reset(new PosixDirectory(fd));
  }
  return Status::OK();
}

Status Env::FileExists(const string &fname)
{
  int32_t result = ::access(fname.c_str(), F_OK);
  if (result == 0)
  {
    return Status::OK();
  }
  return IOError("accessing file " + fname + " error, " + ToString(result), errno);
}

bool Env::IsDirectory(const string &dname)
{
  struct stat statbuf;
  if (::stat(dname.c_str(), &statbuf) == 0)
  {
    return S_ISDIR(statbuf.st_mode);
  }
  return false; // stat() failed return false
}

Status Env::Stat(const string &fname, FileStatistics *stats)
{
  Status s;
  struct stat sbuf;
  if (::stat(fname.c_str(), &sbuf) != 0)
  {
    s = IOError("while stat a file ", fname, errno);
  }
  else
  {
    stats->mode = sbuf.st_mode;
    stats->uid = sbuf.st_uid;
    stats->gid = sbuf.st_gid;
    stats->length = sbuf.st_size;
    stats->mtime_nsec = sbuf.st_mtime * 1e9;
    stats->is_directory = S_ISDIR(sbuf.st_mode);
  }
  return s;
}

Status Env::GetFileSize(const string &fname, uint64_t *size)
{
  Status s;
  struct stat sbuf;
  if (::stat(fname.c_str(), &sbuf) != 0)
  {
    *size = 0;
    s = IOError("while stat a file for size", fname, errno);
  }
  else
  {
    *size = sbuf.st_size;
  }
  return s;
}

Status Env::GetFileModificationTime(const string &fname,
                                    uint64_t *file_mtime)
{
  struct stat s;
  if (::stat(fname.c_str(), &s) != 0)
  {
    return IOError("while stat a file for modification time", fname, errno);
  }
  *file_mtime = static_cast<uint64_t>(s.st_mtime);
  return Status::OK();
}

Status Env::GetDirChildren(const string &dir,
                           std::vector<string> *result)
{
  result->clear();
  DIR *d = ::opendir(dir.c_str());
  if (d == nullptr)
  {
    return IOError("While opendir", dir, errno);
  }
  struct dirent *entry;
  while ((entry = ::readdir(d)) != nullptr)
  {
    if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)
    {
      continue;
    }
    result->push_back(entry->d_name);
  }
  ::closedir(d);
  return Status::OK();
}

Status Env::GetDirChildrenRecursively(const string &dir, std::vector<string> *result)
{
  result->clear();
  DIR *d = ::opendir(dir.c_str());
  if (d == nullptr)
  {
    return IOError("opendir error ", dir, errno);
  }
  Status s;
  struct dirent *entry;
  string fname;
  while ((entry = ::readdir(d)) != NULL)
  {
    if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)
    {
      continue;
    }
    fname = dir + "/" + entry->d_name;
    if (IsDirectory(fname))
    {
      s = GetDirChildrenRecursively(fname, result);
      if (!s.ok())
      {
        return s;
      }
    }
    else
    {
      result->push_back(fname);
    }
  }
  ::closedir(d);
  return Status::OK();
}

Status Env::GetChildrenFileAttributes(const string &dir,
                                      std::vector<FileAttributes> *result)
{
  std::vector<string> child_fnames;
  Status s = GetDirChildren(dir, &child_fnames);
  if (!s.ok())
  {
    return s;
  }
  result->resize(child_fnames.size());
  uint64_t result_size = 0;
  for (uint64_t i = 0; i < child_fnames.size(); ++i)
  {
    const string path = dir + "/" + child_fnames[i];
    s = GetFileSize(path, &(*result)[result_size].size_bytes);
    if (!s.ok())
    {
      if (!FileExists(path).ok())
      {
        // The file may have been deleted since we listed the directory
        continue;
      }
      return s;
    }
    (*result)[result_size].name = std::move(child_fnames[i]);
    result_size++;
  }
  result->resize(result_size);
  return Status::OK();
}

bool Env::MatchPath(const string &path, const string &pattern)
{
  return ::fnmatch(pattern.c_str(), path.c_str(), FNM_PATHNAME) == 0;
}

Status Env::DeleteFile(const string &fname)
{
  Status result;
  if (::unlink(fname.c_str()) != 0)
  {
    result = IOError("while unlink() file", fname, errno);
  }
  return result;
};

Status Env::CreateDir(const string &name)
{
  Status result;
  if (::mkdir(name.c_str(), 0755) != 0)
  {
    result = IOError("while mkdir", name, errno);
  }
  return result;
};

Status Env::CreateDirIfMissing(const string &name)
{
  Status result;
  if (::mkdir(name.c_str(), 0755) != 0)
  {
    if (errno != EEXIST)
    {
      result = IOError("while mkdir if missing", name, errno);
    }
    else if (!IsDirectory(name))
    { // Check that name is actually a
      // directory.
      // Message is taken from mkdir
      result = IOError(name + " exists but is not a directory", errno);
    }
  }
  return result;
};

Status Env::CreateDirRecursively(const string &dirname)
{
  StringPiece remaining_dir(dirname);
  std::vector<StringPiece> sub_dirs;
  while (!remaining_dir.empty())
  {
    Status status = FileExists(remaining_dir.toString());
    if (status.ok())
    {
      break;
    }
    // Basename returns "" for / ending dirs.
    if (!remaining_dir.ends_with("/"))
    {
      sub_dirs.push_back(Basename(remaining_dir));
    }
    remaining_dir = Dirname(remaining_dir);
  }

  // sub_dirs contains all the dirs to be created but in reverse order.
  std::reverse(sub_dirs.begin(), sub_dirs.end());

  // Now create the directories.
  string built_path = remaining_dir.toString();
  for (const StringPiece sub_dir : sub_dirs)
  {
    built_path = JoinPath(built_path, sub_dir);
    Status status = CreateDir(built_path);
    if (!status.ok())
    {
      return status;
    }
  }
  return Status::OK();
}

// Algorithm takes the pessimistic view and works top-down to ensure
// each directory in path exists, rather than optimistically creating
// the last element and working backwards.
Status Env::CreatePath(const string &path)
{
  char *pp;
  char *sp;
  Status result;
  char *copypath = ::strdup(path.c_str());

  pp = copypath;
  while (result.ok() && (sp = ::strchr(pp, '/')) != 0)
  {
    if (sp != pp)
    {
      // Neither root nor double slash in path
      *sp = '\0';
      result = DoCreatePath(copypath, 0755);
      *sp = '/';
    }
    pp = sp + 1;
  }
  if (result.ok())
    result = DoCreatePath(path.c_str(), 0755);
  ::free(copypath);
  return result;
}

Status Env::DeleteDir(const string &name)
{
  Status result;
  if (::rmdir(name.c_str()) != 0)
  {
    result = IOError("file rmdir", name, errno);
  }
  return result;
};

Status Env::DeleteDirRecursively(const string &dirname,
                                 int64_t *undeleted_files,
                                 int64_t *undeleted_dirs)
{
  *undeleted_files = 0;
  *undeleted_dirs = 0;
  // Make sure that dirname exists;
  Status exists_status = FileExists(dirname);
  if (!exists_status.ok())
  {
    (*undeleted_dirs)++;
    return exists_status;
  }
  std::deque<string> dir_q;     // Queue for the BFS
  std::vector<string> dir_list; // List of all dirs discovered
  dir_q.push_back(dirname);
  Status ret; // Status to be returned.
  // Do a BFS on the directory to discover all the sub-directories. Remove all
  // children that are files along the way. Then cleanup and remove the
  // directories in reverse order.;
  while (!dir_q.empty())
  {
    string dir = dir_q.front();
    dir_q.pop_front();
    dir_list.push_back(dir);
    std::vector<string> children;
    // GetChildren might fail if we don't have appropriate permissions.
    Status s = GetDirChildren(dir, &children);
    ret.update(s);
    if (!s.ok())
    {
      (*undeleted_dirs)++;
      continue;
    }
    for (const string &child : children)
    {
      const string child_path = JoinPath(dir, child);
      // If the child is a directory add it to the queue, otherwise delete it.
      if (IsDirectory(child_path))
      {
        dir_q.push_back(child_path);
      }
      else
      {
        // Delete file might fail because of permissions issues or might be
        // unimplemented.
        Status del_status = DeleteFile(child_path);
        ret.update(del_status);
        if (!del_status.ok())
        {
          (*undeleted_files)++;
        }
      }
    }
  }
  // Now reverse the list of directories and delete them. The BFS ensures that
  // we can delete the directories in this order.
  std::reverse(dir_list.begin(), dir_list.end());
  for (const string &dir : dir_list)
  {
    // Delete dir might fail because of permissions issues or might be
    // unimplemented.
    Status s = DeleteDir(dir);
    ret.update(s);
    if (!s.ok())
    {
      (*undeleted_dirs)++;
    }
  }
  return ret;
}

Status Env::RenameFile(const string &src,
                       const string &target)
{
  Status result;
  if (::rename(src.c_str(), target.c_str()) != 0)
  {
    result = IOError("While renaming a file to " + target, src, errno);
  }
  return result;
}

Status Env::LinkFile(const string &src,
                     const string &target)
{
  Status result;
  if (::link(src.c_str(), target.c_str()) != 0)
  {
    if (errno == EXDEV)
    {
      return Status::NotSupported("No cross FS links allowed");
    }
    result = IOError("while link file to " + target, src, errno);
  }
  return result;
}

Status Env::AreFilesSame(const std::string &first,
                         const std::string &second, bool *res)
{
  struct stat statbuf[2];
  if (::stat(first.c_str(), &statbuf[0]) != 0)
  {
    return IOError("stat file", first, errno);
  }
  if (::stat(second.c_str(), &statbuf[1]) != 0)
  {
    return IOError("stat file", second, errno);
  }

  if (::major(statbuf[0].st_dev) != ::major(statbuf[1].st_dev) ||
      ::minor(statbuf[0].st_dev) != ::minor(statbuf[1].st_dev) ||
      statbuf[0].st_ino != statbuf[1].st_ino)
  {
    *res = false;
  }
  else
  {
    *res = true;
  }
  return Status::OK();
}

Status LockFile(const std::string &fname, FileLock **lock)
{
  *lock = NULL;
  Status result;
  int32_t fd = ::open(fname.c_str(), O_RDWR | O_CREAT, 0644);
  if (fd < 0)
  {
    result = IOError("open file ", fname, errno);
  }
  else if (LockOrUnlock(fd, true) == -1)
  {
    result = IOError("lock file" + fname, errno);
    close(fd);
  }
  else
  {
    FileLock *my_lock = new FileLock;
    my_lock->fd = fd;
    my_lock->name = fname;
    *lock = my_lock;
  }
  return result;
}

Status UnlockFile(FileLock *lock)
{
  Status result;
  if (LockOrUnlock(lock->fd, false) == -1)
  {
    result = IOError("unlock file", errno);
  }
  ::close(lock->fd);
  delete lock;
  return result;
}

uint64_t Env::Du(const string &filename)
{
  struct stat statbuf;
  uint64_t sum;
  if (::lstat(filename.c_str(), &statbuf) != 0)
  {
    return 0;
  }
  if (S_ISLNK(statbuf.st_mode) && ::stat(filename.c_str(), &statbuf) != 0)
  {
    return 0;
  }
  sum = statbuf.st_size;
  if (S_ISDIR(statbuf.st_mode))
  {
    DIR *dir = NULL;
    struct dirent *entry;
    string newfile;

    dir = ::opendir(filename.c_str());
    if (!dir)
    {
      return sum;
    }
    while ((entry = ::readdir(dir)))
    {
      if (strcmp(entry->d_name, "..") == 0 || strcmp(entry->d_name, ".") == 0)
      {
        continue;
      }
      newfile = filename + "/" + entry->d_name;
      sum += Du(newfile);
    }
    ::closedir(dir);
  }
  return sum;
}

Status Env::WriteStringToFile(const StringPiece &data, const string &fname,
                              bool should_sync)
{
  std::unique_ptr<WritableFile> file;
  EnvOptions soptions;
  Status s = Env::NewWritableFile(fname, &file, soptions);
  if (!s.ok())
  {
    return s;
  }
  s = file->append(data);
  if (s.ok() && should_sync)
  {
    s = file->sync();
  }
  if (!s.ok())
  {
    Env::DeleteFile(fname);
  }
  return s;
}

Status Env::ReadFileToString(const string &fname, string *data)
{
  EnvOptions soptions;
  data->clear();
  std::unique_ptr<SequentialFile> file;
  Status s = Env::NewSequentialFile(fname, &file, soptions);
  if (!s.ok())
  {
    return s;
  }
  static const int32_t kBufferSize = 8192;
  char *space = new char[kBufferSize];
  while (true)
  {
    StringPiece fragment;
    s = file->read(kBufferSize, &fragment, space);
    if (!s.ok())
    {
      break;
    }
    data->append(fragment.data(), fragment.size());
    if (fragment.empty())
    {
      break;
    }
  }
  delete[] space;
  return s;
}

string Env::GenerateUniqueId()
{
  string uuid_file = "/proc/sys/kernel/random/uuid";

  Status s = FileExists(uuid_file);
  if (s.ok())
  {
    std::string uuid;
    s = Env::ReadFileToString(uuid_file, &uuid);
    if (s.ok())
    {
      return uuid;
    }
  }
  // Could not read uuid_file - generate uuid using "nanos-random"
  Random64 r(time(nullptr));
  uint64_t random_uuid_portion =
      r.uniform(std::numeric_limits<uint64_t>::max());
  uint64_t nanos_uuid_portion = Env::NowNanos();
  char uuid2[200];
  snprintf(uuid2,
           200,
           "%lx-%lx",
           (unsigned long)nanos_uuid_portion,
           (unsigned long)random_uuid_portion);
  return uuid2;
}

string Env::PriorityToString(Env::Priority priority)
{
  switch (priority)
  {
  case Env::Priority::BOTTOM:
    return "Bottom";
  case Env::Priority::LOW:
    return "Low";
  case Env::Priority::HIGH:
    return "High";
  case Env::Priority::TOTAL:
    assert(false);
  }
  return "Invalid";
}

uint64_t Env::GetThreadID()
{
  return Gettid(pthread_self());
}

uint64_t Env::GetStdThreadId()
{
  std::hash<std::thread::id> hasher;
  return hasher(std::this_thread::get_id());
}

void Env::StartNewPthread(void (*function)(void *arg), void *arg)
{
  pthread_t t;
  StartThreadState *state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  PthreadCall("pthread_create",
              ::pthread_create(&t, nullptr, &StartThreadWrapper, state));
}

Thread *Env::StartNewThread(const ThreadOptions &thread_options, const string &name,
                            std::function<void()> fn)
{
  return new StdThread(thread_options, name, fn);
}

ThreadPool *Env::NewThreadPool(int32_t num_threads)
{
  ThreadPoolImpl *thread_pool = new ThreadPoolImpl();
  thread_pool->setBackgroundThreads(num_threads);
  return thread_pool;
}

/// PosixEnv

class PosixEnv : public Env
{
public:
  PosixEnv();

  virtual ~PosixEnv()
  {
    for (const auto tid : threads_to_join_)
    {
      ::pthread_join(tid, nullptr);
    }
    for (int32_t pool_id = 0; pool_id < Env::Priority::TOTAL; ++pool_id)
    {
      thread_pools_[pool_id].joinAllThreads();
    }
  }

  // Allow increasing the number of worker threads.
  virtual void SetBackgroundThreads(int32_t num, Priority pri) override
  {
    assert(pri >= Priority::BOTTOM && pri <= Priority::HIGH);
    thread_pools_[pri].setBackgroundThreads(num);
  }

  virtual int32_t GetBackgroundThreads(Priority pri) override
  {
    assert(pri >= Priority::BOTTOM && pri <= Priority::HIGH);
    return thread_pools_[pri].getBackgroundThreads();
  }

  // Allow increasing the number of worker threads.
  virtual void IncBackgroundThreadsIfNeeded(int32_t num, Priority pri) override
  {
    assert(pri >= Priority::BOTTOM && pri <= Priority::HIGH);
    thread_pools_[pri].incBackgroundThreadsIfNeeded(num);
  }

  virtual void LowerThreadPoolIOPriority(Priority pool = LOW) override
  {
    assert(pool >= Priority::BOTTOM && pool <= Priority::HIGH);
#ifdef OS_LINUX
    thread_pools_[pool].lowerIOPriority();
#else
    (void)pool;
#endif
  }

  virtual void LowerThreadPoolCPUPriority(Priority pool = LOW) override
  {
    assert(pool >= Priority::BOTTOM && pool <= Priority::HIGH);
#ifdef OS_LINUX
    thread_pools_[pool].lowerCPUPriority();
#else
    (void)pool;
#endif
  }

  virtual void Schedule(void (*function)(void *arg1), void *arg,
                        Priority pri = LOW, void *tag = nullptr,
                        void (*unschedFunction)(void *arg) = nullptr) override;

  virtual int32_t UnSchedule(void *arg, Priority pri) override;

  virtual void StartThread(void (*function)(void *arg), void *arg) override;

  virtual void WaitForJoin() override;

  virtual uint32_t GetThreadPoolQueueLen(Priority pri = LOW) const override;

private:
  std::vector<ThreadPoolImpl> thread_pools_;
  pthread_mutex_t mu_;
  std::vector<pthread_t> threads_to_join_;
};

PosixEnv::PosixEnv()
    : thread_pools_(Priority::TOTAL)
{
  ThreadPoolImpl::PthreadCall("mutex_init", pthread_mutex_init(&mu_, nullptr));
  for (int32_t pool_id = 0; pool_id < Env::Priority::TOTAL; ++pool_id)
  {
    thread_pools_[pool_id].setThreadPriority(
        static_cast<Env::Priority>(pool_id));
    // This allows later initializing the thread-local-env of each thread.
    thread_pools_[pool_id].setHostEnv(this);
  }
}

void PosixEnv::Schedule(void (*function)(void *arg1), void *arg, Priority pri,
                        void *tag, void (*unschedFunction)(void *arg))
{
  assert(pri >= Priority::BOTTOM && pri <= Priority::HIGH);
  thread_pools_[pri].schedule(function, arg, tag, unschedFunction);
}

int32_t PosixEnv::UnSchedule(void *arg, Priority pri)
{
  return thread_pools_[pri].unSchedule(arg);
}

void PosixEnv::StartThread(void (*function)(void *arg), void *arg)
{
  pthread_t t;
  StartThreadState *state = new StartThreadState;
  state->user_function = function;
  state->arg = arg;
  ThreadPoolImpl::PthreadCall(
      "start thread", pthread_create(&t, nullptr, &StartThreadWrapper, state));
  ThreadPoolImpl::PthreadCall("lock", pthread_mutex_lock(&mu_));
  threads_to_join_.push_back(t);
  ThreadPoolImpl::PthreadCall("unlock", pthread_mutex_unlock(&mu_));
}

void PosixEnv::WaitForJoin()
{
  for (const auto tid : threads_to_join_)
  {
    pthread_join(tid, nullptr);
  }
  threads_to_join_.clear();
}

uint32_t PosixEnv::GetThreadPoolQueueLen(Priority pri) const
{
  assert(pri >= Priority::BOTTOM && pri <= Priority::HIGH);
  return thread_pools_[pri].getQueueLen();
}

//
// Default Posix Env
//
static pthread_once_t once = PTHREAD_ONCE_INIT;
static Env *default_env = nullptr;
static void InitDefaultEnv() { default_env = new PosixEnv(); }

Env *Env::Default()
{
  pthread_once(&once, InitDefaultEnv);
  return default_env;
}

} // namespace util
} // namespace mycc