
#include "logging_util.h"
#include <assert.h>
#include <dirent.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syscall.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <algorithm>
#include <condition_variable>
#include <queue>
#include <set>
#include <string>
#include <vector>
#include "error_util.h"
#include "locks_util.h"
#include "thread_util.h"
#include "time_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

////////////////////// BD log ///////////////////////////

int32_t g_bd_log_level = INFO;
int64_t g_bd_log_size = 0;
int32_t g_bd_log_count = 0;
FILE *g_bd_log_file = stdout;
string g_bd_log_file_name;
FILE *g_bd_warning_file = NULL;
int64_t g_bd_total_size_limit = 0;
std::set<string> g_bd_log_set;
int64_t g_bd_current_total_size = 0;

bool BDGetNewLog(bool append)
{
  char buf[32];
  CurrentSystimeBuf(buf, sizeof(buf));
  string full_path(g_bd_log_file_name + ".");
  full_path.append(buf);
  uint64_t idx = full_path.rfind('/');
  if (idx == string::npos)
  {
    idx = 0;
  }
  else
  {
    idx += 1;
  }
  const char *mode = append ? "ab" : "wb";
  FILE *fp = fopen(full_path.c_str(), mode);
  if (fp == NULL)
  {
    return false;
  }
  if (g_bd_log_file != stdout)
  {
    fclose(g_bd_log_file);
  }
  g_bd_log_file = fp;
  remove(g_bd_log_file_name.c_str());
  symlink(full_path.substr(idx).c_str(), g_bd_log_file_name.c_str());
  g_bd_log_set.insert(full_path);
  while ((g_bd_log_count && static_cast<int64_t>(g_bd_log_set.size()) > g_bd_log_count) || (g_bd_total_size_limit && g_bd_current_total_size > g_bd_total_size_limit))
  {
    std::set<string>::iterator it = g_bd_log_set.begin();
    if (it != g_bd_log_set.end())
    {
      struct stat sta;
      if (-1 == lstat(it->c_str(), &sta))
      {
        return false;
      }
      remove(it->c_str());
      g_bd_current_total_size -= sta.st_size;
      g_bd_log_set.erase(it++);
    }
    else
    {
      break;
    }
  }
  return true;
}

void BDSetLogLevel(int32_t level)
{
  g_bd_log_level = level;
}

class BDAsyncLogger
{
public:
  BDAsyncLogger()
      : jobs_(&mu_), done_(&mu_), stopped_(false), size_(0),
        thread_(std::bind(&BDAsyncLogger::asyncWriter, this))
  {
    buffer_queue_ = new std::queue<std::pair<int32_t, string *>>;
    bg_queue_ = new std::queue<std::pair<int32_t, string *>>;
    thread_.start();
  }

  ~BDAsyncLogger()
  {
    stopped_ = true;
    {
      MutexLock lock(&mu_);
      jobs_.signal();
    }
    thread_.join();
    delete buffer_queue_;
    delete bg_queue_;
    // close fd
  }

  void writeLog(int32_t log_level, const char *buffer, int32_t len)
  {
    string *log_str = new string(buffer, len);
    MutexLock lock(&mu_);
    buffer_queue_->push(make_pair(log_level, log_str));
    jobs_.signal();
  }

  void asyncWriter()
  {
    int64_t loglen = 0;
    int64_t wflen = 0;
    while (1)
    {
      while (!bg_queue_->empty())
      {
        int log_level = bg_queue_->front().first;
        string *str = bg_queue_->front().second;
        bg_queue_->pop();
        if (g_bd_log_file != stdout && g_bd_log_size && str &&
            static_cast<int64_t>(size_ + str->length()) > g_bd_log_size)
        {
          g_bd_current_total_size += static_cast<int64_t>(size_ + str->length());
          BDGetNewLog(false);
          size_ = 0;
        }
        if (str && !str->empty())
        {
          uint64_t lret = ::fwrite(str->data(), 1, str->size(), g_bd_log_file);
          loglen += lret;
          if (g_bd_warning_file && log_level >= 8)
          {
            uint64_t wret = ::fwrite(str->data(), 1, str->size(), g_bd_warning_file);
            wflen += wret;
          }
          if (g_bd_log_size)
            size_ += lret;
        }
        delete str;
      }
      MutexLock lock(&mu_);
      if (!buffer_queue_->empty())
      {
        std::swap(buffer_queue_, bg_queue_);
        continue;
      }
      if (loglen)
        fflush(g_bd_log_file);
      if (wflen)
        fflush(g_bd_warning_file);
      done_.broadcast();
      if (stopped_)
      {
        break;
      }
      jobs_.wait();
      loglen = 0;
      wflen = 0;
    }
  }

  void flush()
  {
    MutexLock lock(&mu_);
    buffer_queue_->push(std::make_pair(0, reinterpret_cast<string *>(NULL)));
    jobs_.signal();
    done_.wait();
  }

private:
  Mutex mu_;
  CondVar jobs_;
  CondVar done_;
  bool stopped_;
  int64_t size_;
  PosixThread thread_;
  std::queue<std::pair<int32_t, string *>> *buffer_queue_;
  std::queue<std::pair<int32_t, string *>> *bg_queue_;
};

BDAsyncLogger g_logger;

bool BDSetWarningFile(const char *path, bool append)
{
  const char *mode = append ? "ab" : "wb";
  FILE *fp = fopen(path, mode);
  if (fp == NULL)
  {
    return false;
  }
  if (g_bd_warning_file)
  {
    fclose(g_bd_warning_file);
  }
  g_bd_warning_file = fp;
  return true;
}

bool BDRecoverHistory(const char *path)
{
  string log_path(path);
  uint64_t idx = log_path.rfind('/');
  string dir = "./";
  string log(path);
  if (idx != string::npos)
  {
    dir = log_path.substr(0, idx + 1);
    log = log_path.substr(idx + 1);
  }
  struct dirent *entry = NULL;
  DIR *dir_ptr = opendir(dir.c_str());
  if (dir_ptr == NULL)
  {
    return false;
  }
  std::vector<string> loglist;
  while ((entry = readdir(dir_ptr)) != NULL)
  {
    if (string(entry->d_name).find(log) != string::npos)
    {
      string file_name = dir + string(entry->d_name);
      struct stat sta;
      if (-1 == lstat(file_name.c_str(), &sta))
      {
        return false;
      }
      if (S_ISREG(sta.st_mode))
      {
        loglist.push_back(dir + string(entry->d_name));
        g_bd_current_total_size += sta.st_size;
      }
    }
  }
  closedir(dir_ptr);
  std::sort(loglist.begin(), loglist.end());
  for (std::vector<string>::iterator it = loglist.begin(); it != loglist.end();
       ++it)
  {
    g_bd_log_set.insert(*it);
  }
  while ((g_bd_log_count && static_cast<int64_t>(g_bd_log_set.size()) > g_bd_log_count) || (g_bd_total_size_limit && g_bd_current_total_size > g_bd_total_size_limit))
  {
    std::set<string>::iterator it = g_bd_log_set.begin();
    if (it != g_bd_log_set.end())
    {
      struct stat sta;
      if (-1 == lstat(it->c_str(), &sta))
      {
        return false;
      }
      remove(it->c_str());
      g_bd_current_total_size -= sta.st_size;
      g_bd_log_set.erase(it++);
    }
  }
  return true;
}

bool BDSetLogFile(const char *path, bool append)
{
  g_bd_log_file_name.assign(path);
  return BDGetNewLog(append);
}

bool BDSetLogSize(int32_t size)
{
  if (size < 0)
  {
    return false;
  }
  g_bd_log_size = static_cast<int64_t>(size) << 20;
  return true;
}

bool BDSetLogCount(int32_t count)
{
  if (count < 0 || g_bd_total_size_limit != 0)
  {
    return false;
  }
  g_bd_log_count = count;
  if (!BDRecoverHistory(g_bd_log_file_name.c_str()))
  {
    return false;
  }
  return true;
}

bool BDSetLogSizeLimit(int32_t size)
{
  if (size < 0 || g_bd_log_count != 0)
  {
    return false;
  }
  g_bd_total_size_limit = static_cast<int64_t>(size) << 20;
  if (!BDRecoverHistory(g_bd_log_file_name.c_str()))
  {
    return false;
  }
  return true;
}

void BDLogv(int32_t log_level, const char *format, va_list ap)
{
  static __thread uint64_t thread_id = 0;
  static __thread char tid_str[32];
  static __thread int tid_str_len = 0;
  if (thread_id == 0)
  {
    thread_id = syscall(__NR_gettid);
    tid_str_len = snprintf(tid_str, sizeof(tid_str), " %5d ", static_cast<int32_t>(thread_id));
  }

  static const char level_char[] = {
      'V', 'D', 'I', 'W', 'E', 'F'};
  char cur_level = level_char[0];
  if (log_level < DEBUG)
  {
    cur_level = level_char[0];
  }
  else if (log_level < INFO)
  {
    cur_level = level_char[1];
  }
  else if (log_level < WARNING)
  {
    cur_level = level_char[2];
  }
  else if (log_level < ERROR)
  {
    cur_level = level_char[3];
  }
  else if (log_level < FATAL)
  {
    cur_level = level_char[4];
  }
  else
  {
    cur_level = level_char[5];
  }

  // We try twice: the first time with a fixed-size stack allocated buffer,
  // and the second time with a much larger dynamically allocated buffer.
  char buffer[500];
  for (int32_t iter = 0; iter < 2; iter++)
  {
    char *base;
    int32_t bufsize;
    if (iter == 0)
    {
      bufsize = sizeof(buffer);
      base = buffer;
    }
    else
    {
      bufsize = 30000;
      base = new char[bufsize];
    }
    char *p = base;
    char *limit = base + bufsize;

    *p++ = cur_level;
    *p++ = ' ';
    int32_t rlen = CurrentSystimeBuf(p, limit - p);
    p += rlen;
    memcpy(p, tid_str, tid_str_len);
    p += tid_str_len;

    // Print the message
    if (p < limit)
    {
      va_list backup_ap;
      va_copy(backup_ap, ap);
      p += vsnprintf(p, limit - p, format, backup_ap);
      va_end(backup_ap);
    }

    // Truncate to available space if necessary
    if (p >= limit)
    {
      if (iter == 0)
      {
        continue; // Try again with larger buffer
      }
      else
      {
        p = limit - 1;
      }
    }

    // Add newline if necessary
    if (p == base || p[-1] != '\n')
    {
      *p++ = '\n';
    }

    assert(p <= limit);
    //fwrite(base, 1, p - base, g_bd_log_file);
    //fflush(g_bd_log_file);
    //if (g_bd_warning_file && log_level >= 8) {
    //    fwrite(base, 1, p - base, g_bd_warning_file);
    //    fflush(g_bd_warning_file);
    //}
    g_logger.writeLog(log_level, base, p - base);
    if (log_level >= ERROR)
    {
      g_logger.flush();
    }
    if (base != buffer)
    {
      delete[] base;
    }
    break;
  }
}

void BDLogC(int32_t level, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);

  if (level >= g_bd_log_level)
  {
    BDLogv(level, fmt, ap);
  }
  va_end(ap);
  if (level == FATAL)
  {
    abort();
  }
}

BDLogStream::BDLogStream(int32_t level) : level_(level) {}

BDLogStream::~BDLogStream()
{
  BDLogC(level_, "%s", oss_.str().c_str());
}

/////////////////////////// TBLOG //////////////////////////////////////

class TBWarningBuffer
{
public:
  TBWarningBuffer() : append_idx_(0), total_warning_count_(0)
  {
    // nop
  }
  ~TBWarningBuffer()
  {
    reset();
  }
  inline void reset(void)
  {
    append_idx_ = 0;
    total_warning_count_ = 0;
  }
  inline static void set_warn_log_on(const bool is_log_on)
  {
    is_log_on_ = is_log_on;
  }
  inline static bool is_warn_log_on(void)
  {
    return is_log_on_;
  }
  inline uint32_t get_total_warning_count(void) const
  {
    return total_warning_count_;
  }
  inline uint32_t get_buffer_size(void) const
  {
    return BUFFER_SIZE;
  }
  inline uint32_t get_readable_warning_count(void) const
  {
    return (total_warning_count_ < get_buffer_size()) ? total_warning_count_ : get_buffer_size();
  }
  inline uint32_t get_max_warn_len(void) const
  {
    return WarningItem::STR_LEN;
  }

  // idx [0, get_readable_warning_count)
  const char *get_warning(const uint32_t idx) const
  {
    const char *ret = NULL;
    if (idx < get_readable_warning_count())
    {
      uint32_t loc = idx;
      if (total_warning_count_ > BUFFER_SIZE)
      {
        loc = (append_idx_ + idx) % BUFFER_SIZE;
      }
      ret = item_[loc].get();
    }
    return ret;
  }

  int append_warning(const char *str)
  {
    //if (is_log_on_)
    {
      item_[append_idx_].set(str);
      append_idx_ = (append_idx_ + 1) % BUFFER_SIZE;
      total_warning_count_++;
    }
    return 0;
  }

  void reset_err_msg()
  {
    err_msg_.reset_err_msg();
  }

  void set_err_msg(const char *str)
  {
    err_msg_.set(str);
  }
  const char *get_err_msg() const
  {
    return err_msg_.get();
  }
  TBWarningBuffer &operator=(const TBWarningBuffer &other)
  {
    if (this != &other)
    {
      uint32_t n = 0;
      if (total_warning_count_ >= BUFFER_SIZE)
      {
        n = BUFFER_SIZE;
      }
      else
      {
        n = other.append_idx_;
      }
      for (uint32_t i = 0; i < n; ++i)
      {
        item_[i] = other.item_[i];
      }
      err_msg_ = other.err_msg_;
      append_idx_ = other.append_idx_;
      total_warning_count_ = other.total_warning_count_;
    }
    return *this;
  }

private:
  struct WarningItem
  {
    static const uint32_t STR_LEN = 512;
    char msg_[STR_LEN];
    int64_t timestamp_;
    int log_level_;
    int line_no_;

    void reset_err_msg()
    {
      msg_[0] = '\0';
    }
    void set(const char *str)
    {
      snprintf(msg_, STR_LEN, "%s", str);
    }
    const char *get() const
    {
      return static_cast<const char *>(msg_);
    }
    WarningItem &operator=(const WarningItem &other)
    {
      if (this != &other)
      {
        strcpy(msg_, other.msg_);
        timestamp_ = other.timestamp_;
        log_level_ = other.log_level_;
        line_no_ = other.line_no_;
      }
      return *this;
    }
  };

private:
  // const define
  static const uint32_t BUFFER_SIZE = 64;
  WarningItem item_[BUFFER_SIZE];
  WarningItem err_msg_;
  uint32_t append_idx_;
  uint32_t total_warning_count_;
  static bool is_log_on_;
};

bool TBWarningBuffer::is_log_on_ = false;

class TBWarningBufferFactory
{
public:
  TBWarningBufferFactory() : key_(INVALID_THREAD_KEY)
  {
    create_thread_key();
  }

  ~TBWarningBufferFactory()
  {
    delete_thread_key();
  }

  TBWarningBuffer *get_buffer() const
  {
    static __thread TBWarningBuffer *buffer = NULL;
    if (NULL != buffer)
    {
      //return buffer, faster path
    }
    else if (INVALID_THREAD_KEY != key_)
    {
      void *ptr = pthread_getspecific(key_);
      if (NULL == ptr)
      {
        ptr = malloc(sizeof(TBWarningBuffer));
        if (NULL != ptr)
        {
          int ret = pthread_setspecific(key_, ptr);
          if (0 != ret)
          {
            // TB_LOG(ERROR, "pthread_setspecific failed:%d", ret);
            free(ptr);
            ptr = NULL;
          }
          else
          {
            buffer = new (ptr) TBWarningBuffer();
          }
        }
        else
        {
          // malloc failed;
          // TB_LOG(ERROR, "malloc thread specific memeory failed.");
        }
      }
      else
      {
        // got exist ptr;
        buffer = reinterpret_cast<TBWarningBuffer *>(ptr);
      }
    }
    else
    {
      // TB_LOG(ERROR, "thread key must be initialized "
      //    "and size must great than zero, key:%u,size:%d", key_, size_);
    }
    return buffer;
  }

private:
  int create_thread_key()
  {
    int ret = pthread_key_create(&key_, destroy_thread_key);
    if (0 != ret)
    {
      PRINT_ERROR("cannot create thread key:%d\n", ret);
    }
    return (0 == ret) ? 0 : 1;
  }

  int delete_thread_key()
  {
    int ret = -1;
    if (INVALID_THREAD_KEY != key_)
    {
      ret = pthread_key_delete(key_);
    }
    if (0 != ret)
    {
      PRINT_ERROR("delete thread key key_ failed.\n");
    }
    return (0 == ret) ? 0 : 1;
  }

  static void destroy_thread_key(void *ptr)
  {
    if (NULL != ptr)
      free(ptr);
    //fprintf(stderr, "destroy %p\n", ptr);
  }

private:
  static const pthread_key_t INVALID_THREAD_KEY = ((uint32_t)-1); //UINT32_MAX;;
private:
  pthread_key_t key_;
};

TBWarningBuffer *TBGetTsiWarningBuffer()
{
  static TBWarningBufferFactory instance;
  return instance.get_buffer();
}

TBLogExtraHeaderCallback TB_LOG_EXTRA_HEADER_CB = NULL;
const char *const TBLogger::_errstr[] = {"ERROR", "USER_ERR", "WARN", "INFO", "TRACE", "DEBUG"};

TBLogger::TBLogger()
{
  _fd = fileno(stderr);
  _wf_fd = fileno(stderr);
  _level = 9;
  _wf_level = 2; /* WARN */
  _name = NULL;
  _check = 0;
  _maxFileSize = 0;
  _maxFileIndex = 0;
  pthread_mutex_init(&_fileSizeMutex, NULL);
  pthread_mutex_init(&_fileIndexMutex, NULL);
  _flag = false;
  _wf_flag = false;
}

TBLogger::~TBLogger()
{
  if (_name != NULL)
  {
    free(_name);
    _name = NULL;
    close(_fd);
    if (_wf_flag)
      close(_wf_fd);
  }
  pthread_mutex_destroy(&_fileSizeMutex);
  pthread_mutex_destroy(&_fileIndexMutex);
}

void TBLogger::setLogLevel(const char *level, const char *wf_level)
{
  if (level == NULL)
    return;
  int l = sizeof(_errstr) / sizeof(char *);
  for (int i = 0; i < l; i++)
  {
    if (strcasecmp(level, _errstr[i]) == 0)
    {
      _level = i;
      break;
    }
  }
  if (NULL != wf_level)
  {
    for (int j = 0; j < l; j++)
    {
      if (strcasecmp(wf_level, _errstr[j]) == 0)
      {
        _wf_level = j;
        break;
      }
    }
  }
}

void TBLogger::setFileName(const char *filename, bool flag, bool open_wf)
{
  bool need_closing = false;
  if (_name)
  {
    need_closing = true;
    free(_name);
    _name = NULL;
  }
  _name = strdup(filename);
  int fd = open(_name, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
  _flag = flag;
  if (!_flag)
  {
    dup2(fd, _fd);
    dup2(fd, 1);
    if (_fd != 2)
      dup2(fd, 2);
    if (fd != _fd)
      close(fd);
  }
  else
  {
    if (need_closing)
    {
      close(_fd);
    }
    _fd = fd;
  }
  if (_wf_flag && need_closing)
  {
    close(_wf_fd);
  }
  //open wf file
  _wf_flag = open_wf;
  if (_wf_flag)
  {
    char tmp_file_name[256];
    memset(tmp_file_name, 0, sizeof(tmp_file_name));
    snprintf(tmp_file_name, sizeof(tmp_file_name), "%s.wf", _name);
    fd = open(tmp_file_name, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
    _wf_fd = fd;
  }
}

static char NEWLINE[1] = {'\n'};
static const int MAX_LOG_SIZE = 64 * 1024; // 64kb
void TBLogger::logMessage(int level, const char *file, int line, const char *function, pthread_t tid, const char *fmt, ...)
{
  if (level > _level)
    return;

  if (_check && _name)
  {
    checkFile();
  }

  struct timeval tv;
  gettimeofday(&tv, NULL);
  struct tm tm;
  ::localtime_r((const time_t *)&tv.tv_sec, &tm);

  static __thread char data1[MAX_LOG_SIZE];
  char head[256];

  va_list args;
  va_start(args, fmt);
  int data_size = vsnprintf(data1, MAX_LOG_SIZE, fmt, args);
  va_end(args);
  if (data_size >= MAX_LOG_SIZE)
  {
    data_size = MAX_LOG_SIZE - 1;
  }
  // remove trailing '\n'
  while (data1[data_size - 1] == '\n')
    data_size--;
  data1[data_size] = '\0';

  int head_size;
  if (level < TB_LOG_LEVEL_INFO)
  {
    head_size = snprintf(head, 256, "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] %-5s %s (%s:%d) [%ld] ",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec,
                         _errstr[level], function, file, line, tid);
  }
  else
  {
    head_size = snprintf(head, 256, "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] %-5s %s:%d [%ld] ",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec,
                         _errstr[level], file, line, tid);
  }
  //truncated
  if (head_size >= 256)
  {
    head_size = 255;
  }
  // extra header
  char extra_head[256];
  extra_head[0] = '\0';
  int32_t extra_head_size = 0;
  if (NULL != TB_LOG_EXTRA_HEADER_CB)
  {
    extra_head_size = TB_LOG_EXTRA_HEADER_CB(extra_head, 256,
                                             level, file, line, function, tid);
  }
  //truncated
  if (extra_head_size >= 256)
  {
    extra_head_size = 255;
  }
  struct iovec vec[4];
  vec[0].iov_base = head;
  vec[0].iov_len = head_size;
  vec[1].iov_base = extra_head;
  vec[1].iov_len = extra_head_size;
  vec[2].iov_base = data1;
  vec[2].iov_len = data_size;
  vec[3].iov_base = NEWLINE;
  vec[3].iov_len = sizeof(NEWLINE);
  if (data_size > 0)
  {
    ::writev(_fd, vec, 4);
    if (_wf_flag && level <= _wf_level)
      ::writev(_wf_fd, vec, 4);
  }
  if (_maxFileSize)
  {
    pthread_mutex_lock(&_fileSizeMutex);
    off_t offset = ::lseek(_fd, 0, SEEK_END);
    if (offset < 0)
    {
      // we got an error , ignore for now
    }
    else
    {
      if (static_cast<int64_t>(offset) >= _maxFileSize)
      {
        rotateLog(NULL);
      }
    }
    pthread_mutex_unlock(&_fileSizeMutex);
  }

  // write data to warning buffer for SQL
  if (TBWarningBuffer::is_warn_log_on() && data_size > 0)
  {
    if (level == TB_LOG_LEVEL_WARN)
    { // WARN only
      TBWarningBuffer *wb = TBGetTsiWarningBuffer();
      if (NULL != wb)
      {
        wb->append_warning(data1);
      }
    }
    else if (level == TB_LOG_LEVEL_USER_ERROR)
    {
      TBWarningBuffer *wb = TBGetTsiWarningBuffer();
      if (NULL != wb)
      {
        wb->set_err_msg(data1);
      }
    }
  }
  return;
}

void TBLogger::rotateLog(const char *filename, const char *fmt)
{
  if (filename == NULL && _name != NULL)
  {
    filename = _name;
  }
  char wf_filename[256];
  if (filename != NULL)
  {
    snprintf(wf_filename, sizeof(wf_filename), "%s.wf", filename);
  }
  if (access(filename, R_OK) == 0)
  {
    char oldLogFile[256];
    char old_wf_log_file[256];
    time_t t;
    time(&t);
    struct tm tm;
    localtime_r((const time_t *)&t, &tm);
    if (fmt != NULL)
    {
      char tmptime[256];
      strftime(tmptime, sizeof(tmptime), fmt, &tm);
      sprintf(oldLogFile, "%s.%s", filename, tmptime);
      snprintf(old_wf_log_file, sizeof(old_wf_log_file), "%s.%s", wf_filename, tmptime);
    }
    else
    {
      sprintf(oldLogFile, "%s.%04d%02d%02d%02d%02d%02d",
              filename, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
              tm.tm_hour, tm.tm_min, tm.tm_sec);
      snprintf(old_wf_log_file, sizeof(old_wf_log_file), "%s.%04d%02d%02d%02d%02d%02d",
               wf_filename, tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
               tm.tm_hour, tm.tm_min, tm.tm_sec);
    }
    if (_maxFileIndex > 0)
    {
      pthread_mutex_lock(&_fileIndexMutex);
      if (_fileList.size() >= _maxFileIndex)
      {
        std::string oldFile = _fileList.front();
        _fileList.pop_front();
        unlink(oldFile.c_str());
      }
      _fileList.push_back(oldLogFile);
      pthread_mutex_unlock(&_fileIndexMutex);
    }
    rename(filename, oldLogFile);
    if (_wf_flag && _maxFileIndex > 0)
    {
      pthread_mutex_lock(&_fileIndexMutex);
      if (_wf_file_list.size() >= _maxFileIndex)
      {
        std::string old_wf_file = _wf_file_list.front();
        _wf_file_list.pop_front();
        unlink(old_wf_file.c_str());
      }
      _wf_file_list.push_back(old_wf_log_file);
      pthread_mutex_unlock(&_fileIndexMutex);
    }
    rename(wf_filename, old_wf_log_file);
  }
  int fd = open(filename, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
  if (!_flag)
  {
    dup2(fd, _fd);
    dup2(fd, 1);
    if (_fd != 2)
      dup2(fd, 2);
    close(fd);
  }
  else
  {
    if (_fd != 2)
    {
      close(_fd);
    }
    _fd = fd;
  }
  if (_wf_flag)
  {
    fd = open(wf_filename, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
    if (_wf_fd != 2)
    {
      close(_wf_fd);
    }
    _wf_fd = fd;
  }
}

void TBLogger::checkFile()
{
  struct stat stFile;
  struct stat stFd;

  fstat(_fd, &stFd);
  int err = stat(_name, &stFile);
  if ((err == -1 && errno == ENOENT) || (err == 0 && (stFile.st_dev != stFd.st_dev || stFile.st_ino != stFd.st_ino)))
  {
    int fd = open(_name, O_RDWR | O_CREAT | O_APPEND | O_LARGEFILE, LOG_FILE_MODE);
    if (!_flag)
    {
      dup2(fd, _fd);
      dup2(fd, 1);
      if (_fd != 2)
        dup2(fd, 2);
      close(fd);
    }
    else
    {
      if (_fd != 2)
      {
        close(_fd);
      }
      _fd = fd;
    }
  }
}

TBLogger &TBLogger::getLogger()
{
  static TBLogger logger;
  return logger;
}

void TBLogger::setMaxFileSize(int64_t maxFileSize)
{
  // 1GB
  if (maxFileSize < 0x0 || maxFileSize > 0x40000000)
  {
    maxFileSize = 0x40000000; //1GB
  }
  _maxFileSize = maxFileSize;
}

void TBLogger::setMaxFileIndex(int maxFileIndex)
{
  if (maxFileIndex < 0x00)
  {
    maxFileIndex = 0x0F;
  }
  if (maxFileIndex > 0x400)
  {                       //1024
    maxFileIndex = 0x400; //1024
  }
  _maxFileIndex = maxFileIndex;
}

/////////////////////// ASYNC TBLOG ////////////////////////////////////////////

static const int32_t TB_PER_THREAD_MAX_LOG_COUNT = 1024;
static const int32_t TB_PER_THREAD_MAX_LOG_LINE = 1024;

struct tb_log_item
{
  pthread_t tid;
  int log_type;
  const char *file;
  int line;
  const char *function;
};

typedef RingQueue<char *> TBLogQueue;

__thread TBLogQueue *g_tb_pre_thread_log_queue = NULL;

static std::vector<TBLogQueue *> g_tb_all_log_queues;
static Mutex g_tb_queues_mutex;
static Mutex g_tb_init_mutex;

class TBAsyncLogWriter : public StdThreadBase
{
public:
  TBAsyncLogWriter()
  {
    start();
    stop_ = false;
  }

  ~TBAsyncLogWriter()
  {
    join();
  }

  void run()
  {
    prctl(PR_SET_NAME, "tb_async_logger", 0, 0, 0);
    std::vector<TBLogQueue *> tmp_log_queues;
    int idle_count = 0;

    while (!stop_)
    {
      {
        MutexLock guard(&g_tb_queues_mutex);
        tmp_log_queues = g_tb_all_log_queues;
      }
      bool busy = false;
      for (size_t i = 0; i < tmp_log_queues.size(); ++i)
      {
        TBLogQueue *queue = tmp_log_queues[i];
        char *buffer = NULL;
        while (queue->get(buffer))
        {
          tb_log_item *item = (tb_log_item *)buffer;
          char *log_buffer = buffer + sizeof(tb_log_item);
          TB_LOGGER.logMessage(item->log_type, item->file, item->line,
                               item->function, item->tid, "%s", log_buffer);
          delete[] buffer;
          busy = true;
        }
      }
      if (busy)
      {
        idle_count = 0;
      }
      else
      {
        if (++idle_count >= 100)
        {
          idle_count = 0;
          usleep(10000);
        }
      }
    }
  }

private:
  bool stop_;
};

// auto init async log writer
static TBAsyncLogWriter *async_log_writer = NULL;

void TBAsyncLog(int log_type, const char *file, int line,
                const char *function, pthread_t tid, const char *format, ...)
{
  if (log_type > TB_LOGGER._level)
  {
    return;
  }

  if (UNLIKELY(!g_tb_pre_thread_log_queue))
  {
    g_tb_pre_thread_log_queue = new TBLogQueue(TB_PER_THREAD_MAX_LOG_COUNT);
    {
      MutexLock guard(&g_tb_queues_mutex);
      g_tb_all_log_queues.push_back(g_tb_pre_thread_log_queue);
    }
  }

  if (UNLIKELY(!async_log_writer))
  {
    MutexLock guard(&g_tb_init_mutex);
    if (!async_log_writer)
    {
      async_log_writer = new TBAsyncLogWriter();
    }
  }

  if (g_tb_pre_thread_log_queue->full())
  {
    return;
  }

  char *buffer = new char[TB_PER_THREAD_MAX_LOG_LINE];
  tb_log_item *item = (tb_log_item *)buffer;

  item->log_type = log_type;
  item->file = file;
  item->function = function;
  item->line = line;
  item->tid = tid;

  char *log_buffer = buffer + sizeof(tb_log_item);
  const int log_buffer_size = TB_PER_THREAD_MAX_LOG_LINE - sizeof(tb_log_item);

  va_list args;
  va_start(args, format);
  // vsnprintf() write the trailing null byte ('\0'))
  vsnprintf(log_buffer, log_buffer_size, format, args);
  va_end(args);

  if (!g_tb_pre_thread_log_queue->put(buffer))
  {
    delete[] buffer;
  }
}

} // namespace util
} // namespace mycc