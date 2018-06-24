
#ifndef MYCC_UTIL_LOGGING_UTIL_H_
#define MYCC_UTIL_LOGGING_UTIL_H_

#include <sstream>

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
using util::DEBUG;
using util::ERROR;
using util::FATAL;
using util::INFO;
using util::WARNING;

void SetLogLevel(int32_t level);
bool SetLogFile(const char *path, bool append = false);
bool SetWarningFile(const char *path, bool append = false);
bool SetLogSize(int32_t size); // in MB
bool SetLogCount(int32_t count);
bool SetLogSizeLimit(int32_t size); // in MB

void LogC(int32_t level, const char *fmt, ...);

class LogStream
{
public:
  LogStream(int32_t level);
  template <class T>
  LogStream &operator<<(const T &t)
  {
    oss_ << t;
    return *this;
  }
  ~LogStream();

private:
  int32_t level_;
  std::ostringstream oss_;
};

#define LOG(level, fmt, args...) ::mycc::util::LogC(level, "[%s:%d] " fmt, __FILE__, __LINE__, ##args)
#define LOGS(level) ::mycc::util::LogStream(level)

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_LOGGING_UTIL_H_