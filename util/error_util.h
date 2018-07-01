
#ifndef MYCC_UTIL_ERROR_UTIL_H_
#define MYCC_UTIL_ERROR_UTIL_H_

#include <errno.h>
#include <execinfo.h> // linux
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdexcept>
#include "types_util.h"

namespace mycc
{
namespace util
{

#define ENABLE_DEBUG 1

#define STR_ERRORNO() (errno == 0 ? "None" : strerror(errno))

string CurrentTestTimeString();

#define PANIC(fmt, ...)                                                           \
  fprintf(stderr, "PANIC |%s|[%s:%d](%s) errno: %d %s, " fmt,                     \
          ::mycc::util::CurrentTestTimeString().c_str(),                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr);                                                                 \
  abort()

#define PANIC_ENFORCE(c, fmt, ...)                 \
  if (!(c))                                        \
  {                                                \
    PANIC("%s is False, " fmt, #c, ##__VA_ARGS__); \
  }

/// Print error utils

#define PRINT_INFO(fmt, ...)                                \
  fprintf(stderr, "INFO |%s|[%s:%d](%s) " fmt,              \
          ::mycc::util::CurrentTestTimeString().c_str(),    \
          __FILE__, __LINE__, __FUNCTION__, ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_WARN(fmt, ...)                                                      \
  fprintf(stderr, "WARN |%s|[%s:%d](%s) errno: %d %s, " fmt,                      \
          ::mycc::util::CurrentTestTimeString().c_str(),                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_ERROR(fmt, ...)                                                     \
  fprintf(stderr, "ERROR |%s|[%s:%d](%s) errno: %d %s, " fmt,                     \
          ::mycc::util::CurrentTestTimeString().c_str(),                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)

#define PRINT_FATAL(fmt, ...)                                                     \
  fprintf(stderr, "FATAL |%s|[%s:%d](%s) errno: %d %s, " fmt,                     \
          ::mycc::util::CurrentTestTimeString().c_str(),                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)                                                                  \
      abort()

#define PRINT_TRACE(fmt, ...)                                                       \
  if (ENABLE_DEBUG)                                                                 \
  {                                                                                 \
    fprintf(stderr, "TRACE |%s|[%s:%d](%s) errno: %d %s, " fmt,                     \
            ::mycc::util::CurrentTestTimeString().c_str(),                          \
            __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
    fflush(stderr)                                                                  \
  }

#define PRINT_ASSERT(fmt, ...)                                                    \
  fprintf(stderr, "ASSERT |%s|[%s:%d](%s) errno: %d %s, " fmt,                    \
          ::mycc::util::CurrentTestTimeString().c_str(),                          \
          __FILE__, __LINE__, __FUNCTION__, errno, STR_ERRORNO(), ##__VA_ARGS__); \
  fflush(stderr)

// check utils

#define PRINT_CHECK_TRUE(c)                \
  if (!(c))                                \
  {                                        \
    PRINT_ASSERT("%s is not TRUE \n", #c); \
  }

#define PRINT_CHECK_FALSE(c)                \
  if (c)                                    \
  {                                         \
    PRINT_ASSERT("%s is not FALSE \n", #c); \
  }

#define PRINT_CHECK_EQ(c, val)                    \
  if ((c) != (val))                               \
  {                                               \
    PRINT_ASSERT("%s is not EQ %s \n", #c, #val); \
  }

#define PRINT_CHECK_NE(c, val)                    \
  if ((c) == (val))                               \
  {                                               \
    PRINT_ASSERT("%s is not NE %s \n", #c, #val); \
  }

#define PRINT_CHECK_GE(c, val)                    \
  if ((c) < (val))                                \
  {                                               \
    PRINT_ASSERT("%s is not GE %s \n", #c, #val); \
  }

#define PRINT_CHECK_GT(c, val)                    \
  if ((c) <= (val))                               \
  {                                               \
    PRINT_ASSERT("%s is not GT %s \n", #c, #val); \
  }

#define PRINT_CHECK_LE(c, val)                    \
  if ((c) > (val))                                \
  {                                               \
    PRINT_ASSERT("%s is not LE %s \n", #c, #val); \
  }

#define PRINT_CHECK_LT(c, val)                    \
  if ((c) >= (val))                               \
  {                                               \
    PRINT_ASSERT("%s is not LT %s \n", #c, #val); \
  }

#define PANIC_TRUE(c)               \
  if (!(c))                         \
  {                                 \
    PANIC("%s is not TRUE \n", #c); \
  }

#define PANIC_FALSE(c)               \
  if (c)                             \
  {                                  \
    PANIC("%s is not FALSE \n", #c); \
  }

#define PANIC_EQ(c, val)                   \
  if ((c) != (val))                        \
  {                                        \
    PANIC("%s is not EQ %s \n", #c, #val); \
  }

#define PANIC_NE(c, val)                   \
  if ((c) == (val))                        \
  {                                        \
    PANIC("%s is not NE %s \n", #c, #val); \
  }

#define PANIC_GE(c, val)                   \
  if ((c) < (val))                         \
  {                                        \
    PANIC("%s is not GE %s \n", #c, #val); \
  }

#define PANIC_GT(c, val)                   \
  if ((c) <= (val))                        \
  {                                        \
    PANIC("%s is not GT %s \n", #c, #val); \
  }

#define PANIC_LE(c, val)                   \
  if ((c) > (val))                         \
  {                                        \
    PANIC("%s is not LE %s \n", #c, #val); \
  }

#define PANIC_LT(c, val)                   \
  if ((c) >= (val))                        \
  {                                        \
    PANIC("%s is not LT %s \n", #c, #val); \
  }

#define EXIT_FAIL(fmt, ...)        \
  PRINT_ERROR(fmt, ##__VA_ARGS__); \
  PRINT_ERROR("\n Exit fail \n");  \
  exit(EXIT_FAILURE)

#define PRINT_STACK_TRACE(fmt, ...)                                           \
  do                                                                          \
  {                                                                           \
    fprintf(stderr, "FATAL (%s:%d: errno: %s) " fmt "\n", __FILE__, __LINE__, \
            errno == 0 ? "None" : strerror(errno), ##__VA_ARGS__);            \
    void *buffer[255];                                                        \
    const int32_t calls = backtrace(buffer, sizeof(buffer) / sizeof(void *)); \
    backtrace_symbols_fd(buffer, calls, 1);                                   \
    \                                                                         \
  } while (0)

// For propagating errors when calling a function.
#define RETURN_IF_ERROR(expr)                    \
  do                                             \
  {                                              \
    const ::mycc::util::Status _status = (expr); \
    if (PREDICT_FALSE(!_status.ok()))            \
      return _status;                            \
  } while (0)

class Exception : public std::exception
{
public:
  explicit Exception(const string &buffer);
  Exception(const string &buffer, int err);

  virtual ~Exception() throw();
  virtual const char *what() const throw();
  int getErrCode() { return _code; }

private:
  void getBacktrace();

private:
  string _buffer;
  int _code; // errno
};

} // namespace util
} // namespace mycc

#endif // MYCC_UTIL_ERROR_UTIL_H_