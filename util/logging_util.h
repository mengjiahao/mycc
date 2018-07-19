
#ifndef MYCC_UTIL_LOGGING_UTIL_H_
#define MYCC_UTIL_LOGGING_UTIL_H_

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <deque>
#include <iostream>
#include <new>
#include <sstream>
#include <string>
#include "lock_free_util.h"
#include "types_util.h"

namespace mycc
{
namespace util
{

enum LogLevel
{
  DEBUG = 2,
  INFO = 4,
  WARNING = 8,
  ERROR = 16,
  FATAL = 32,
};

class DateLogger
{
public:
  DateLogger() {}
  const char *HumanDate()
  {
    time_t time_value = time(NULL);
    struct tm *pnow;
    struct tm now;
    pnow = localtime_r(&time_value, &now);
    snprintf(buffer_, sizeof(buffer_), "%02d:%02d:%02d",
             pnow->tm_hour, pnow->tm_min, pnow->tm_sec);
    return buffer_;
  }

private:
  char buffer_[9];
};

class LogMessage
{
public:
  LogMessage(const char *file, int line)
      : log_stream_(std::cerr)
  {
    log_stream_ << "[" << pretty_date_.HumanDate() << "] " << file << ":"
                << line << ": ";
  }
  ~LogMessage() { log_stream_ << '\n'; }
  std::ostream &stream() { return log_stream_; }

protected:
  std::ostream &log_stream_;

private:
  DateLogger pretty_date_;
  LogMessage(const LogMessage &);
  void operator=(const LogMessage &);
};

////////////////////// BD LOG ///////////////////////////

void BDSetLogLevel(int32_t level);
bool BDSetLogFile(const char *path, bool append = false);
bool BDSetWarningFile(const char *path, bool append = false);
bool BDSetLogSize(int32_t size); // in MB
bool BDSetLogCount(int32_t count);
bool BDSetLogSizeLimit(int32_t size); // in MB

void BDLogC(int32_t level, const char *fmt, ...);

class BDLogStream
{
public:
  BDLogStream(int32_t level);
  template <class T>
  BDLogStream &operator<<(const T &t)
  {
    oss_ << t;
    return *this;
  }
  ~BDLogStream();

private:
  int32_t level_;
  std::ostringstream oss_;
};

#define BDLOG(level, fmt, args...) ::mycc::util::BDLogC(level, "[%s:%d] " fmt, __FILE__, __LINE__, ##args)
#define BDLOGS(level) ::mycc::util::BDLogStream(level)

/////////////////////////// TB LOG /////////////////////////////

#define TB_LOG_LEVEL_ERROR 0
#define TB_LOG_LEVEL_USER_ERROR 1
#define TB_LOG_LEVEL_WARN 2
#define TB_LOG_LEVEL_INFO 3
#define TB_LOG_LEVEL_TRACE 4
#define TB_LOG_LEVEL_DEBUG 5

#define TB_LOG_LEVEL(level) TB_LOG_LEVEL_##level, __FILE__, __LINE__, __FUNCTION__, pthread_self()
#define TB_LOG_NUM_LEVEL(level) level, __FILE__, __LINE__, __FUNCTION__, pthread_self()
#define TB_LOGGER ::mycc::util::TBLogger::getLogger()
#define TB_PRINT(level, ...) TB_LOGGER.logMessage(TB_LOG_LEVEL(level), __VA_ARGS__)
#define TB_LOG_BASE(level, ...) (TB_LOG_LEVEL_##level > TB_LOGGER._level) ? (void)0 : TB_PRINT(level, __VA_ARGS__)
#define TB_LOG(level, _fmt_, args...) ((TB_LOG_LEVEL_##level > TB_LOGGER._level) ? (void)0 : TB_LOG_BASE(level, _fmt_, ##args))
#define TB_LOG_US(level, _fmt_, args...) \
  ((TB_LOG_LEVEL_##level > TB_LOGGER._level) ? (void)0 : TB_LOG_BASE(level, "[%ld][%ld][%ld] " _fmt_, pthread_self(), ::mycc::util::TBLogger::get_cur_tv().tv_sec, ::mycc::util::TBLogger::get_cur_tv().tv_usec, ##args))

typedef int32_t (*TBLogExtraHeaderCallback)(char *buf, int32_t buf_size,
                                            int level, const char *file, int line,
                                            const char *function, pthread_t tid);

extern TBLogExtraHeaderCallback TB_LOG_EXTRA_HEADER_CB;

class TBLogger
{
public:
  static const mode_t LOG_FILE_MODE = 0644;
  TBLogger();
  ~TBLogger();

  void rotateLog(const char *filename, const char *fmt = NULL);
  void logMessage(int level, const char *file, int line, const char *function, pthread_t tid, const char *fmt, ...) __attribute__((format(printf, 7, 8)));
  /**
     * @brief set log putout level
     * @param level DEBUG|WARN|INFO|TRACE|ERROR
     * @param wf_level set the level putout to wf log file
     */
  void setLogLevel(const char *level, const char *wf_level = NULL);
  /**
     * @brief set log file name
     * @param filename log file name
     * @param flag whether to redirect stdout to log file, if false, redirect it
     * @param open_wf whether to open wf log file, default close
     */
  void setFileName(const char *filename, bool flag = false, bool open_wf = false);
  void checkFile();
  void setCheck(int v) { _check = v; }
  void setMaxFileSize(int64_t maxFileSize = 0x40000000);
  void setMaxFileIndex(int maxFileIndex = 0x0F);

  static inline struct timeval get_cur_tv()
  {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv;
  };

  static TBLogger &getLogger();

private:
  int _fd;
  int _wf_fd;
  char *_name;
  int _check;
  uint64_t _maxFileIndex;
  int64_t _maxFileSize;
  bool _flag;
  bool _wf_flag;

public:
  int _level;
  int _wf_level;

private:
  std::deque<string> _fileList;
  std::deque<string> _wf_file_list;
  static const char *const _errstr[];
  pthread_mutex_t _fileSizeMutex;
  pthread_mutex_t _fileIndexMutex;
};

void TBAsyncLog(int log_type, const char *file, int line,
                const char *function, pthread_t tid, const char *format, ...);

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOGGING_UTIL_H_